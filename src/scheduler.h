#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stddef.h>
#include <stdio.h>

#define MAX_PATIENTS 1000
#define MAX_ID_LENGTH 32
#define NUM_DOCTORS 2

typedef struct
{
    char id[MAX_ID_LENGTH];
    char category[MAX_ID_LENGTH];

    int arrival_time;
    int priority;
    int service_time;
    int remaining_time;

    int first_start_time;
    int completion_time;
} Patient;

/* Shared input/output functions, to be implemented in a separate module. */
int read_input(const char *filename,
               Patient patients[],
               int *n,
               int *quantum);

void print_results(Patient patients[], int n);

/* Scheduling algorithms (priority_preemptive.c, fcfs.c, priority.c). */
int simulate_priority_preemptive(Patient patients[], int n);
int simulate_fcfs(Patient patients[], int n);
int simulate_priority_nonpreemptive(Patient patients[], int n);

/* Shared non-preemptive engine (nonpreemptive_core.c). `better` returns
 * non-zero if patient a should be served before patient b. */
typedef int (*BetterFn)(const Patient *a, const Patient *b);
int run_nonpreemptive(Patient patients[], int n, BetterFn better);


/* ------------------------------------------------------------------ */
/* Summary metrics (spec Section 10), shared by print_results() and    */
/* the COMPARE mode so both always agree.                              */
/* ------------------------------------------------------------------ */
typedef struct
{
    double avg_waiting;
    double avg_turnaround;
    double avg_response;
    double throughput;
    double utilization;      /* percentage, 0-100 */
    int first_arrival;
    int last_completion;
} Summary;

void compute_summary(const Patient patients[], int n, Summary *out);

/* ------------------------------------------------------------------ */
/* Algorithm table entry (used by main.c and compare.c).               */
/* ------------------------------------------------------------------ */
typedef int (*SchedulerFunction)(Patient patients[], int n);

typedef struct
{
    const char *name;
    SchedulerFunction run;
} AlgorithmEntry;

/* ------------------------------------------------------------------ */
/* Instrumentation (stats.c). Schedulers call the stats_note_* hooks;  */
/* they never print anything themselves.                               */
/* ------------------------------------------------------------------ */
#define MAX_SEGMENTS 8192

typedef struct
{
    int doctor;
    int start;
    int end;
    char id[MAX_ID_LENGTH];
} RunSegment;

typedef struct
{
    int preemptions;                 /* running patient pushed back to READY */
    int dispatches;                  /* patient placed on a doctor           */
    int max_ready_len;               /* longest READY queue before dispatch  */
    int busy_time[NUM_DOCTORS];      /* time units each doctor was working   */
    int segment_count;
    int segments_dropped;            /* non-zero if MAX_SEGMENTS overflowed  */
    RunSegment segments[MAX_SEGMENTS];
} SimStats;

extern SimStats g_stats;

void stats_reset(void);
void stats_note_dispatch(void);
void stats_note_preemption(void);
void stats_note_queue_len(int len);
void stats_note_run(int doctor, const char *id, int start, int end);

void print_stats(FILE *out);                 /* preemptions, dispatches ... */
void print_gantt(FILE *out);                 /* per-doctor timeline (gantt.c) */

/* COMPARE mode (compare.c): run every algorithm on one input. */
int run_compare(const Patient original[], int n,
                const AlgorithmEntry algorithms[], size_t count, int csv);

#endif
