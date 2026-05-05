#ifndef TLI_HYBRID_VARIANTS_H
#define TLI_HYBRID_VARIANTS_H
//
// Diagnostic ladder of hybrid variants. Each variant adds *one* piece of
// bookkeeping on top of the previous. By running all 5 in one benchmark
// invocation we can see the throughput cost of each piece in isolation.
//
//   V0  bare LIPP passthrough — ceiling, no hybrid logic
//   V1  V0 + heavy_regime_ flag (never set) — pure branch cost
//   V2  V1 + lazy-DPGM insert routing (lipp_insert_count_ / lipp_threshold)
//   V3  V2 + DPGM + bloom on heavy path (still synchronous, no BG thread)
//   V4  V3 + async flush w/ mutex + atomic — full functionality
//
// All variants are configured with lipp_threshold ≥ workload insert budget
// for the 10 % run, so heavy_regime_ never flips ⇒ hot path stays minimal.
// Thread safety in V4: BG drainer can only exist *after* heavy_regime_=true,
// so the V4 fast path (taken only when heavy_regime_=false) is provably
// race-free without any synchronisation.
//
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "../util.h"
#include "base.h"
#include "pgm_index_dynamic.hpp"
#include "./lipp/src/core/lipp.h"

namespace hybrid_variants_detail {

class BloomFilter {
 public:
  BloomFilter() = default;
  void reset(size_t n, double fpr = 0.01) {
    if (n < 64) n = 64;
    double m = -double(n) * std::log(fpr) /
               (std::log(2.0) * std::log(2.0));
    size_t mb = ((static_cast<size_t>(std::ceil(m)) + 63) / 64) * 64;
    bits_.assign(mb / 64, 0ULL);
    n_bits_ = mb;
    size_t k = static_cast<size_t>(
        std::round(double(mb) / double(n) * std::log(2.0)));
    k_ = std::max<size_t>(1, std::min<size_t>(k, 4));
  }
  void clear() { std::fill(bits_.begin(), bits_.end(), 0ULL); }
  void release() {
    std::vector<uint64_t>().swap(bits_);
    n_bits_ = 0; k_ = 0;
  }
  void insert(uint64_t key) {
    uint64_t h1 = mix(key), h2 = mix(h1 ^ 0x9E3779B97F4A7C15ULL);
    for (size_t i = 0; i < k_; ++i) {
      uint64_t pos = (h1 + i * h2) % n_bits_;
      bits_[pos >> 6] |= (1ULL << (pos & 63));
    }
  }
  bool maybe_contains(uint64_t key) const {
    if (n_bits_ == 0) return false;
    uint64_t h1 = mix(key), h2 = mix(h1 ^ 0x9E3779B97F4A7C15ULL);
    for (size_t i = 0; i < k_; ++i) {
      uint64_t pos = (h1 + i * h2) % n_bits_;
      if (!(bits_[pos >> 6] & (1ULL << (pos & 63)))) return false;
    }
    return true;
  }
  size_t size_in_bytes() const { return bits_.size() * sizeof(uint64_t); }
 private:
  static uint64_t mix(uint64_t x) {
    x ^= x >> 30; x *= 0xBF58476D1CE4E5B9ULL;
    x ^= x >> 27; x *= 0x94D049BB133111EBULL;
    x ^= x >> 31; return x;
  }
  std::vector<uint64_t> bits_;
  size_t n_bits_ = 0, k_ = 0;
};

}  // namespace hybrid_variants_detail

// ===========================================================================
// V0 — bare LIPP passthrough (ceiling).
// ===========================================================================
template <class KeyType, class SearchClass>
class HybridV0 : public Competitor<KeyType, SearchClass> {
 public:
  HybridV0(const std::vector<int>&) {}

  uint64_t Build(const std::vector<KeyValue<KeyType>>& data, size_t) {
    std::vector<std::pair<KeyType, uint64_t>> ld;
    ld.reserve(data.size());
    for (auto& d : data) ld.emplace_back(d.key, d.value);
    return util::timing([&] {
      lipp_.bulk_load(ld.data(), static_cast<int>(ld.size()));
    });
  }

  size_t EqualityLookup(const KeyType& k, uint32_t) const {
    uint64_t v;
    if (!lipp_.find(k, v)) return util::OVERFLOW;
    return static_cast<size_t>(v);
  }

  uint64_t RangeQuery(const KeyType&, const KeyType&, uint32_t) const { return 0; }
  void Insert(const KeyValue<KeyType>& d, uint32_t) { lipp_.insert(d.key, d.value); }

  std::string name() const { return "HybridV0"; }
  std::size_t size() const { return lipp_.index_size(); }
  bool applicable(bool unique, bool, bool, bool multithread,
                  const std::string&) const { return unique && !multithread; }
  std::vector<std::string> variants() const { return {SearchClass::name(), ""}; }

 private:
  LIPP<KeyType, uint64_t> lipp_;
};

// ===========================================================================
// V1 — adds heavy_regime_ flag (never flipped) → pure branch cost.
// ===========================================================================
template <class KeyType, class SearchClass>
class HybridV1 : public Competitor<KeyType, SearchClass> {
 public:
  HybridV1(const std::vector<int>&) {}

  uint64_t Build(const std::vector<KeyValue<KeyType>>& data, size_t) {
    heavy_regime_ = false;
    std::vector<std::pair<KeyType, uint64_t>> ld;
    ld.reserve(data.size());
    for (auto& d : data) ld.emplace_back(d.key, d.value);
    return util::timing([&] {
      lipp_.bulk_load(ld.data(), static_cast<int>(ld.size()));
    });
  }

  size_t EqualityLookup(const KeyType& k, uint32_t) const {
    if (__builtin_expect(!heavy_regime_, 1)) {
      uint64_t v;
      if (!lipp_.find(k, v)) return util::OVERFLOW;
      return static_cast<size_t>(v);
    }
    return util::OVERFLOW;  // unreachable in this variant
  }

  uint64_t RangeQuery(const KeyType&, const KeyType&, uint32_t) const { return 0; }
  void Insert(const KeyValue<KeyType>& d, uint32_t) { lipp_.insert(d.key, d.value); }

  std::string name() const { return "HybridV1"; }
  std::size_t size() const { return lipp_.index_size(); }
  bool applicable(bool unique, bool, bool, bool multithread,
                  const std::string&) const { return unique && !multithread; }
  std::vector<std::string> variants() const { return {SearchClass::name(), ""}; }

 private:
  LIPP<KeyType, uint64_t> lipp_;
  bool heavy_regime_ = false;
};

// ===========================================================================
// V2 — adds lazy-DPGM insert routing. heavy_regime_ flips after lipp_threshold
//      inserts. DPGM is *not* yet involved; flipped inserts go nowhere
//      (essentially measuring the cost of the routing branch on insert).
// ===========================================================================
template <class KeyType, class SearchClass,
          size_t lipp_threshold = 2000000>
class HybridV2 : public Competitor<KeyType, SearchClass> {
 public:
  HybridV2(const std::vector<int>&) {}

  uint64_t Build(const std::vector<KeyValue<KeyType>>& data, size_t) {
    heavy_regime_ = false;
    lipp_insert_count_ = 0;
    std::vector<std::pair<KeyType, uint64_t>> ld;
    ld.reserve(data.size());
    for (auto& d : data) ld.emplace_back(d.key, d.value);
    return util::timing([&] {
      lipp_.bulk_load(ld.data(), static_cast<int>(ld.size()));
    });
  }

  size_t EqualityLookup(const KeyType& k, uint32_t) const {
    if (__builtin_expect(!heavy_regime_, 1)) {
      uint64_t v;
      if (!lipp_.find(k, v)) return util::OVERFLOW;
      return static_cast<size_t>(v);
    }
    return util::OVERFLOW;
  }

  uint64_t RangeQuery(const KeyType&, const KeyType&, uint32_t) const { return 0; }

  void Insert(const KeyValue<KeyType>& d, uint32_t) {
    if (__builtin_expect(lipp_insert_count_ < lipp_threshold, 1)) {
      lipp_.insert(d.key, d.value);
      ++lipp_insert_count_;
      return;
    }
    heavy_regime_ = true;
    // intentionally noop in V2 — routing branch only
  }

  std::string name() const { return "HybridV2"; }
  std::size_t size() const { return lipp_.index_size(); }
  bool applicable(bool unique, bool, bool, bool multithread,
                  const std::string&) const { return unique && !multithread; }
  std::vector<std::string> variants() const {
    return {SearchClass::name(), "lipp" + std::to_string(lipp_threshold)};
  }

 private:
  LIPP<KeyType, uint64_t> lipp_;
  bool heavy_regime_ = false;
  size_t lipp_insert_count_ = 0;
};

// ===========================================================================
// V3 — V2 + DPGM + bloom (lazy-allocated). Synchronous flush only (no BG
//      thread, no mutex, no atomic). Lookups in heavy regime go through bloom
//      gate then DPGM.
// ===========================================================================
template <class KeyType, class SearchClass,
          size_t pgm_error      = 64,
          size_t flush_every    = 2000000,
          size_t lipp_threshold = 2000000>
class HybridV3 : public Competitor<KeyType, SearchClass> {
  using PGMType = DynamicPGMIndex<KeyType, uint64_t, SearchClass,
                                  PGMIndex<KeyType, SearchClass, pgm_error, 16>>;

 public:
  HybridV3(const std::vector<int>&) {}

  uint64_t Build(const std::vector<KeyValue<KeyType>>& data, size_t) {
    heavy_regime_ = false;
    lipp_insert_count_ = 0;
    dpgm_count_ = 0;
    active_pgm_.reset();
    bloom_.release();
    std::vector<std::pair<KeyType, uint64_t>> ld;
    ld.reserve(data.size());
    for (auto& d : data) ld.emplace_back(d.key, d.value);
    return util::timing([&] {
      lipp_.bulk_load(ld.data(), static_cast<int>(ld.size()));
    });
  }

  size_t EqualityLookup(const KeyType& k, uint32_t) const {
    if (__builtin_expect(!heavy_regime_, 1)) {
      uint64_t v;
      if (!lipp_.find(k, v)) return util::OVERFLOW;
      return static_cast<size_t>(v);
    }
    return SlowLookup(k);
  }

  uint64_t RangeQuery(const KeyType&, const KeyType&, uint32_t) const { return 0; }

  void Insert(const KeyValue<KeyType>& d, uint32_t) {
    if (__builtin_expect(lipp_insert_count_ < lipp_threshold, 1)) {
      lipp_.insert(d.key, d.value);
      ++lipp_insert_count_;
      return;
    }
    InsertHeavy(d);
  }

  std::string name() const { return "HybridV3"; }
  std::size_t size() const {
    return lipp_.index_size() + bloom_.size_in_bytes() +
           (active_pgm_ ? active_pgm_->size_in_bytes() : 0);
  }
  bool applicable(bool unique, bool, bool, bool multithread,
                  const std::string&) const {
    return unique && !multithread &&
           SearchClass::name() != std::string("LinearAVX");
  }
  std::vector<std::string> variants() const {
    return {SearchClass::name(),
            std::to_string(pgm_error) + "_lipp" +
                std::to_string(lipp_threshold)};
  }

 private:
  __attribute__((noinline, cold))
  size_t SlowLookup(const KeyType& k) const {
    uint64_t v;
    if (lipp_.find(k, v)) return static_cast<size_t>(v);
    if (dpgm_count_ > 0 &&
        bloom_.maybe_contains(static_cast<uint64_t>(k))) {
      auto it = active_pgm_->find(k);
      if (it != active_pgm_->end()) return static_cast<size_t>(it->value());
    }
    return util::OVERFLOW;
  }

  __attribute__((noinline, cold))
  void InsertHeavy(const KeyValue<KeyType>& d) {
    if (!active_pgm_) {
      active_pgm_ = std::make_unique<PGMType>();
      bloom_.reset(flush_every);
      heavy_regime_ = true;
    }
    active_pgm_->insert(d.key, d.value);
    bloom_.insert(static_cast<uint64_t>(d.key));
    ++dpgm_count_;
    // V3 is synchronous-only — no flush logic. dpgm_count_ just grows.
  }

  LIPP<KeyType, uint64_t> lipp_;
  std::unique_ptr<PGMType> active_pgm_;
  hybrid_variants_detail::BloomFilter bloom_;
  bool heavy_regime_ = false;
  size_t lipp_insert_count_ = 0;
  size_t dpgm_count_ = 0;
};

// ===========================================================================
// V4 — full functionality: V3 + double-buffered DPGM + async flush + mutex +
//      atomic. Identical to current HybridPGMLIPPAsync semantics, but the hot
//      path uses heavy_regime_ instead of an atomic load so that the 10 %
//      workload (heavy_regime_ never set) stays at V0/V1 speed.
// ===========================================================================
template <class KeyType, class SearchClass,
          size_t pgm_error      = 64,
          size_t flush_every    = 2000000,
          size_t lipp_threshold = 2000000>
class HybridV4 : public Competitor<KeyType, SearchClass> {
  using PGMType = DynamicPGMIndex<KeyType, uint64_t, SearchClass,
                                  PGMIndex<KeyType, SearchClass, pgm_error, 16>>;

 public:
  HybridV4(const std::vector<int>&) {}
  ~HybridV4() { join_flush(); }

  uint64_t Build(const std::vector<KeyValue<KeyType>>& data, size_t) {
    heavy_regime_ = false;
    lipp_insert_count_ = 0;
    dpgm_count_ = 0;
    flushing_count_ = 0;
    flush_running_.store(false, std::memory_order_release);
    active_pgm_.reset();
    flushing_pgm_.reset();
    active_bloom_.release();
    flushing_bloom_.release();
    std::vector<std::pair<KeyType, uint64_t>> ld;
    ld.reserve(data.size());
    for (auto& d : data) ld.emplace_back(d.key, d.value);
    return util::timing([&] {
      lipp_.bulk_load(ld.data(), static_cast<int>(ld.size()));
    });
  }

  // Hot path: only safe to bypass synchronisation when heavy_regime_ is false,
  // which guarantees no BG thread can exist (BG is only spawned by
  // InsertHeavy, which sets heavy_regime_=true before doing so).
  size_t EqualityLookup(const KeyType& k, uint32_t) const {
    if (__builtin_expect(!heavy_regime_, 1)) {
      uint64_t v;
      if (!lipp_.find(k, v)) return util::OVERFLOW;
      return static_cast<size_t>(v);
    }
    return SlowLookup(k);
  }

  uint64_t RangeQuery(const KeyType&, const KeyType&, uint32_t) const { return 0; }

  void Insert(const KeyValue<KeyType>& d, uint32_t) {
    if (__builtin_expect(lipp_insert_count_ < lipp_threshold, 1)) {
      lipp_.insert(d.key, d.value);
      ++lipp_insert_count_;
      return;
    }
    InsertHeavy(d);
  }

  std::string name() const { return "HybridV4"; }
  std::size_t size() const {
    const_cast<HybridV4*>(this)->join_flush();
    size_t s = lipp_.index_size() + active_bloom_.size_in_bytes() +
               flushing_bloom_.size_in_bytes();
    if (active_pgm_)   s += active_pgm_->size_in_bytes();
    if (flushing_pgm_) s += flushing_pgm_->size_in_bytes();
    return s;
  }
  bool applicable(bool unique, bool, bool, bool multithread,
                  const std::string&) const {
    return unique && !multithread &&
           SearchClass::name() != std::string("LinearAVX");
  }
  std::vector<std::string> variants() const {
    return {SearchClass::name(),
            std::to_string(pgm_error) + "_flush" +
                std::to_string(flush_every) + "_lipp" +
                std::to_string(lipp_threshold)};
  }

 private:
  __attribute__((noinline, cold))
  size_t SlowLookup(const KeyType& k) const {
    const bool flushing = flush_running_.load(std::memory_order_acquire);
    uint64_t v;
    if (flushing) {
      std::lock_guard<std::mutex> lk(lipp_mutex_);
      if (lipp_.find(k, v)) return static_cast<size_t>(v);
    } else {
      if (lipp_.find(k, v)) return static_cast<size_t>(v);
    }
    const uint64_t bk = static_cast<uint64_t>(k);
    if (dpgm_count_ > 0 && active_bloom_.maybe_contains(bk)) {
      auto it = active_pgm_->find(k);
      if (it != active_pgm_->end()) return static_cast<size_t>(it->value());
    }
    if (flushing && flushing_bloom_.maybe_contains(bk)) {
      auto it = flushing_pgm_->find(k);
      if (it != flushing_pgm_->end()) return static_cast<size_t>(it->value());
    }
    return util::OVERFLOW;
  }

  __attribute__((noinline, cold))
  void InsertHeavy(const KeyValue<KeyType>& d) {
    if (!active_pgm_) {
      active_pgm_ = std::make_unique<PGMType>();
      active_bloom_.reset(flush_every);
      heavy_regime_ = true;
    }
    active_pgm_->insert(d.key, d.value);
    active_bloom_.insert(static_cast<uint64_t>(d.key));
    ++dpgm_count_;
    if (dpgm_count_ >= flush_every) trigger_flush();
  }

  void trigger_flush() {
    join_flush();
    flushing_pgm_ = std::move(active_pgm_);
    std::swap(active_bloom_, flushing_bloom_);
    flushing_count_ = dpgm_count_;
    dpgm_count_ = 0;
    active_pgm_ = std::make_unique<PGMType>();
    active_bloom_.reset(flush_every);
    flush_running_.store(true, std::memory_order_release);
    flush_thread_ = std::thread([this] { drain(); });
  }

  void drain() {
    constexpr size_t kBatch = 32;
    auto it = flushing_pgm_->begin();
    auto end = flushing_pgm_->end();
    while (it != end) {
      {
        std::lock_guard<std::mutex> lk(lipp_mutex_);
        for (size_t i = 0; i < kBatch && it != end; ++i, ++it)
          lipp_.insert(it->key(), it->value());
      }
      std::this_thread::yield();
    }
    flush_running_.store(false, std::memory_order_release);
  }

  void join_flush() {
    if (flush_thread_.joinable()) flush_thread_.join();
    if (flushing_count_ > 0) {
      flushing_pgm_.reset();
      flushing_bloom_.release();
      flushing_count_ = 0;
    }
  }

  LIPP<KeyType, uint64_t> lipp_;
  std::unique_ptr<PGMType> active_pgm_;
  std::unique_ptr<PGMType> flushing_pgm_;
  hybrid_variants_detail::BloomFilter active_bloom_;
  hybrid_variants_detail::BloomFilter flushing_bloom_;
  bool heavy_regime_ = false;
  size_t lipp_insert_count_ = 0;
  size_t dpgm_count_ = 0;
  size_t flushing_count_ = 0;
  mutable std::mutex lipp_mutex_;
  std::thread flush_thread_;
  std::atomic<bool> flush_running_{false};
};

#endif  // TLI_HYBRID_VARIANTS_H
