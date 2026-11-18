#include <string.h>
#include "scheduler.h"

/* Priority: smaller value first, then earlier arrival, then smaller ID. */
static int priority_better(const Patient *a, const Patient *b)
{
    if (a->priority != b->priority) {
        return a->priority < b->priority;
    }
    if (a->arrival_time != b->arrival_time) {
        return a->arrival_time < b->arrival_time;
    }
    return strcmp(a->id, b->id) < 0;
}

int simulate_priority_nonpreemptive(Patient patients[], int n)
{
    return run_nonpreemptive(patients, n, priority_better);
}