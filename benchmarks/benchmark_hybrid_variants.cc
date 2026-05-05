#include "benchmarks/benchmark_hybrid_variants.h"

#include "benchmark.h"
#include "benchmarks/common.h"
#include "competitors/hybrid_variants.h"

template <typename Searcher>
void benchmark_64_hybrid_variants(tli::Benchmark<uint64_t>& benchmark,
                                   bool /*pareto*/,
                                   const std::vector<int>& /*params*/) {
  benchmark.template Run<HybridV0<uint64_t, Searcher>>();
  benchmark.template Run<HybridV1<uint64_t, Searcher>>();
  benchmark.template Run<HybridV2<uint64_t, Searcher, 2000000>>();
  benchmark.template Run<HybridV3<uint64_t, Searcher, 64, 2000000, 2000000>>();
  benchmark.template Run<HybridV4<uint64_t, Searcher, 64, 2000000, 2000000>>();
}

// Test ladder on both mix workloads. Variant configurations:
//   - 10 % (lookup-heavy): lipp_threshold = 2 M for all variants ⇒
//     heavy_regime_ never flips ⇒ every variant exercises the fast path for
//     all 1.8 M lookups. Validates fast-path cost.
//   - 90 % (insert-heavy): also lipp_threshold = 2 M for V0/V1/V2 (acts like
//     pure LIPP, will be slow since LIPP-inserts hammer the tree). For
//     V3/V4 we additionally run lipp_threshold = 100 K so the heavy regime
//     engages and we can confirm the 5 + mops result is preserved.
//     V2 routes post-threshold inserts to a no-op (it's a measurement
//     variant) so we only run it with lipp_threshold = 2 M to avoid losing
//     keys.
template <int record>
void benchmark_64_hybrid_variants(tli::Benchmark<uint64_t>& benchmark,
                                   const std::string& filename) {
  if (filename.find("mix") == std::string::npos) return;

  const bool ten   = filename.find("0.100000i") != std::string::npos;
  const bool ninty = filename.find("0.900000i") != std::string::npos;
  if (!ten && !ninty) return;

  // Fast-path ladder — same config on both workloads. heavy_regime_ never
  // flips for either (lipp_threshold > insert budget on both 200 K and
  // 1.8 M sides), so every variant lookup hits the fast path.
  benchmark.template Run<HybridV0<uint64_t, BranchingBinarySearch<record>>>();
  benchmark.template Run<HybridV1<uint64_t, BranchingBinarySearch<record>>>();
  benchmark.template Run<HybridV2<uint64_t, BranchingBinarySearch<record>, 2000000>>();
  benchmark.template Run<HybridV3<uint64_t, BranchingBinarySearch<record>, 64, 2000000, 2000000>>();
  benchmark.template Run<HybridV4<uint64_t, BranchingBinarySearch<record>, 64, 2000000, 2000000>>();

  // 90 %-only: heavy-regime engagement on V3/V4. lipp_threshold = 0 means
  // every insert goes straight to DPGM (no lazy LIPP) — should reproduce
  // the v4 (no-lazy) result of ~5.3 mops. Smaller thresholds (≥1 below)
  // included for comparison.
  if (ninty) {
    benchmark.template Run<HybridV3<uint64_t, BranchingBinarySearch<record>, 64, 2000000, 0>>();
    benchmark.template Run<HybridV4<uint64_t, BranchingBinarySearch<record>, 64, 2000000, 0>>();
    benchmark.template Run<HybridV3<uint64_t, BranchingBinarySearch<record>, 64, 2000000, 100000>>();
    benchmark.template Run<HybridV4<uint64_t, BranchingBinarySearch<record>, 64, 2000000, 100000>>();
    benchmark.template Run<HybridV3<uint64_t, BranchingBinarySearch<record>, 64, 2000000, 250000>>();
    benchmark.template Run<HybridV4<uint64_t, BranchingBinarySearch<record>, 64, 2000000, 250000>>();
  }
}

INSTANTIATE_TEMPLATES_MULTITHREAD(benchmark_64_hybrid_variants, uint64_t);
