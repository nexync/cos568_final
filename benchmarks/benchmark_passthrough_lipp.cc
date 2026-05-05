#include "benchmarks/benchmark_passthrough_lipp.h"

#include "benchmark.h"
#include "benchmarks/common.h"
#include "competitors/passthrough_lipp.h"

template <typename Searcher>
void benchmark_64_passthrough_lipp(tli::Benchmark<uint64_t>& benchmark,
                                    bool /*pareto*/,
                                    const std::vector<int>& /*params*/) {
  benchmark.template Run<PassthroughLIPP<uint64_t, Searcher>>();
}

template <int record>
void benchmark_64_passthrough_lipp(tli::Benchmark<uint64_t>& benchmark,
                                    const std::string& filename) {
  if (filename.find("mix") == std::string::npos) return;
  benchmark.template Run<PassthroughLIPP<uint64_t, BranchingBinarySearch<record>>>();
}

INSTANTIATE_TEMPLATES_MULTITHREAD(benchmark_64_passthrough_lipp, uint64_t);
