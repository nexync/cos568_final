#!/usr/bin/env bash
# Run mixed-workload benchmarks for DynamicPGM, LIPP, HybridPGMLIPP (M2 naive),
# and HybridPGMLIPPAsync (M3 async double-buffered). Run from project root.
# Submit via sbatch on Adroit/Della — do NOT run on login nodes.
#
# Usage:
#   bash scripts/run_hybrid_async_benchmark.sh
#
# To run a subset:
#   DATASETS="fb_100M_public_uint64" bash scripts/run_hybrid_async_benchmark.sh
#   INDEXES="HybridPGMLIPPAsync"   bash scripts/run_hybrid_async_benchmark.sh

set -e

if [ ! -f CMakeLists.txt ]; then
    echo "Error: run this script from the project root directory."
    exit 1
fi

BENCHMARK=build/benchmark
if [ ! -f "$BENCHMARK" ]; then
    echo "Error: benchmark binary not found at $BENCHMARK — run scripts/build_benchmark.sh first."
    exit 1
fi

mkdir -p results

REPEATS=3

: "${DATASETS:=fb_100M_public_uint64 books_100M_public_uint64 osmc_100M_public_uint64}"
: "${INDEXES:=DynamicPGM LIPP HybridPGMLIPP HybridPGMLIPPAsync}"

for DATA in $DATASETS; do
    for INDEX in $INDEXES; do
        echo "==> $DATA  |  $INDEX"

        OPS_90="./data/${DATA}_ops_2M_0.000000rq_0.500000nl_0.900000i_0m_mix"
        if [ -f "$OPS_90" ]; then
            echo "    90% insert mix"
            $BENCHMARK ./data/$DATA "$OPS_90" \
                --through --csv --only "$INDEX" -r "$REPEATS"
        else
            echo "    WARNING: workload not found: $OPS_90"
        fi

        OPS_10="./data/${DATA}_ops_2M_0.000000rq_0.500000nl_0.100000i_0m_mix"
        if [ -f "$OPS_10" ]; then
            echo "    10% insert mix"
            $BENCHMARK ./data/$DATA "$OPS_10" \
                --through --csv --only "$INDEX" -r "$REPEATS"
        else
            echo "    WARNING: workload not found: $OPS_10"
        fi
    done
done

echo ""
echo "=== Adding CSV headers ==="
for FILE in results/*.csv; do
    [ -f "$FILE" ] || continue
    [[ "$FILE" == *mix* ]] || continue
    if head -n 1 "$FILE" | grep -q "index_name"; then
        sed -i '1d' "$FILE"
    fi
    sed -i '1s/^/index_name,build_time_ns1,build_time_ns2,build_time_ns3,index_size_bytes,mixed_throughput_mops1,mixed_throughput_mops2,mixed_throughput_mops3,search_method,value\n/' "$FILE"
    echo "  $FILE"
done

echo ""
echo "Done. Results in results/"
echo "To regenerate plots:"
echo "  python3 scripts/plot_hybrid.py --results results --out figures"
