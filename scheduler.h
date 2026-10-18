#ifndef SCHEDULER_H
#define SCHEDULER_H

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

/* Scheduling algorithm implemented in priority_preemptive.c. */
int simulate_priority_preemptive(Patient patients[], int n);
int simulate_fcfs(Patient patients[], int n);
int simulate_priority_nonpreemptive(Patient patients[], int n);

/* Shared non-preemptive engine (nonpreemptive_core.c). `better` returns
 * non-zero if patient a should be served before patient b. */
typedef int (*BetterFn)(const Patient *a, const Patient *b);
int run_nonpreemptive(Patient patients[], int n, BetterFn better);

#endif
