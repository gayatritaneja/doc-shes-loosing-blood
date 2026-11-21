#!/bin/sh
# runs every scheduler with every memory allocator (3 x 3 = 9 runs) on
# each assignment 2 input and saves one csv per input.
# usage (from anywhere):  sh analysis/compare_memory.sh
# needs ./scheduler to be built first (see the readme).

cd "$(dirname "$0")/.." || exit 1

BIN=./scheduler
[ -x "$BIN" ] || BIN=./scheduler.exe
mkdir -p analysis/results

for input in tests/a2/*.txt; do
    name=$(basename "$input" .txt)
    out="analysis/results/memory_${name}.csv"
    "$BIN" "$input" COMPARE_MEMORY_CSV > "$out" || exit 1
    echo "wrote $out"
done
