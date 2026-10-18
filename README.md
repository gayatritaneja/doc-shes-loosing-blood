# Hospital scheduler

This project simulates CPU-style scheduling for an emergency hospital with
two doctors. Patients are processes and doctors are two identical parallel
service units. Three algorithms are implemented:

- `FCFS`: first-come, first-served (non-preemptive)
- `PRIORITY`: non-preemptive priority scheduling
- `PRIORITY_PREEMPTIVE`: preemptive priority scheduling

## Compile

From the project directory, compile all source files into the `scheduler`
executable:

```
gcc -std=c11 -Wall -Wextra -Wpedantic -Werror \
  main.c io.c priority_preemptive.c nonpreemptive.c -o scheduler
```

You can use `clang` or `cc` instead of `gcc`.

## Run the program

```sh
./scheduler input.txt PRIORITY_PREEMPTIVE
```

The command prints the scheduling results as CSV. To compile and run in one
step:

```sh
cc -std=c11 -Wall -Wextra -Wpedantic -Werror \
  main.c io.c priority_preemptive.c -o scheduler &&
./scheduler input.txt PRIORITY_PREEMPTIVE
```
where `ALGORITHM` is one of `FCFS`, `PRIORITY` or `PRIORITY_PREEMPTIVE`.

```
./scheduler input.txt FCFS
./scheduler input.txt PRIORITY
./scheduler input.txt PRIORITY_PREEMPTIVE
```

The first two lines contain the patient count and time quantum. The remaining
records use five comma-separated fields:

```text
8
2
patient_id,arrival_time,priority,service_time,category
```

Lower numeric priority values are scheduled first. Blank lines and lines
starting with `#` are ignored.

## Input format

The first two lines contain the patient count and the Round Robin time
quantum (not used by these algorithms). Each remaining line is one patient:

```
8
2
ID,ARRIVAL,PRIORITY,SERVICE,CATEGORY
```

Lower numeric priority values are scheduled first. Blank lines and lines
starting with `#` are ignored.

## Output format

One row per patient, sorted by process ID, then a summary block:

```
PROCESS_ID,ARRIVAL,PRIORITY,SERVICE,START,COMPLETION,WAITING,TURNAROUND,RESPONSE
...
SUMMARY
AVERAGE_WAITING,...
AVERAGE_TURNAROUND,...
AVERAGE_RESPONSE,...
THROUGHPUT,...
UTILIZATION,...
```

`UTILIZATION` is printed as a percentage value without the `%` sign.

