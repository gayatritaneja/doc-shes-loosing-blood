# Hospital scheduler

CS-3710 Operating Systems, Assignment 1 (Process Management).
Scenario: **Emergency Hospital Management**. Patients are processes and the
two doctors are two identical parallel service units.

Group: **doc shes losing blood**

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
gcc -std=c11 -Wall -Wextra -Wpedantic -Werror -pthread src/*.c -o scheduler
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

---

# final project: concurrency, deadlock and integration

everything above is from assignment 1. this part covers the final project: shared resources, real threads, deadlock and how it all fits together with scheduling (a1) and memory (a2).

## 1. the scenario

| in the hospital | in os terms |
|---|---|
| patient | process |
| 2 doctors | 2 cpus (execution units) |
| 6 slots of space | memory (contiguous) |
| `XRAY`, `MRI`, `NURSE_1`-`NURSE_3`, `ROOM_1`, `ROOM_2` | shared resources, only 1 patient can hold each one at a time |
| staff who actually do the xray/mri work | 2 real worker threads (pthreads) |
| front desk that runs everything | 1 coordinator (the main thread) |

a patient goes through these states:

```text
NEW -> BLOCKED_MEMORY -> READY -> RUNNING -> TERMINATED
                           ^         |
                           |         v
                         BLOCKED_RESOURCE
```

1. the patient arrives and joins the memory line (fifo, nobody skips the line).
2. it only becomes READY once it gets all the memory it asked for.
3. the scheduler (FCFS / PRIORITY / PRIORITY_PREEMPTIVE) gives it a doctor.
4. while it's running, it may ask for a resource (like the xray). if the xray is busy, the patient becomes BLOCKED_RESOURCE. it gives the doctor back but keeps its memory. waiting doesn't count as service time.
5. when the xray is free again, the patient goes back to READY and has to wait for a doctor again.
6. if a patient gets preempted it keeps the resources it holds. when it finishes, its memory is freed. every resource it took must have been given back by then.

---

## 2. how the code is split up

```text
io.c                read the input file and check it
engine.c            the main loop: virtual clock, scheduler, memory, calls the workers
fcfs.c, priority.c, priority_preemptive.c
                    the rule for "who goes next"
memory.c            first fit / best fit / worst fit + the fifo memory line
resource_sim.c      turns "P1 wants XRAY" into a job for a worker thread,
                    blocks / wakes patients, deadlock prevention, deadlock check
workers.c           the 2 worker threads and their job queue
resource_manager.c  the table of who holds which resource (protected by a mutex)
resource_report.c   prints the final output
deadlock_demo.c     the stand-alone deadlock demo with real threads
```

how one time step works in `engine.c`:

1. jump to the next thing that happens (an arrival, someone finishing, or someone reaching a resource action).
2. new arrivals join the memory line.
3. keep doing these until nothing changes:
   - do the resource actions that are due (the worker threads do them)
   - finished patients free their memory
   - give memory to whoever is at the front of the memory line
   - put READY patients in the ready queue
   - give free doctors to the best READY patients (and preempt if needed)
4. only with `--unsafe`: check for a deadlock, stop if there is one.

---

## 3. build and run

linux / mac:

```sh
gcc -std=c11 -Wall -Wextra -Wpedantic -Werror -pthread src/*.c -o scheduler
```

windows: you need a gcc that has pthreads (like winlibs: `winget install BrechtSanders.WinLibs.POSIX.UCRT`). the old mingw doesn't have `pthread.h`. add `-static` so the .exe runs on its own:

```sh
gcc -std=c11 -Wall -Wextra -Wpedantic -Werror -pthread -static src/*.c -o scheduler.exe
```

run it:

```text
./scheduler INPUT ALGORITHM [--memory FIRST_FIT|BEST_FIT|WORST_FIT]
            [--final] [--trace] [--gantt] [--stats] [--unsafe]
./scheduler --deadlock-demo
```

| option | what it does |
|---|---|
| `ALGORITHM` | `FCFS`, `PRIORITY`, `PRIORITY_PREEMPTIVE`, or `COMPARE` / `COMPARE_CSV` |
| `--memory` | which memory algorithm to use, default is `FIRST_FIT` |
| `--final` | print the final project output format (it's automatic if the input has resources) |
| `--trace` | print every event (arrive, memory, doctor, acquire, block, release...) to stderr |
| `--gantt` | print a timeline for each doctor to stderr |
| `--stats` | print preemption / dispatch counts to stderr |
| `--unsafe` | demo only: turns deadlock prevention off (see section 8) |
| `--deadlock-demo` | runs the stand-alone deadlock demo with real threads |

deadlock prevention is always on unless you pass `--unsafe`.

run all the tests (97 checks):

```sh
sh tests/run_tests.sh          # or: CC=gcc sh tests/run_tests.sh
```

---

## 4. input and output files

input (same as assignment 2, plus two optional parts at the end):

```text
N                                         number of patients
Q                                         round robin quantum (read but not used, hospital has no RR)
ID,ARRIVAL,PRIORITY,SERVICE,CATEGORY,SPACE_REQ     one line per patient
R                                         number of resources (optional)
RESOURCE_ID,CAPACITY                      one line per resource
K                                         number of actions
ID,WORK_OFFSET,ACTION,RESOURCE_ID,UNITS   one line per action
```

`WORK_OFFSET` means "after this many units of service". so `P1,4,RELEASE,XRAY,1` means p1 gives back the xray after it has had 4 units of treatment. if there's no R/K part, it's just a normal assignment 2 file.

bad files are rejected with a clear error message, for example: using a resource that wasn't declared, releasing something you don't hold, taking the same thing twice, never giving something back, or holding more than 7 resources at once.

output goes to stdout and messages go to stderr, so saving the result is just:

```sh
./scheduler tests/examples/integration.txt FCFS > results.txt
./scheduler tests/examples/integration.txt FCFS --trace 2> trace.txt > results.txt
```

the output has these lines in this order (no header row):

```text
PROCESS,ID,ARRIVAL,PRIORITY,SERVICE,START,COMPLETION,WAITING,TURNAROUND,RESPONSE,MEM_ADMIT,MEM_WAIT,READY_WAIT,RESOURCE_WAIT
SUMMARY,... (8 lines)
POOL,SPACE,6,PEAK_OCCUPIED,UTILIZATION_PCT,MAX_EXT_FRAG_SLOTS
RESOURCE,RESOURCE_ID,CAPACITY,MAX_SIMULTANEOUS_HOLDERS,ACQUISITIONS,WAIT_EVENTS   (one per resource)
CHECK,EXCLUSIVITY_VIOLATIONS,...
CHECK,UNRESOLVED_DEADLOCKS,...
CHECK,COMPLETED_COUNT,...
```

---

## 5. algorithms and metrics we kept

- scheduling (a1): `FCFS`, `PRIORITY`, `PRIORITY_PREEMPTIVE`. smaller priority number = more urgent. ties go to whoever came first, then the smaller id. preemption only happens for a strictly more urgent patient.
- memory (a2): 6 slots in a row. `FIRST_FIT` takes the first hole that fits, `BEST_FIT` the smallest hole that fits, `WORST_FIT` the biggest hole. the memory line is fifo.

how the numbers are worked out:

- waiting = turnaround - service
- mem_wait = time it got memory - arrival time
- resource_wait = time spent blocked waiting for a resource
- ready_wait = waiting - mem_wait - resource_wait
- so for every patient: completion - arrival - service = mem_wait + ready_wait + resource_wait
- throughput = number of patients / (last finish - first arrival)
- service utilization = total service / (2 doctors x that same time)
- memory utilization = (slots used x how long) / (6 slots x that same time)
- max ext frag = the worst moment of (free slots - biggest free hole)
- `MAX_SIMULTANEOUS_HOLDERS` = most patients holding that resource at once (must be 1)
- `ACQUISITIONS` = how many times it was given out, `WAIT_EVENTS` = how many times someone found it busy
- `EXCLUSIVITY_VIOLATIONS` = how many times the "held + free = capacity" check failed (must be 0)

---

## 6. threads and locks

**threads**

- the main thread is the coordinator. it runs the virtual clock, the scheduler and the memory. it is the only thread that changes a patient's state, so states don't need their own lock.
- there are 2 worker threads. the coordinator puts jobs in a queue ("P1 wants XRAY", "P2 gives back MRI"). a worker picks a job, does it on the resource table, and puts the result in a result queue. the trace shows which worker did it (`W1` or `W2`).
- jobs on different resources (like NURSE_1 and NURSE_2) are sent together, so both workers really run at the same time. jobs on the same resource are sent one after the other in a fixed order. this way the results are always the same even though the threads aren't.

**locks**

| what is shared | what protects it | why it's safe |
|---|---|---|
| the resource table (who holds what) | one mutex | checking "is it free?" and taking it happen together inside the lock, so two patients can never both take it |
| waiting for a resource (in the demo) | condition variable | the thread sleeps until someone releases. we check again after waking up. every release wakes everyone (broadcast), so no wake-up gets lost |
| the job queue and result queue | a mutex + 4 condition variables | normal producer/consumer. workers sleep when there's nothing to do, they never spin in a loop |
| deadlock demo timeout | `pthread_cond_timedwait` | waits at most 300 ms, so the demo always ends |

we never use `sleep()` to make things correct and we never busy-wait.

**virtual time vs real time**

| | virtual time | real time |
|---|---|---|
| who moves it | our coordinator, jumping event to event | the os, switching threads whenever it wants |
| unit | the time units in the input file | actual nanoseconds |
| used for | all the metrics (waiting, throughput...) | nothing we report, only the order things run in |
| same every run? | yes | no (which worker does a job can change) |

a resource action takes 0 virtual time. the coordinator waits for the workers to finish before moving the clock, so the real thread timing can never change a metric. "2 doctors" means at most 2 patients get treated at the same virtual time. that has nothing to do with how many os threads there are, and we don't make one thread per patient.

---

## 7. rules for sharing resources

- each resource (`XRAY`, `MRI`, `NURSE_1`...) can be held by only 1 patient at a time. different copies (like `NURSE_1` and `NURSE_2`) can be used by different patients at the same time.
- you can only release something you actually hold. releasing it twice gives an error (and the input checker catches it before the run starts).
- if the resource is busy, the patient is blocked: it gives the doctor back, keeps its memory, and waits.
- every time something is released, all blocked patients get another try, oldest first. if a patient gets what it needs, it goes to READY (not straight to RUNNING).
- fairness: "oldest first" is best effort, not a promise. a patient who needs 2 things can get overtaken by someone who only needs 1 of them. we don't save resources for anyone.

---

## 8. deadlock

### the 4 conditions for a deadlock, in our hospital

| condition | is it here? | example |
|---|---|---|
| mutual exclusion | yes, always | only one patient on the xray at a time |
| hold and wait | yes, if we don't prevent it | a patient keeps the mri while waiting for the xray |
| no preemption | yes for resources | we can take a doctor away, but never an xray someone is holding |
| circular wait | it can happen | p1 has the xray and wants the mri, p2 has the mri and wants the xray |

all 4 have to be true for a deadlock. we break **hold and wait**.

### showing the deadlock (with prevention off)

the input `tests/concurrency/deadlock_reversed.txt` has p1 asking for XRAY then MRI and p2 asking for MRI then XRAY.

```text
$ ./scheduler tests/concurrency/deadlock_reversed.txt FCFS --trace --unsafe
TRACE t=0    W2 ACQUIRE          P1     XRAY
TRACE t=0    W2 ACQUIRE          P2     MRI
TRACE t=1    W2 BLOCK            P1     MRI
TRACE t=1    W1 BLOCK            P2     XRAY
TRACE t=1    CO DEADLOCK         P1     MRI
TRACE t=1    CO DEADLOCK         P2     XRAY
DEADLOCK,DETECTED_AT,1
DEADLOCK,WAIT_FOR,P1,MRI,P2
DEADLOCK,WAIT_FOR,P2,XRAY,P1
CHECK,EXCLUSIVITY_VIOLATIONS,0
CHECK,UNRESOLVED_DEADLOCKS,1
CHECK,COMPLETED_COUNT,0
```

how we find it: every blocked patient is waiting for something that someone else holds, so "p1 waits for p2" is an arrow. after every event we follow the arrows. if we end up back where we started, that's a loop, so it's a deadlock. we print the loop and stop (exit code 2). so the demo can never hang.

there's also `./scheduler --deadlock-demo`, which does the same thing with 2 real threads that lock in opposite orders. both get stuck, a 300 ms timeout notices it, prints it and lets go. then it runs again with prevention on and both finish.

### the prevention (always on normally): all or nothing

- if you ask for something and it's free, you get it.
- if it's busy, you **give back everything you're holding** and wait for all of it together (what you asked for + what you had).
- later you get the whole set at once or nothing at all (checked inside one mutex).

so a waiting patient never holds anything. that means hold and wait can't happen, so a loop can't form. same input, prevention on:

```text
$ ./scheduler tests/concurrency/deadlock_reversed.txt FCFS --trace
TRACE t=1    W2 BLOCK            P1     MRI
TRACE t=1    W2 BLOCK            P2     XRAY
TRACE t=1    W2 ROLLBACK_RELEASE P1     XRAY        <- p1 gives back the xray
TRACE t=1    W2 ROLLBACK_RELEASE P2     MRI         <- p2 gives back the mri
TRACE t=1    W2 UNBLOCK/ACQUIRE  P1     MRI+XRAY    <- p1 gets both at once
TRACE t=5    W2 RELEASE          P1     XRAY
TRACE t=5    W1 UNBLOCK/ACQUIRE  P2     XRAY+MRI    <- then p2 gets both
...
PROCESS,P1,0,1,6,0,6,0,6,0,0,0,0,0
PROCESS,P2,0,1,6,0,10,4,10,0,0,0,0,4
CHECK,UNRESOLVED_DEADLOCKS,0
CHECK,COMPLETED_COUNT,2
```

if the normal run ever got stuck anyway, it would print who is stuck on what and exit. it doesn't hang.

### downsides of this policy

- more waiting: p2 waits 4 units (but that's better than waiting forever).
- a resource that was given back might sit free for a bit, and it gets asked for again (more wait events).
- someone who needs many things at once can keep losing to people who need just one. it works fine in our tests, but there's no guarantee.
- we only give back the resource, not the work. we assume the treatment already done still counts.
- another option was a fixed order (always xray before mri), which has no give-backs. but the input file decides the order, and the spec even reverses it on purpose. all or nothing works no matter what order the input uses.

---

## 9. examples you can run

all of these are checked automatically by `tests/run_tests.sh`.

### (i) no resources = same answer as before

```sh
./scheduler tests/inputs/sample_hw.txt FCFS --final
```

for every file in `tests/inputs/` and every algorithm, the start, finish, waiting, turnaround and response times are exactly the same as before. resource_wait is 0 and there are no RESOURCE lines.

### (ii) memory algorithms give different results: `tests/examples/memory_policies.txt`

at t=5 the free holes are 2 slots (0-1) and 1 slot (3). p5 needs 1 slot and p6 needs 2.

```sh
./scheduler tests/examples/memory_policies.txt FCFS --final --memory BEST_FIT --trace
```

| memory algorithm | where p5 goes | what happens to p6 | avg memory wait | memory utilization |
|---|---|---|---|---|
| FIRST_FIT | slot 0 (breaks up the 2-slot hole) | has to wait for memory from t=5 to t=8 | 0.5 | 71.67% |
| BEST_FIT | slot 3 (exact fit) | gets slots 0-1 straight away | 0 | 81.67% |
| WORST_FIT | slot 0 (biggest hole) | has to wait for memory from t=5 to t=8 | 0.5 | 71.67% |

average waiting (2.1667) and throughput (0.6) are the same for all 3, because the doctors are the real bottleneck. best fit just moves p6's wait from "waiting for memory" to "waiting for a doctor". first and worst fit come out the same here because there are only 2 holes.

### (iii) real threads competing, and it stays safe: `tests/examples/nurses_parallel.txt`

```sh
./scheduler tests/examples/nurses_parallel.txt PRIORITY_PREEMPTIVE --trace --gantt
```

- t=0: p1 takes NURSE_1 and p2 takes NURSE_2 at the same time (different copies, both workers busy).
- t=1: p3 (more urgent) preempts p2. p2 keeps NURSE_2. p3 asks for NURSE_1, but p1 has it, so p3 is blocked and the doctor goes back to p2.
- t=4: p1 gives back NURSE_1, p3 wakes up, gets it, and preempts p2 again.
- NURSE_1 shows `MAX_SIMULTANEOUS_HOLDERS=1` and `WAIT_EVENTS=1`.

and in `./scheduler --deadlock-demo` part 1, 2 threads each take the xray, add 1 to a counter, and give it back, 5000 times each. the counter ends at exactly 10000 (no lost updates), and there are never 2 holders.

### (iv) the deadlock demo

see section 8: `--unsafe` finds the loop at t=1, and without it both patients finish. `contention.txt` still finishes even with `--unsafe`, because everyone asks in the same order there, so no loop can form.

### blocked patients can't work, and releasing lets others go: `tests/examples/integration.txt`

```text
t=1  P2 BLOCK XRAY           p1 has it. p2 gives up doctor 2 but keeps its memory
t=1  P3 MEM_WAIT             memory is full, so p3 can't even be READY
t=4  P1 RELEASE XRAY         -> p2 wakes up and gets doctor 2 back
t=5  P1 COMPLETE             -> frees memory -> p3 gets memory -> p3 gets doctor 1
GANTT
D1 | P1 [0,5) | P3 [5,7)
D2 | P2 [0,1) | idle [1,4) | P2 [4,7)
```

doctor 2 sits idle from 1 to 4 even though p3 is there: p2 is blocked on the xray and p3 has no memory. p2's resource_wait is 3 and p3's mem_wait is 4, and both add up correctly.

### scheduling makes a difference

see "algorithm comparison" above. for example on `sample_hw`, average response is 2.38 with FCFS and 1.12 with PRIORITY_PREEMPTIVE. with resources, the scheduler also decides who gets to ask for the resource first (in the nurses example, p3 jumps ahead because it's critical).

---

## 10. trade-offs

- **waiting vs no deadlock:** all or nothing turns a "stuck forever" into "wait a bit longer" (p2 waits 4). a fixed order would have no give-backs, but every input would have to follow it.
- **throughput vs utilization:** a blocked patient gives its doctor to someone else (good), but keeps its memory, which can block new patients (doctor 2 was idle 1-4). best fit used memory better (81.7% vs 71.7%) but throughput didn't change, because the doctors were the bottleneck.
- **resource use vs safety:** giving resources back means they're used a bit less and asked for more often, but nothing ever gets stuck.
- **fairness:** preemptive priority helps critical patients get doctors and resources first, but low priority patients can wait a long time (we have no aging). the "oldest blocked first" wake-up is fair-ish, but it doesn't save resources for anyone.
- **lock overhead:** one big lock for the whole resource table keeps things simple and makes "all or nothing" easy, but it means even unrelated resources wait on each other. one lock per resource would be faster but harder to get right. sending jobs to workers costs a bit of extra handing over too.

---

## 11. assumptions and limits

- hospital rows have 6 fields. old 5-field rows get `SPACE_REQ=1`. `Q` is read but not used.
- resource actions take 0 time. a release at the very end of service happens before the patient finishes.
- if there are no resources and no `--final`, it prints the old a1 table (so the a1 tests still work). use `--final` for the new format.
- memory utilization is counted over time, from the first arrival to the last finish.
- a patient can hold at most 7 resources at once.
- `--unsafe` is only for the demo. it stops at the first deadlock and doesn't try to fix it.
- the deadlock check assumes each resource has 1 copy (true for every hospital resource).

## 12. extra features

the a1 extras (gantt, stats, compare) are described above. new for the final project: the event trace (`--trace`) and the deadlock loop check in `--unsafe`.

---
