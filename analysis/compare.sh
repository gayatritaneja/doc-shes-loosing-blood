#!/bin/sh
# Run every algorithm on every test input and collect one CSV.
# Usage (from anywhere):  sh analysis/compare.sh
# Output: analysis/results/summary.csv

cd "$(dirname "$0")/.." || exit 1

BIN="$(mktemp -d)/scheduler"
CSV=analysis/results/summary.csv

cc -std=c11 -Wall -Wextra -Wpedantic -Werror src/*.c -o "$BIN" || exit 1
mkdir -p analysis/results

first=1
for input in tests/inputs/*.txt; do
    name=$(basename "$input" .txt)
    rows=$("$BIN" "$input" COMPARE_CSV) || { echo "failed on $name" >&2; exit 1; }

    if [ $first -eq 1 ]; then
        # header row, with an extra leading column for the input name
        echo "$rows" | head -n 1 | sed 's/^/input,/' > "$CSV"
        first=0
    fi
    echo "$rows" | tail -n +2 | sed "s/^/$name,/" >> "$CSV"
done

echo "wrote $CSV ($(($(wc -l < "$CSV") - 1)) rows)"
