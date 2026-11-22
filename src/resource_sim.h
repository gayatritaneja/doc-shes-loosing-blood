#ifndef RESOURCE_SIM_H
#define RESOURCE_SIM_H

#include "scheduler.h"
#include "resource_actions.h"
#include "workers.h"

/*
 * Coordinator side of the shared-resource subsystem. The A1 engine owns the
 * virtual clock; every ACQUIRE/RELEASE it reaches is executed by the real
 * worker threads (workers.c) against the mutex-protected ResourceManager.
 */

typedef struct {
    WorkerTask task;
    int status;
    int worker;
    int done;
    Patient *p;
} ResourceJob;

typedef struct {
    ResourceManager manager;
    WorkerPool pool;
    ResourceJob *jobs;
    int active;           /* 0 when the input declares no resources */
    int failed;           /* internal error (never expected)        */
} ResourceSim;

typedef struct {
    char id[RESOURCE_ID_LENGTH];
    int capacity;
    int max_holders;
    int acquisitions;
    int wait_events;
} ResourceReportRow;

typedef struct {
    ResourceReportRow rows[MAX_RESOURCES];   /* sorted by id */
    int count;
    int exclusivity_violations;
    int unresolved_deadlocks;
    int completed_count;
} ResourceReport;

extern ResourceReport g_resource_report;     /* filled by rsim_finish() */

void resource_trace_enable(int on);          /* event trace on stderr   */

int rsim_init(ResourceSim *rs);
void rsim_finish(ResourceSim *rs, int completed, int unresolved);

/* Is the running patient's next resource action due (offset reached)? */
int rsim_action_due(const Patient *p);
/* Virtual time of the patient's next action if it keeps running, or -1. */
int rsim_next_action_time(const Patient *p, int now);

/*
 * Execute one round of due actions of the running patients, then wake the
 * blocked ones that can now proceed. Patients that block leave running[].
 * Returns how many actions/wake-ups happened (0 = nothing to do).
 */
int rsim_process_due(ResourceSim *rs, Patient patients[], int n,
                     Patient *running[NUM_DOCTORS], int now);

/* returns 1 if blocked patients are waiting on each other in a loop */
int rsim_detect_deadlock(ResourceSim *rs, Patient patients[], int n,
                         int now);

/* prints every blocked patient and what it waits for (stderr) */
void rsim_report_blocked(Patient patients[], int n, int now);

#endif
