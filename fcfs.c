#include <string.h>
#include "scheduler.h"

/* FCFS: earlier arrival first, then lexicographically smaller ID. */
static int fcfs_better(const Patient *a, const Patient *b)
{
    if (a->arrival_time != b->arrival_time)
    {
        return a->arrival_time < b->arrival_time;
    }
    return strcmp(a->id, b->id) < 0;
}

int simulate_fcfs(Patient patients[], int n)
{
    return run_nonpreemptive(patients, n, fcfs_better);
}