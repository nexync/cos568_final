#include "benchmarks/benchmark_hybrid_pgm_lipp_async.h"

#include "benchmark.h"
#include "benchmarks/common.h"
#include "competitors/hybrid_pgm_lipp_async.h"

// Pareto sweep (--pareto). Variants: <pgm_error, flush_every, lipp_threshold>
template <typename Searcher>
void benchmark_64_hybrid_pgm_lipp_async(tli::Benchmark<uint64_t>& benchmark,
                                         bool pareto,
                                         const std::vector<int>& /*params*/) {
  if (!pareto) {
    util::fail("HybridPGMLIPPAsync hyperparameters cannot be set manually");
  }
  benchmark.template Run<HybridPGMLIPPAsync<uint64_t, Searcher,  64, 2000000,        0>>();
  benchmark.template Run<HybridPGMLIPPAsync<uint64_t, Searcher,  64, 2000000, 2000000>>();
  benchmark.template Run<HybridPGMLIPPAsync<uint64_t, Searcher, 128, 2000000,        0>>();
  benchmark.template Run<HybridPGMLIPPAsync<uint64_t, Searcher, 128, 2000000, 2000000>>();
}

// Filename-based dispatch.
//   - 90 % insert (~1.8 M inserts): lipp_threshold = 0 ⇒ heavy regime from
//     the first insert ⇒ DPGM amortises. flush_every = 2 M skips flushing.
//   - 10 % insert (~200 K inserts): lipp_threshold = 2 M ⇒ heavy_regime_
//     never flips ⇒ pure-LIPP fast path on every lookup.
template <int record>
void benchmark_64_hybrid_pgm_lipp_async(tli::Benchmark<uint64_t>& benchmark,
                                         const std::string& filename) {
  if (filename.find("mix") == std::string::npos) return;

  const bool target_dataset =
      filename.find("fb_100M")    != std::string::npos ||
      filename.find("books_100M") != std::string::npos ||
      filename.find("osmc_100M")  != std::string::npos;
  if (!target_dataset) return;

  if (filename.find("0.900000i") != std::string::npos) {
    benchmark.template Run<HybridPGMLIPPAsync<uint64_t, BranchingBinarySearch<record>,  64, 2000000, 0>>();
    benchmark.template Run<HybridPGMLIPPAsync<uint64_t, BranchingBinarySearch<record>, 128, 2000000, 0>>();
  } else if (filename.find("0.100000i") != std::string::npos) {
    benchmark.template Run<HybridPGMLIPPAsync<uint64_t, BranchingBinarySearch<record>,  64, 2000000, 2000000>>();
    benchmark.template Run<HybridPGMLIPPAsync<uint64_t, BranchingBinarySearch<record>, 128, 2000000, 2000000>>();
  }
}

INSTANTIATE_TEMPLATES_MULTITHREAD(benchmark_64_hybrid_pgm_lipp_async, uint64_t);
