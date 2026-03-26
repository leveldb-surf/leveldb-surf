#!/bin/bash
# baseline_benchmark.sh
# Run ONCE before writing any code to capture your "before" numbers.
# Usage: bash /workspace/benchmarks/baseline_benchmark.sh

set -e

BENCH=/workspace/leveldb/build/db_bench
RESULTS=/workspace/benchmarks/baseline
mkdir -p "$RESULTS"

echo ""
echo "============================================"
echo "  Baseline Benchmark - Unmodified LevelDB"
echo "  Results -> $RESULTS/"
echo "============================================"

# 1 million keys, 100-byte values
# --key_size is not supported in this version of db_bench
# key size defaults to 16 bytes internally
COMMON="--num=1000000 --value_size=100"

echo ""
echo "Test 1: Sequential read..."
$BENCH $COMMON --benchmarks=fillseq,readseq 2>&1 | tee "$RESULTS/readseq.txt"

echo ""
echo "Test 2: Random point read..."
$BENCH $COMMON --benchmarks=fillseq,readrandom 2>&1 | tee "$RESULTS/readrandom.txt"

echo ""
echo "Test 3: Seek random (most relevant - closest to range scan)..."
$BENCH $COMMON --benchmarks=fillseq,seekrandom 2>&1 | tee "$RESULTS/seekrandom.txt"

echo ""
echo "Test 4: Write throughput..."
$BENCH $COMMON --benchmarks=fillrandom 2>&1 | tee "$RESULTS/fillrandom.txt"

echo ""
echo "============================================"
echo "  Baseline complete."
echo ""
echo "Your key baseline numbers:"
echo -n "  seekrandom : " && grep "seekrandom" "$RESULTS/seekrandom.txt" | tail -1
echo -n "  readrandom : " && grep "readrandom" "$RESULTS/readrandom.txt" | tail -1
echo -n "  readseq    : " && grep "readseq"    "$RESULTS/readseq.txt"    | tail -1
echo -n "  fillrandom : " && grep "fillrandom" "$RESULTS/fillrandom.txt" | tail -1
echo ""
echo "Save these. Compare after adding SuRF."
echo "============================================"