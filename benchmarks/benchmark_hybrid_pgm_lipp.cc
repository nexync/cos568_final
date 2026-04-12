#include "benchmarks/benchmark_hybrid_pgm_lipp.h"

#include "benchmark.h"
#include "benchmarks/common.h"
#include "competitors/hybrid_pgm_lipp.h"

// ---------------------------------------------------------------------------
// Pareto sweep (--pareto flag): try several (pgm_error, flush_every) combos.
// ---------------------------------------------------------------------------
template <typename Searcher>
void benchmark_64_hybrid_pgm_lipp(tli::Benchmark<uint64_t>& benchmark,
                                   bool pareto,
                                   const std::vector<int>& /*params*/) {
  if (!pareto) {
    util::fail("HybridPGMLIPP hyperparameters cannot be set manually");
  }
  benchmark.template Run<HybridPGMLIPP<uint64_t, Searcher,  32,  100000>>();
  benchmark.template Run<HybridPGMLIPP<uint64_t, Searcher,  64,  100000>>();
  benchmark.template Run<HybridPGMLIPP<uint64_t, Searcher, 128,  100000>>();
  benchmark.template Run<HybridPGMLIPP<uint64_t, Searcher,  64,  500000>>();
  benchmark.template Run<HybridPGMLIPP<uint64_t, Searcher, 128,  500000>>();
  benchmark.template Run<HybridPGMLIPP<uint64_t, Searcher,  64, 1000000>>();
}

// ---------------------------------------------------------------------------
// Filename-based dispatch (default, no --pareto): pick variants per dataset.
// We only target the two mixed workloads used for Milestone 2 / 3.
// ---------------------------------------------------------------------------
template <int record>
void benchmark_64_hybrid_pgm_lipp(tli::Benchmark<uint64_t>& benchmark,
                                   const std::string& filename) {
  // Only run on "mix" workloads (insert+lookup interleaved).
  if (filename.find("mix") == std::string::npos) return;

  if (filename.find("fb_100M") != std::string::npos ||
      filename.find("books_100M") != std::string::npos ||
      filename.find("osmc_100M") != std::string::npos) {
    if (filename.find("0.900000i") != std::string::npos) {
      // Insert-heavy: flush frequently so DPGM stays small.
      benchmark.template Run<HybridPGMLIPP<uint64_t, BranchingBinarySearch<record>,  64, 100000>>();
      benchmark.template Run<HybridPGMLIPP<uint64_t, BranchingBinarySearch<record>, 128, 100000>>();
      benchmark.template Run<HybridPGMLIPP<uint64_t, BranchingBinarySearch<record>,  64, 500000>>();
    } else if (filename.find("0.100000i") != std::string::npos) {
      // Lookup-heavy: fewer inserts, less benefit to flushing often.
      benchmark.template Run<HybridPGMLIPP<uint64_t, BranchingBinarySearch<record>,  64, 100000>>();
      benchmark.template Run<HybridPGMLIPP<uint64_t, BranchingBinarySearch<record>, 128, 100000>>();
      benchmark.template Run<HybridPGMLIPP<uint64_t, BranchingBinarySearch<record>,  64, 500000>>();
    }
  }
}

INSTANTIATE_TEMPLATES_MULTITHREAD(benchmark_64_hybrid_pgm_lipp, uint64_t);
