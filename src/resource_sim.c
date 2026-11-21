#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "resource_sim.h"

/*
 * Deadlock prevention policy: ALL-OR-NOTHING (removes "hold and wait").
 *
 *  - A request that can be granted at once is granted (no waiting).
 *  - If an ACQUIRE of X cannot be granted while the patient already holds
 *    other resources H, the patient ROLLS BACK: it releases H and blocks
 *    with the pending set {X} + H. The set is granted atomically
 *    (resource_try_acquire_all), so a blocked patient never holds anything
 *    and a circular wait cannot form.
 *  - A patient that is merely preempted keeps what it holds.
 *
 * Determinism: tasks that touch the same resource are run in separate
 * "waves" in a fixed order (block time, then patient ID), while tasks on
 * different resources run in the same wave, i.e. truly in parallel on the
 * two workers. Virtual-time results therefore do not depend on thread
 * timing; only the worker that ran a task (shown in the trace) does.
 */

ResourceReport g_resource_report;
static int trace_on = 0;
static int unsafe_mode = 0;   /* 1 when --unsafe is given (prevention off) */

void resource_trace_enable(int on)
{
    trace_on = on;
}

void rsim_set_unsafe(int on)
{
    unsafe_mode = on;
}

int rsim_is_unsafe(void)
{
    return unsafe_mode;
}

/* trace line printed by the coordinator (CO), not by a worker */
void sim_trace(int now, const char *event, const char *pid, const char *what)
{
    if (trace_on) {
        fprintf(stderr, "TRACE t=%-4d CO %-16s %-6s %s\n",
                now, event, pid, what);
    }
}

static void trace(int now, int worker, const char *event,
                  const char *pid, const char *what)
{
    if (trace_on) {
        fprintf(stderr, "TRACE t=%-4d W%d %-16s %-6s %s\n",
                now, worker + 1, event, pid, what);
    }
}

/* ---------------------------------------------------------------- */
/* Set-up and tear-down                                              */
/* ---------------------------------------------------------------- */

int rsim_init(ResourceSim *rs)
{
    memset(rs, 0, sizeof(*rs));
    memset(&g_resource_report, 0, sizeof(g_resource_report));

    if (g_resource_config.count == 0) {
        return 0;
    }

    if (resource_manager_init(&rs->manager) != RESOURCE_SUCCESS) {
        return -1;
    }

    for (int i = 0; i < g_resource_config.count; i++) {
        if (resource_manager_add(&rs->manager,
                                 g_resource_config.defs[i].id,
                                 g_resource_config.defs[i].capacity)
            != RESOURCE_SUCCESS) {
            resource_manager_destroy(&rs->manager);
            return -1;
        }
    }

    rs->jobs = calloc(MAX_PATIENTS, sizeof(ResourceJob));
    if (rs->jobs == NULL ||
        worker_pool_init(&rs->pool, &rs->manager, MAX_WORKERS) != 0) {
        free(rs->jobs);
        resource_manager_destroy(&rs->manager);
        return -1;
    }

    rs->active = 1;
    return 0;
}

static int compare_rows(const void *a, const void *b)
{
    return strcmp(((const ResourceReportRow *)a)->id,
                  ((const ResourceReportRow *)b)->id);
}

void rsim_finish(ResourceSim *rs, int completed, int unresolved)
{
    g_resource_report.completed_count = completed;
    g_resource_report.unresolved_deadlocks = unresolved > 0 ? 1 : 0;

    if (!rs->active) {
        return;
    }

    for (int i = 0; i < g_resource_config.count; i++) {
        const char *id = g_resource_config.defs[i].id;
        ResourceReportRow *row =
            &g_resource_report.rows[g_resource_report.count++];

        strcpy(row->id, id);
        row->capacity = g_resource_config.defs[i].capacity;
        row->max_holders = resource_get_max_holders(&rs->manager, id);
        row->acquisitions = resource_get_acquisitions(&rs->manager, id);
        row->wait_events = resource_get_wait_events(&rs->manager, id);
    }
    qsort(g_resource_report.rows, (size_t)g_resource_report.count,
          sizeof(ResourceReportRow), compare_rows);

    g_resource_report.exclusivity_violations =
        resource_get_violations(&rs->manager);

    worker_pool_destroy(&rs->pool);
    resource_manager_destroy(&rs->manager);
    free(rs->jobs);
    rs->jobs = NULL;
    rs->active = 0;
}

/* ---------------------------------------------------------------- */
/* Running a batch of jobs on the worker threads                     */
/* ---------------------------------------------------------------- */

static int job_ids(const WorkerTask *t, const char *ids[], int max)
{
    int count = 0;

    if (t->operation == WORKER_ACQUIRE_ALL) {
        for (int i = 0; i < t->resource_count && count < max; i++) {
            ids[count++] = t->resource_set[i];
        }
    } else {
        ids[count++] = t->resource_id;
    }
    return count;
}

static int conflicts(const WorkerTask *t, const char *used[], int used_n)
{
    const char *ids[RESOURCE_SET_MAX];
    int n = job_ids(t, ids, RESOURCE_SET_MAX);

    for (int i = 0; i < n; i++) {
        for (int j = 0; j < used_n; j++) {
            if (strcmp(ids[i], used[j]) == 0) {
                return 1;
            }
        }
    }
    return 0;
}

/* Run jobs[0..count) and store each status/worker. */
static void run_jobs(ResourceSim *rs, ResourceJob *jobs, int count)
{
    int remaining = count;

    for (int i = 0; i < count; i++) {
        jobs[i].done = 0;
    }

    while (remaining > 0) {
        const char *used[MAX_RESOURCES];
        int used_n = 0;
        int wave[MAX_RESOURCES];
        int wave_n = 0;

        for (int i = 0; i < count && wave_n < MAX_RESOURCES; i++) {
            if (jobs[i].done || conflicts(&jobs[i].task, used, used_n)) {
                continue;
            }
            const char *ids[RESOURCE_SET_MAX];
            int n = job_ids(&jobs[i].task, ids, RESOURCE_SET_MAX);
            if (used_n + n > MAX_RESOURCES) {
                continue;
            }
            for (int k = 0; k < n; k++) {
                used[used_n++] = ids[k];
            }
            wave[wave_n++] = i;
        }

        int submitted = 0;
        for (int w = 0; w < wave_n; w++) {
            ResourceJob *job = &jobs[wave[w]];

            if (worker_pool_submit(&rs->pool, &job->task) == 0) {
                submitted++;
            } else {
                job->status = RESOURCE_MANAGER_ERROR;
                job->done = 1;
                rs->failed = 1;
                remaining--;
            }
        }

        for (int w = 0; w < submitted; w++) {
            WorkerResult result;

            if (worker_pool_get_result(&rs->pool, &result) != 0) {
                rs->failed = 1;
                return;
            }
            jobs[result.task_id].status = result.status;
            jobs[result.task_id].worker = result.worker;
            jobs[result.task_id].done = 1;
            remaining--;
        }
    }
}

static void set_job(ResourceJob *job, int id, WorkerOperation op,
                    Patient *p, const char *resource)
{
    memset(job, 0, sizeof(*job));
    job->task.task_id = id;
    job->task.operation = op;
    job->task.units = 1;
    snprintf(job->task.process_id, sizeof(job->task.process_id),
             "%s", p->id);
    snprintf(job->task.resource_id, sizeof(job->task.resource_id),
             "%s", resource);
    job->p = p;
}

/* ---------------------------------------------------------------- */
/* Plan helpers                                                      */
/* ---------------------------------------------------------------- */

static const ResourceAction *next_action(const Patient *p)
{
    const PatientResourcePlan *plan = &p->resource_plan;

    return plan->current_action < plan->count
               ? &plan->actions[plan->current_action] : NULL;
}

int rsim_action_due(const Patient *p)
{
    const ResourceAction *a = next_action(p);

    return a != NULL && a->work_offset <= p->service_time - p->remaining_time;
}

int rsim_next_action_time(const Patient *p, int now)
{
    const ResourceAction *a = next_action(p);

    if (a == NULL) {
        return -1;
    }
    return now + (a->work_offset - (p->service_time - p->remaining_time));
}

static int by_id(const void *a, const void *b)
{
    return strcmp((*(Patient *const *)a)->id, (*(Patient *const *)b)->id);
}

static int by_block_time(const void *a, const void *b)
{
    const Patient *x = *(Patient *const *)a;
    const Patient *y = *(Patient *const *)b;

    if (x->resource_block_start != y->resource_block_start) {
        return x->resource_block_start - y->resource_block_start;
    }
    return strcmp(x->id, y->id);
}

static void describe_set(const Patient *p, char *out, size_t size)
{
    out[0] = '\0';
    for (int i = 0; i < p->pending_count; i++) {
        size_t used = strlen(out);
        snprintf(out + used, size - used, "%s%s", i ? "+" : "",
                 p->pending_set[i]);
    }
}

/* ---------------------------------------------------------------- */
/* Wake-up: retry every blocked patient (FIFO by block time)         */
/* ---------------------------------------------------------------- */

static int wake_blocked(ResourceSim *rs, Patient patients[], int n, int now)
{
    Patient *blocked[MAX_PATIENTS];
    int count = 0;
    int woken = 0;

    for (int i = 0; i < n; i++) {
        if (patients[i].state == PROCESS_BLOCKED_RESOURCE) {
            blocked[count++] = &patients[i];
        }
    }
    if (count == 0) {
        return 0;
    }
    qsort(blocked, (size_t)count, sizeof(Patient *), by_block_time);

    for (int i = 0; i < count; i++) {
        ResourceJob *job = &rs->jobs[i];
        Patient *p = blocked[i];

        set_job(job, i, WORKER_ACQUIRE_ALL, p, p->pending_set[0]);
        for (int k = 0; k < p->pending_count; k++) {
            strcpy(job->task.resource_set[k], p->pending_set[k]);
        }
        job->task.resource_count = p->pending_count;
        job->task.counted = 1;            /* only the new request counts */
    }
    run_jobs(rs, rs->jobs, count);

    for (int i = 0; i < count; i++) {
        ResourceJob *job = &rs->jobs[i];
        Patient *p = job->p;
        char what[128];

        if (job->status == RESOURCE_BUSY) {
            continue;                      /* stays blocked */
        }
        if (job->status != RESOURCE_SUCCESS) {
            rs->failed = 1;
            continue;
        }

        describe_set(p, what, sizeof(what));
        trace(now, job->worker, "UNBLOCK/ACQUIRE", p->id, what);

        p->resource_wait_time += now - p->resource_block_start;
        p->resource_plan.current_action++;
        p->pending_count = 0;
        p->scheduler_queued = 0;
        p->state = PROCESS_READY;         /* must be scheduled again */
        woken++;
    }
    return woken;
}

/* ---------------------------------------------------------------- */
/* One round of due actions                                          */
/* ---------------------------------------------------------------- */

int rsim_process_due(ResourceSim *rs, Patient patients[], int n,
                     Patient *running[NUM_DOCTORS], int now)
{
    Patient *due[NUM_DOCTORS];
    const ResourceAction *action[NUM_DOCTORS];   /* fixed for this round */
    Patient *rolled[NUM_DOCTORS];
    int due_n = 0;
    int rolled_n = 0;
    int progress = 0;

    if (!rs->active) {
        return 0;
    }

    for (int d = 0; d < NUM_DOCTORS; d++) {
        if (running[d] != NULL && rsim_action_due(running[d])) {
            due[due_n++] = running[d];
        }
    }
    if (due_n == 0) {
        return 0;
    }
    qsort(due, (size_t)due_n, sizeof(Patient *), by_id);
    for (int i = 0; i < due_n; i++) {
        action[i] = next_action(due[i]);
    }

    /* Phase 1: releases (all independent resources run in parallel). */
    int jobs_n = 0;
    for (int i = 0; i < due_n; i++) {
        const ResourceAction *a = action[i];

        if (a->type == RESOURCE_ACTION_RELEASE) {
            set_job(&rs->jobs[jobs_n], jobs_n, WORKER_RELEASE,
                    due[i], a->resource_id);
            jobs_n++;
        }
    }
    run_jobs(rs, rs->jobs, jobs_n);
    for (int j = 0; j < jobs_n; j++) {
        ResourceJob *job = &rs->jobs[j];

        if (job->status != RESOURCE_SUCCESS) {
            rs->failed = 1;
        }
        trace(now, job->worker, "RELEASE", job->p->id,
              job->task.resource_id);
        job->p->resource_plan.current_action++;
        progress++;
    }
    if (jobs_n > 0) {
        progress += wake_blocked(rs, patients, n, now);
    }

    /* Phase 2: acquisitions (blocked waiters were served first). */
    jobs_n = 0;
    for (int i = 0; i < due_n; i++) {
        const ResourceAction *a = action[i];

        if (a->type == RESOURCE_ACTION_ACQUIRE) {
            set_job(&rs->jobs[jobs_n], jobs_n, WORKER_ACQUIRE,
                    due[i], a->resource_id);
            jobs_n++;
        }
    }
    run_jobs(rs, rs->jobs, jobs_n);
    for (int j = 0; j < jobs_n; j++) {
        ResourceJob *job = &rs->jobs[j];
        Patient *p = job->p;

        if (job->status == RESOURCE_SUCCESS) {
            trace(now, job->worker, "ACQUIRE", p->id,
                  job->task.resource_id);
            p->resource_plan.current_action++;
            progress++;
            continue;
        }
        if (job->status != RESOURCE_BUSY) {
            rs->failed = 1;
            continue;
        }

        /* Cannot be granted: give up the doctor and (all-or-nothing)
         * every resource held, then wait for the whole set. */
        char held[RESOURCE_SET_MAX][RESOURCE_ID_LENGTH];
        int held_n = resource_list_held(&rs->manager, p->id, held,
                                        RESOURCE_SET_MAX - 1);

        /* unsafe mode: the patient keeps what it holds while it waits.
         * this is "hold and wait", and it is what lets a deadlock happen. */
        if (unsafe_mode) {
            held_n = 0;
        }

        strcpy(p->pending_set[0], job->task.resource_id);
        for (int k = 0; k < held_n; k++) {
            strcpy(p->pending_set[k + 1], held[k]);
        }
        p->pending_count = held_n + 1;
        p->resource_block_start = now;
        p->state = PROCESS_BLOCKED_RESOURCE;
        for (int d = 0; d < NUM_DOCTORS; d++) {
            if (running[d] == p) {
                running[d] = NULL;
            }
        }
        trace(now, job->worker, "BLOCK", p->id, job->task.resource_id);
        if (held_n > 0) {
            rolled[rolled_n++] = p;
        }
        progress++;
    }

    /* Roll back: release what the blocked patients were holding. */
    jobs_n = 0;
    for (int i = 0; i < rolled_n; i++) {
        for (int k = 1; k < rolled[i]->pending_count; k++) {
            set_job(&rs->jobs[jobs_n], jobs_n, WORKER_RELEASE,
                    rolled[i], rolled[i]->pending_set[k]);
            jobs_n++;
        }
    }
    run_jobs(rs, rs->jobs, jobs_n);
    for (int j = 0; j < jobs_n; j++) {
        if (rs->jobs[j].status != RESOURCE_SUCCESS) {
            rs->failed = 1;
        }
        trace(now, rs->jobs[j].worker, "ROLLBACK_RELEASE",
              rs->jobs[j].p->id, rs->jobs[j].task.resource_id);
    }
    if (jobs_n > 0) {
        progress += wake_blocked(rs, patients, n, now);
    }

    return progress;
}

/* ---------------------------------------------------------------- */
/* deadlock check (used by --unsafe)                                 */
/* ---------------------------------------------------------------- */

/*
 * idea: every blocked patient is waiting for one resource, and that
 * resource has one holder. so "p waits for q" is an arrow p -> q.
 * if we follow the arrows from p and come back to p, it's a loop,
 * which means a deadlock (nobody in the loop can ever move).
 */
#define MAX_LOOP 64

static struct {
    int found;
    int time;
    int length;
    char pid[MAX_LOOP][MAX_ID_LENGTH];
    char wants[MAX_LOOP][RESOURCE_ID_LENGTH];
    char holder[MAX_LOOP][MAX_ID_LENGTH];
} loop_info;

/* index of the patient that holds this resource, or -1 if it's free */
static int holder_of(ResourceSim *rs, Patient patients[], int n,
                     const char *resource)
{
    for (int i = 0; i < n; i++) {
        char held[RESOURCE_SET_MAX][RESOURCE_ID_LENGTH];
        int count = resource_list_held(&rs->manager, patients[i].id, held,
                                       RESOURCE_SET_MAX);

        for (int k = 0; k < count; k++) {
            if (strcmp(held[k], resource) == 0) {
                return i;
            }
        }
    }
    return -1;
}

/* who is patient i waiting for? -1 if it isn't blocked on a resource */
static int waits_for(ResourceSim *rs, Patient patients[], int n, int i)
{
    if (patients[i].state != PROCESS_BLOCKED_RESOURCE) {
        return -1;
    }
    return holder_of(rs, patients, n, patients[i].pending_set[0]);
}

int rsim_detect_deadlock(ResourceSim *rs, Patient patients[], int n,
                         int now)
{
    if (!rs->active) {
        return 0;
    }

    for (int start = 0; start < n; start++) {
        /* follow the arrows from start, at most n steps */
        int cur = waits_for(rs, patients, n, start);
        int steps = 0;

        while (cur != -1 && cur != start && steps < n) {
            cur = waits_for(rs, patients, n, cur);
            steps++;
        }
        if (cur != start) {
            continue;              /* no loop through this patient */
        }

        /* found a loop: walk it once more and save it for the report */
        memset(&loop_info, 0, sizeof(loop_info));
        loop_info.found = 1;
        loop_info.time = now;
        cur = start;
        do {
            int next = waits_for(rs, patients, n, cur);
            int k = loop_info.length++;

            snprintf(loop_info.pid[k], MAX_ID_LENGTH, "%s", patients[cur].id);
            snprintf(loop_info.wants[k], RESOURCE_ID_LENGTH, "%s",
                     patients[cur].pending_set[0]);
            snprintf(loop_info.holder[k], MAX_ID_LENGTH, "%s",
                     patients[next].id);
            sim_trace(now, "DEADLOCK", patients[cur].id,
                      loop_info.wants[k]);
            cur = next;
        } while (cur != start && loop_info.length < MAX_LOOP);
        return 1;
    }
    return 0;
}

/* if the normal run gets stuck, say who is stuck and on what */
void rsim_report_blocked(Patient patients[], int n, int now)
{
    for (int i = 0; i < n; i++) {
        if (patients[i].state == PROCESS_BLOCKED_RESOURCE) {
            fprintf(stderr, "  t=%d: %s is stuck waiting for %s\n",
                    now, patients[i].id, patients[i].pending_set[0]);
        }
    }
}

/* prints: DEADLOCK,DETECTED_AT,t and one DEADLOCK,WAIT_FOR row per arrow */
void print_deadlock_report(FILE *out)
{
    if (!loop_info.found) {
        return;
    }
    fprintf(out, "DEADLOCK,DETECTED_AT,%d\n", loop_info.time);
    for (int k = 0; k < loop_info.length; k++) {
        fprintf(out, "DEADLOCK,WAIT_FOR,%s,%s,%s\n", loop_info.pid[k],
                loop_info.wants[k], loop_info.holder[k]);
    }
}
