#include <string.h>
#include "scheduler.h"

/* Smaller priority value first, then earlier arrival, then smaller ID. */
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

/* Preemptive priority: the shared engine with preemption enabled. */
int simulate_priority_preemptive(Patient patients[], int n)
{
    return run_event_engine(patients, n, priority_better, 1);
}
