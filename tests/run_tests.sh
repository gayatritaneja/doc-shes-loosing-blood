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

cc -std=c11 -Wall -Wextra -Wpedantic -Werror -pthread src/*.c -o "$BIN" || exit 1

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

# 5. concurrency: real workers, deadlock-prone input, bounded demo
for algo in $ALGOS; do
    for input in tests/concurrency/*.txt; do
        $RUN "$BIN" "$input" "$algo" > "$OUT" 2>/dev/null
        if [ $? -eq 0 ] &&
           grep -q '^CHECK,EXCLUSIVITY_VIOLATIONS,0$' "$OUT" &&
           grep -q '^CHECK,UNRESOLVED_DEADLOCKS,0$' "$OUT" &&
           grep -q "^CHECK,COMPLETED_COUNT,$(grep -c '^PROCESS,' "$OUT")\$" "$OUT"; then
            ok
        else
            bad "concurrency $(basename "$input") $algo"
        fi
    done
done
$RUN "$BIN" --deadlock-demo > "$OUT" 2>&1 && grep -q '^DEMO PASSED' "$OUT" \
    && ok || bad "deadlock demo"

# 6. resource-free baseline: --final keeps the same schedule as the
#    resource-free output, appends RESOURCE_WAIT=0, emits a POOL record
#    and no RESOURCE records
for input in tests/inputs/*.txt; do
    name=$(basename "$input" .txt)
    for algo in $ALGOS; do
        $RUN "$BIN" "$input" "$algo" --final > "$OUT" 2>/dev/null
        want=$(sed -n '2,/^SUMMARY$/p' "tests/expected/${name}_${algo}.out" |
               grep -v '^SUMMARY$')
        got=$(grep '^PROCESS,' "$OUT" | cut -d, -f2-10)
        if [ "$want" = "$got" ] &&
           [ -z "$(grep '^PROCESS,' "$OUT" | cut -d, -f14 | grep -v '^0$')" ] &&
           grep -q '^POOL,SPACE,6,' "$OUT" &&
           ! grep -q '^RESOURCE,' "$OUT"; then
            ok
        else
            bad "baseline --final $name $algo"
        fi
    done
done

# 7. reproducible examples (virtual-time results are deterministic even
#    though the worker thread that runs each operation is not)
check_example() {   # expected-file, then the command line
    expected="tests/examples/expected/$1"; shift
    $RUN "$BIN" "$@" > "$OUT" 2>/dev/null
    if diff -q "$OUT" "$expected" > /dev/null; then ok; else bad "example $expected"; fi
}
for fit in FIRST_FIT BEST_FIT WORST_FIT; do
    check_example "memory_policies_FCFS_$fit.out" \
        tests/examples/memory_policies.txt FCFS --final --memory "$fit"
done
check_example integration_FCFS.out tests/examples/integration.txt FCFS
check_example nurses_parallel_PRIORITY_PREEMPTIVE.out \
    tests/examples/nurses_parallel.txt PRIORITY_PREEMPTIVE
check_example deadlock_reversed_FCFS.out \
    tests/concurrency/deadlock_reversed.txt FCFS

# 8. bounded unsafe demo: prevention off -> cycle reported, exit 2, no hang
$RUN "$BIN" tests/concurrency/deadlock_reversed.txt FCFS --unsafe \
    > "$OUT" 2>/dev/null
status=$?
if [ $status -eq 2 ] &&
   diff -q "$OUT" tests/examples/expected/deadlock_reversed_FCFS_unsafe.out \
       > /dev/null; then
    ok
else
    bad "--unsafe deadlock demo (exit $status)"
fi

# 9. assignment 2: every scheduler x every allocator, using the a2 command
#    INPUT SCHEDULER ALLOCATOR OUTPUT_FILE. the file must match the expected
#    a2 output and nothing may be printed to stdout.
EXPORT="$(mktemp)"
for input in tests/a2/*.txt; do
    name=$(basename "$input" .txt)
    for algo in $ALGOS; do
        for fit in FIRST_FIT BEST_FIT WORST_FIT; do
            $RUN "$BIN" "$input" "$algo" "$fit" "$EXPORT" > "$OUT" 2>/dev/null
            if [ $? -eq 0 ] && [ ! -s "$OUT" ] &&
               diff -q "$EXPORT" "tests/a2/expected/${name}_${algo}_${fit}.out" \
                   > /dev/null; then
                ok
            else
                bad "a2 $name $algo $fit"
            fi
        done
    done
done
rm -f "$EXPORT"

# 10. a2 command without an output file prints the same thing to stdout
$RUN "$BIN" tests/a2/hospital_mix.txt PRIORITY BEST_FIT > "$OUT" 2>/dev/null
if diff -q "$OUT" tests/a2/expected/hospital_mix_PRIORITY_BEST_FIT.out \
       > /dev/null; then ok; else bad "a2 command to stdout"; fi

# 11. same input + scheduler + allocator gives the same output every time
$RUN "$BIN" tests/a2/hospital_mix.txt PRIORITY_PREEMPTIVE WORST_FIT > "$OUT"
same=1
for i in 1 2 3 4 5; do
    $RUN "$BIN" tests/a2/hospital_mix.txt PRIORITY_PREEMPTIVE WORST_FIT |
        diff -q - "$OUT" > /dev/null || same=0
done
if [ $same -eq 1 ]; then ok; else bad "a2 output changes between runs"; fi

# 12. COMPARE_MEMORY_CSV: header + 9 rows (3 schedulers x 3 allocators)
rows=$("$BIN" tests/a2/hospital_mix.txt COMPARE_MEMORY_CSV 2>/dev/null | wc -l)
if [ "$rows" -eq 10 ]; then ok; else bad "COMPARE_MEMORY_CSV produced $rows lines"; fi

# 13. unknown allocator name is an error
"$BIN" tests/a2/hospital_mix.txt FCFS NEXT_FIT > /dev/null 2>&1 \
    && bad "unknown allocator accepted" || ok

rm -f "$OUT"
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
