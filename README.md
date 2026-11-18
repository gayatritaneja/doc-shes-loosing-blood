# Hospital scheduler

CS-3710 Operating Systems, Assignment 1 (Process Management).
Scenario: **Emergency Hospital Management**. Patients are processes and the
two doctors are two identical parallel service units.

<!-- TODO (teammates, spec Section 20): group member names, process model,
     per-algorithm descriptions, design decisions and assumptions, metric
     definitions. The sections below cover compile/run, testing, comparison,
     optional features and the limitations that belong to them. -->

Implemented algorithms:

- `FCFS`: first-come, first-served (non-preemptive)
- `PRIORITY`: non-preemptive priority scheduling
- `PRIORITY_PREEMPTIVE`: preemptive priority scheduling

## Project layout

```text
src/        C sources (main, io, three schedulers, stats, gantt, compare)
tests/      regression tests (inputs, expected, invalid, run_tests.sh)
analysis/   algorithm comparison (compare.sh, plot.py, results, graphs)
```

## Compile

From the project directory:

```sh
gcc -std=c11 -Wall -Wextra -Wpedantic -Werror src/*.c -o scheduler
```

`cc` or `clang` work as well. `src/*.c` picks up every source file, so
nothing needs editing when a file is added.

## Run

```sh
./scheduler INPUT_FILE ALGORITHM [--gantt] [--stats]
```

| Argument | Meaning |
|---|---|
| `INPUT_FILE` | input file in the format below |
| `ALGORITHM` | `FCFS`, `PRIORITY`, `PRIORITY_PREEMPTIVE`, `COMPARE` or `COMPARE_CSV` |
| `--gantt` | (optional) print a per-doctor timeline to **stderr** |
| `--stats` | (optional) print preemption/dispatch counts to **stderr** |

```sh
./scheduler tests/inputs/sample_hw.txt FCFS
./scheduler tests/inputs/sample_hw.txt PRIORITY_PREEMPTIVE --gantt --stats
./scheduler tests/inputs/sample_hw.txt COMPARE
```

stdout always stays in the machine-readable format below. Extra output goes
to stderr.

## Input format

```text
N
Q
ID,ARRIVAL,PRIORITY,SERVICE,CATEGORY
...
```

`N` is the patient count and `Q` the Round Robin quantum (read but unused by
these algorithms). Lower priority numbers are served first. Blank lines and
lines starting with `#` are ignored. Inputs are rejected (non-zero exit, nothing
on stdout) for: a count that does not match the records, a missing or
non-numeric field, a negative number, a zero service time, an ID of 32
characters or more, and a duplicate ID.

## Output format

One row per patient in lexicographic ID order, then a summary block:

```text
PROCESS_ID,ARRIVAL,PRIORITY,SERVICE,START,COMPLETION,WAITING,TURNAROUND,RESPONSE
...
SUMMARY
AVERAGE_WAITING,...
AVERAGE_TURNAROUND,...
AVERAGE_RESPONSE,...
THROUGHPUT,...
UTILIZATION,...
```

`UTILIZATION` is a percentage without the `%` sign.

## Testing

```sh
sh tests/run_tests.sh
```

The script compiles the program, then runs four groups of checks (47 in total):

1. **Valid inputs:** each of the 12 files in `tests/inputs/` is run with all
   three algorithms and stdout is compared with `tests/expected/<case>_<ALGO>.out`.
2. **Invalid inputs:** each file in `tests/invalid/` must exit non-zero with
   empty stdout, as must an unknown algorithm name and a missing file.
3. **Flags:** `--gantt --stats` must not change stdout.
4. **Compare mode:** `COMPARE_CSV` must output a header and one row per algorithm.

Every run has a 10 second timeout so a scheduler stuck in a loop fails instead
of hanging the suite.

| Input | Edge case (spec Section 17) |
|---|---|
| `single` | one process |
| `same_time` | all processes arrive at time 0 |
| `arrival_during_run` | processes arriving while another runs |
| `equal_priority` | equal priorities (must not preempt) |
| `identical` | identical arrival, priority and service |
| `idle_gap` | long idle period (arrival at time 1000) |
| `long_service` | very long job (50000 units) next to very short ones |
| `preempt_on_arrival_tick` | preemption at the exact tick of an arrival |
| `completion_equals_arrival` | a completion and an arrival at the same time |
| `lexicographic_ids` | `P10` vs `P2` ordering |
| `sample_hw`, `spec_sample` | mixed workloads; spec Section 13.4 sample |

**How the expected files were produced.** They were not copied from the C
program. `tests/reference_sim.py` is an independent model that advances one time
unit at a time (no event jumping, no heap) and follows spec Sections 7 to 10
literally. Several cases were also traced by hand and matched. To check the
suite detects real bugs, three defects were planted in a scratch copy
(equal priorities preempting, FCFS ties broken by the larger ID, duplicate IDs
accepted) and each was reported as a failure.

To add a case: put `name.txt` in `tests/inputs/`, run
`python3 tests/reference_sim.py tests/inputs/name.txt ALGO > tests/expected/name_ALGO.out`
for each algorithm, and **check the result by hand** before committing it.

## Algorithm comparison

Run on the `sample_hw` input (8 patients) and the specification sample
`spec_sample` (5 patients). Reproduce with
`./scheduler tests/inputs/sample_hw.txt COMPARE`.

**sample_hw**

| Algorithm | AvgWait | AvgTAT | AvgResp | Throughput | Util % | Preempt | Dispatch | Max queue | Longest wait |
|---|---|---|---|---|---|---|---|---|---|
| FCFS | 2.38 | 5.50 | 2.38 | 0.4706 | 73.53 | 0 | 8 | 4 | 6 (P4) |
| PRIORITY | 2.12 | 5.25 | 2.12 | 0.4706 | 73.53 | 0 | 8 | 4 | 6 (P6) |
| PRIORITY_PREEMPTIVE | 1.75 | 4.88 | 1.12 | 0.4706 | 73.53 | 2 | 10 | 4 | 6 (P6) |

**spec_sample**

| Algorithm | AvgWait | AvgTAT | AvgResp | Throughput | Util % | Preempt | Dispatch | Max queue | Longest wait |
|---|---|---|---|---|---|---|---|---|---|
| FCFS | 5.00 | 13.40 | 5.00 | 0.2273 | 95.45 | 0 | 5 | 3 | 11 (P5) |
| PRIORITY | 5.40 | 13.80 | 5.40 | 0.2273 | 95.45 | 0 | 5 | 3 | 14 (P4) |
| PRIORITY_PREEMPTIVE | 6.40 | 14.80 | 2.60 | 0.2273 | 95.45 | 2 | 7 | 3 | 13 (P4) |

Graphs over more inputs are in `analysis/graphs/` (regenerate with
`sh analysis/compare.sh && python3 analysis/plot.py`):

![Average waiting time](analysis/graphs/avg_waiting.png)
![Average response time](analysis/graphs/avg_response.png)
![Preemptions](analysis/graphs/preemptions.png)

### Observations

- **Response time is where preemption pays off.** On `sample_hw` the average
  response drops from 2.38 (FCFS) to 1.12, and on `spec_sample` from 5.00 to
  2.60, because a critical patient starts the moment they arrive rather than
  waiting for a doctor to finish. In `preempt_on_arrival_tick` the preemptive
  average response is 0.00 against 2.00 for the other two.
- **Preemption does not always reduce average waiting.** It does on
  `sample_hw` (1.75 against 2.38), but on `spec_sample` it is the worst (6.40
  against 5.00 for FCFS). Two long critical jobs (P3 and P5) displace P1 and
  P2, and the extra time is paid for by the low-priority patients P1 and P4.
  Priority scheduling optimises who is served first, not the average.
- **Throughput and utilization usually match, and can differ.** All three
  algorithms keep a doctor busy whenever someone is waiting, so on 10 of 12
  inputs the last completion time is the same and so are throughput and
  utilization. `same_time` is the exception: FCFS finishes at time 8 (throughput
  0.5000, 93.75 %), but the priority algorithms serve the 4-unit job P1 last and
  finish at 9 (0.4444, 83.33 %). Priority order does not minimise makespan.
- **Preemption has a cost.** `PRIORITY_PREEMPTIVE` needs more dispatches than
  patients (10 against 8 on `sample_hw`) because interrupted patients are
  dispatched again. Each is a context switch that a real system would pay for.
  Here switching is free.
- **Starvation and fairness.** The longest waits are always priority-3
  patients: P6 (6 units) on `sample_hw`, and P4 (13 to 14 units) on
  `spec_sample`, where it waits behind both priority-1 and priority-2 work.
  Both algorithms always serve the best-priority patient first, so if urgent
  patients kept arriving a low-priority patient could wait without bound. FCFS
  cannot starve anyone but ignores urgency: in `sample_hw` the Critical patient
  P4 waits 6 units behind lower-priority arrivals. Aging (raising the priority
  of long-waiting patients) is the standard fix and is not implemented.
- **Long jobs.** In `long_service`, a 50000-unit job occupies one doctor under
  every algorithm, so the other doctor serves everyone else and the three
  algorithms behave almost identically. Preemption only matters when the
  doctors are both busy.

## Optional features (spec Section 18)

### Gantt chart

`--gantt` prints a timeline per doctor to stderr. Intervals are half-open:
`[3,5)` means busy from time 3 up to 5.

```text
$ ./scheduler tests/inputs/sample_hw.txt PRIORITY_PREEMPTIVE --gantt
GANTT
D1 | P1 [0,1) | P3 [1,2) | P4 [2,4) | P3 [4,7) | P5 [7,10) | P6 [10,12) | P7 [12,13) | idle [13,15) | P8 [15,17)
D2 | idle [0,1) | P2 [1,4) | P1 [4,11)
```

P1 is preempted at time 1 and P3 at time 2, which is visible as P1 and P3
appearing twice.

### Context-switch counting and scheduler statistics

`--stats` prints to stderr:

```text
PREEMPTIONS,2        times a running patient was pushed back to READY
DISPATCHES,10        times a patient was placed on a doctor
MAX_READY_QUEUE,4    longest READY queue just before a dispatch
DOCTOR1_BUSY,15      time units each doctor spent treating patients
DOCTOR2_BUSY,10
```

### Interactive comparison

`COMPARE` runs all three algorithms on one input and prints the spec metrics
next to the statistics above, plus the longest wait and who had it.
`COMPARE_CSV` prints the same data as CSV and feeds `analysis/`.

### Comparison graphs

`analysis/compare.sh` runs `COMPARE_CSV` on every test input and writes
`analysis/results/summary.csv`. `analysis/plot.py` (needs `matplotlib`) draws
the grouped bar charts in `analysis/graphs/`.

## Known limitations

- Aging is not implemented, so starvation of low-priority patients is possible
  (see the observations above).
- The input parser accepts a priority of 0 and a quantum of 0, although the
  specification calls both positive. Valid inputs are unaffected.
- `MAX_READY_QUEUE` is sampled just before each dispatch. For the
  non-preemptive algorithms this is the true maximum; for the preemptive one it
  is measured after arrivals are admitted at each event time.
- The Gantt chart records at most 8192 segments and notes when it truncates.
