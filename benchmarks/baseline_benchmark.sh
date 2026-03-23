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

# 1 million keys, 16-byte keys, 100-byte values
# Matches your project proposal spec exactly
COMMON="--num=1000000 --key_size=16 --value_size=100"

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
echo "Your key baseline number (seekrandom):"
grep "seekrandom" "$RESULTS/seekrandom.txt" | tail -1
echo ""
echo "Save this. Compare after adding SuRF."
echo "============================================"
