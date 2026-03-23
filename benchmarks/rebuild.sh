#!/bin/bash
# rebuild.sh — run inside the container after every code change
# Usage: bash /workspace/benchmarks/rebuild.sh

set -e

echo ""
echo "============================================"
echo "  LevelDB + SuRF — Rebuild"
echo "============================================"

# STEP 1: Copy your files into LevelDB source tree
# Your files live at /workspace/project/ (volume mount from Windows laptop)
# LevelDB CMakeLists.txt looks for files in its own directories,
# so we copy your work there before building.

echo ""
echo "Step 1: Copying your files into LevelDB..."

if [ -f /workspace/project/surf_filter.h ]; then
    cp /workspace/project/surf_filter.h /workspace/leveldb/include/leveldb/surf_filter.h
    echo "  OK: surf_filter.h -> include/leveldb/"
fi

if [ -f /workspace/project/surf_filter.cc ]; then
    cp /workspace/project/surf_filter.cc /workspace/leveldb/util/surf_filter.cc
    echo "  OK: surf_filter.cc -> util/"
fi

if [ -f /workspace/project/filter_policy.h ]; then
    cp /workspace/project/filter_policy.h /workspace/leveldb/include/leveldb/filter_policy.h
    echo "  OK: filter_policy.h -> include/leveldb/"
fi

if [ -f /workspace/project/filter_block.h ]; then
    cp /workspace/project/filter_block.h /workspace/leveldb/table/filter_block.h
    echo "  OK: filter_block.h -> table/"
fi

if [ -f /workspace/project/filter_block.cc ]; then
    cp /workspace/project/filter_block.cc /workspace/leveldb/table/filter_block.cc
    echo "  OK: filter_block.cc -> table/"
fi

if [ -f /workspace/project/table.cc ]; then
    cp /workspace/project/table.cc /workspace/leveldb/table/table.cc
    echo "  OK: table.cc -> table/"
fi

if [ -f /workspace/project/two_level_iterator.cc ]; then
    cp /workspace/project/two_level_iterator.cc /workspace/leveldb/table/two_level_iterator.cc
    echo "  OK: two_level_iterator.cc -> table/"
fi

if [ -f /workspace/project/table_cache.cc ]; then
    cp /workspace/project/table_cache.cc /workspace/leveldb/db/table_cache.cc
    echo "  OK: table_cache.cc -> db/"
fi

if [ -f /workspace/project/version_set.cc ]; then
    cp /workspace/project/version_set.cc /workspace/leveldb/db/version_set.cc
    echo "  OK: version_set.cc -> db/"
fi

# STEP 2: Incremental rebuild
# cmake only recompiles files that changed. Usually 10-30 seconds.

echo ""
echo "Step 2: Building..."
cd /workspace/leveldb/build
cmake --build . --parallel 4
echo "  OK: Build complete"

# STEP 3: Run full test suite
# All original LevelDB tests must keep passing.
# Any failure means your change broke existing behavior.

echo ""
echo "Step 3: Running tests..."
./leveldb_tests

echo ""
echo "============================================"
echo "  All tests passed."
echo "============================================"
echo ""
echo "To benchmark:"
echo "  cd /workspace/leveldb/build"
echo "  ./db_bench --benchmarks=seekrandom --num=1000000"
