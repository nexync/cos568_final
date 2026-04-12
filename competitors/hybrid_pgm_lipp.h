#ifndef TLI_HYBRID_PGM_LIPP_H
#define TLI_HYBRID_PGM_LIPP_H

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <vector>

#include "../util.h"
#include "base.h"
#include "pgm_index_dynamic.hpp"
#include "./lipp/src/core/lipp.h"

// Hybrid DynamicPGM + LIPP index.
//
// Strategy (Milestone 2 – naive synchronous flush):
//   - Build: bulk-load all initial keys into LIPP.
//   - Insert: insert into DynamicPGM; when DPGM accumulates `flush_every`
//             inserts, flush every entry individually into LIPP and reset DPGM.
//   - Lookup: check DPGM first (small, recently inserted), then LIPP.
//
// Template parameters
//   pgm_error   – epsilon bound for DynamicPGM
//   flush_every – number of inserts between flushes (absolute count)
template <class KeyType, class SearchClass,
          size_t pgm_error  = 64,
          size_t flush_every = 500000>
class HybridPGMLIPP : public Competitor<KeyType, SearchClass> {
  using PGMType = DynamicPGMIndex<KeyType, uint64_t, SearchClass,
                                  PGMIndex<KeyType, SearchClass, pgm_error, 16>>;

 public:
  HybridPGMLIPP(const std::vector<int>& /*params*/) {}

  uint64_t Build(const std::vector<KeyValue<KeyType>>& data,
                 size_t /*num_threads*/) {
    std::vector<std::pair<KeyType, uint64_t>> loading_data;
    loading_data.reserve(data.size());
    for (const auto& itm : data)
      loading_data.emplace_back(itm.key, itm.value);

    dpgm_count_ = 0;

    return util::timing([&] {
      lipp_.bulk_load(loading_data.data(),
                      static_cast<int>(loading_data.size()));
    });
  }

  size_t EqualityLookup(const KeyType& lookup_key,
                        uint32_t /*thread_id*/) const {
    // Check the small DPGM buffer first (recently inserted keys).
    if (dpgm_count_ > 0) {
      auto it = pgm_.find(lookup_key);
      if (it != pgm_.end())
        return static_cast<size_t>(it->value());
    }

    // Fall back to LIPP.
    uint64_t value;
    if (lipp_.find(lookup_key, value))
      return static_cast<size_t>(value);

    return util::OVERFLOW;
  }

  uint64_t RangeQuery(const KeyType& /*lower_key*/,
                      const KeyType& /*upper_key*/,
                      uint32_t /*thread_id*/) const {
    return 0;
  }

  void Insert(const KeyValue<KeyType>& data, uint32_t /*thread_id*/) {
    pgm_.insert(data.key, data.value);
    ++dpgm_count_;

    if (dpgm_count_ >= flush_every)
      flush_to_lipp();
  }

  std::string name() const { return "HybridPGMLIPP"; }

  std::size_t size() const {
    return pgm_.size_in_bytes() + lipp_.index_size();
  }

  bool applicable(bool unique, bool /*range_query*/, bool /*insert*/,
                  bool multithread,
                  const std::string& /*ops_filename*/) const {
    // LIPP requires unique keys; LinearAVX unsupported by DPGM.
    return unique && !multithread &&
           SearchClass::name() != std::string("LinearAVX");
  }

  std::vector<std::string> variants() const {
    return {SearchClass::name(),
            std::to_string(pgm_error) + "_flush" +
                std::to_string(flush_every)};
  }

 private:
  void flush_to_lipp() {
    // Naive flush: iterate DPGM in sorted order, insert each entry into LIPP.
    for (auto it = pgm_.begin(); it != pgm_.end(); ++it)
      lipp_.insert(it->key(), it->value());

    // Reset DPGM to an empty index.
    pgm_       = PGMType();
    dpgm_count_ = 0;
  }

  PGMType            pgm_;
  LIPP<KeyType, uint64_t> lipp_;
  size_t             dpgm_count_ = 0;
};

#endif  // TLI_HYBRID_PGM_LIPP_H
