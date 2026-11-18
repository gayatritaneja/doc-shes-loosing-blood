#!/bin/sh
# Regression tests for the hospital scheduler.
# Usage (from anywhere):  sh tests/run_tests.sh
#
#  1. valid inputs   : stdout must equal tests/expected/<case>_<ALGO>.out
#  2. invalid inputs : program must exit non-zero and print nothing to stdout
#  3. extra flags    : --gantt/--stats must not change stdout
#  4. COMPARE_CSV    : must run and produce one row per algorithm

cd "$(dirname "$0")/.." || exit 1

BIN="$(mktemp -d)/scheduler"
OUT="$(mktemp)"
ALGOS="FCFS PRIORITY PRIORITY_PREEMPTIVE"
pass=0
fail=0

# Kill any run that hangs (an infinite loop in a scheduler must fail, not stall).
if command -v timeout > /dev/null 2>&1; then
    RUN="timeout 10"
else
    RUN=""
fi

ok()  { pass=$((pass + 1)); }
bad() { fail=$((fail + 1)); echo "FAIL: $1"; }

cc -std=c11 -Wall -Wextra -Wpedantic -Werror src/*.c -o "$BIN" || exit 1

# 1. valid inputs against hand-checked expected output
for input in tests/inputs/*.txt; do
    name=$(basename "$input" .txt)
    for algo in $ALGOS; do
        expected="tests/expected/${name}_${algo}.out"
        if [ ! -f "$expected" ]; then
            bad "$name $algo (missing $expected)"
            continue
        fi
        $RUN "$BIN" "$input" "$algo" > "$OUT" 2>/dev/null
        if [ $? -eq 0 ] && diff -q "$OUT" "$expected" > /dev/null; then
            ok
        else
            bad "$name $algo"
        fi
    done
done

# 2. invalid inputs must be rejected
for input in tests/invalid/*.txt; do
    name=$(basename "$input" .txt)
    $RUN "$BIN" "$input" FCFS > "$OUT" 2>/dev/null
    status=$?
    if [ $status -ne 0 ] && [ ! -s "$OUT" ]; then
        ok
    else
        bad "invalid/$name (exit $status, stdout $(wc -c < "$OUT") bytes)"
    fi
done

# unknown algorithm and missing file are errors too
"$BIN" tests/inputs/single.txt NOPE > /dev/null 2>&1 && bad "unknown algorithm accepted" || ok
"$BIN" tests/inputs/does_not_exist.txt FCFS > /dev/null 2>&1 && bad "missing file accepted" || ok

# 3. extra flags must leave stdout untouched
"$BIN" tests/inputs/sample_hw.txt PRIORITY_PREEMPTIVE --gantt --stats \
    > "$OUT" 2>/dev/null
if diff -q "$OUT" tests/expected/sample_hw_PRIORITY_PREEMPTIVE.out > /dev/null; then
    ok
else
    bad "--gantt/--stats changed stdout"
fi

# 4. comparison mode: header + one row per algorithm
rows=$("$BIN" tests/inputs/sample_hw.txt COMPARE_CSV 2>/dev/null | wc -l)
if [ "$rows" -eq 4 ]; then ok; else bad "COMPARE_CSV produced $rows lines"; fi

rm -f "$OUT"
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
