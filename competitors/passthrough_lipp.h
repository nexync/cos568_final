#ifndef TLI_PASSTHROUGH_LIPP_H
#define TLI_PASSTHROUGH_LIPP_H

#include <vector>

#include "../util.h"
#include "base.h"
#include "./lipp/src/core/lipp.h"

// Diagnostic passthrough class. Same plumbing as HybridPGMLIPPAsync
// (Competitor<KeyType, SearchClass> base, --only dispatch, etc.) but with
// no extra logic — just delegates to a LIPP. Used to isolate whether the
// HybridPGMLIPPAsync overhead vs the bare Lipp wrapper comes from the
// Hybrid class's own code (atomic load, branches, member layout) or from
// something architectural (template instantiation, dispatch).
template <class KeyType, class SearchClass>
class PassthroughLIPP : public Competitor<KeyType, SearchClass> {
 public:
  PassthroughLIPP(const std::vector<int>& /*params*/) {}

  uint64_t Build(const std::vector<KeyValue<KeyType>>& data,
                 size_t /*num_threads*/) {
    std::vector<std::pair<KeyType, uint64_t>> loading_data;
    loading_data.reserve(data.size());
    for (const auto& itm : data)
      loading_data.emplace_back(itm.key, itm.value);

    return util::timing([&] {
      lipp_.bulk_load(loading_data.data(),
                      static_cast<int>(loading_data.size()));
    });
  }

  size_t EqualityLookup(const KeyType& lookup_key,
                        uint32_t /*thread_id*/) const {
    uint64_t value;
    if (!lipp_.find(lookup_key, value)) return util::OVERFLOW;
    return static_cast<size_t>(value);
  }

  uint64_t RangeQuery(const KeyType& /*lower_key*/,
                      const KeyType& /*upper_key*/,
                      uint32_t /*thread_id*/) const {
    return 0;
  }

  void Insert(const KeyValue<KeyType>& data, uint32_t /*thread_id*/) {
    lipp_.insert(data.key, data.value);
  }

  std::string name() const { return "PassthroughLIPP"; }

  std::size_t size() const { return lipp_.index_size(); }

  bool applicable(bool unique, bool /*range_query*/, bool /*insert*/,
                  bool multithread,
                  const std::string& /*ops_filename*/) const {
    return unique && !multithread;
  }

  std::vector<std::string> variants() const {
    return {SearchClass::name(), ""};
  }

 private:
  LIPP<KeyType, uint64_t> lipp_;
};

#endif  // TLI_PASSTHROUGH_LIPP_H
