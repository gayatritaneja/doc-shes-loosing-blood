#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "resource_sim.h"
#include "scheduler.h"

/*
 * Event-driven engine shared by FCFS, PRIORITY and PRIORITY_PREEMPTIVE.
 * Virtual time jumps between events: an arrival, a completion, or a
 * resource action reached by a running patient. At each event the state is
 * "settled": resource actions run on the worker threads (blocking patients
 * and waking others), completions free memory, memory admission runs, and
 * idle doctors are filled, until nothing changes.
 */

typedef struct {
    BetterFn better;
    Patient *items[MAX_PATIENTS];
    int size;
} ReadyQueue;

static void swap_patients(Patient **a, Patient **b)
{
    Patient *temp = *a;
    *a = *b;
    *b = temp;
}

static int push(ReadyQueue *q, Patient *p)
{
    if (q->size >= MAX_PATIENTS) {
        return 0;
    }
    int i = q->size++;
    q->items[i] = p;
    while (i > 0) {
        int parent = (i - 1) / 2;
        if (!q->better(q->items[i], q->items[parent])) {
            break;
        }
        swap_patients(&q->items[i], &q->items[parent]);
        i = parent;
    }
    return 1;
}

static Patient *peek(const ReadyQueue *q)
{
    return q->size == 0 ? NULL : q->items[0];
}

static Patient *pop(ReadyQueue *q)
{
    if (q->size == 0) {
        return NULL;
    }
    Patient *result = q->items[0];
    q->items[0] = q->items[--q->size];
    int i = 0;
    while (1) {
        int left = 2 * i + 1;
        int right = 2 * i + 2;
        int best = i;

        if (left < q->size && q->better(q->items[left], q->items[best])) {
            best = left;
        }
        if (right < q->size && q->better(q->items[right], q->items[best])) {
            best = right;
        }
        if (best == i) {
            break;
        }
        swap_patients(&q->items[i], &q->items[best]);
        i = best;
    }
    result->scheduler_queued = 0;
    return result;
}

static int choose_preemption_victim(Patient *running[NUM_DOCTORS])
{
    int victim = -1;

    for (int d = 0; d < NUM_DOCTORS; d++) {
        if (running[d] == NULL) {
            continue;
        }
        if (victim == -1 ||
            running[d]->priority > running[victim]->priority ||
            (running[d]->priority == running[victim]->priority &&
             running[d]->arrival_time > running[victim]->arrival_time) ||
            (running[d]->priority == running[victim]->priority &&
             running[d]->arrival_time == running[victim]->arrival_time &&
             strcmp(running[d]->id, running[victim]->id) > 0)) {
            victim = d;
        }
    }
    return victim;
}

static int next_event_time(const Patient patients[], int n, int next_arrival,
                           Patient *running[NUM_DOCTORS], int now)
{
    int next_time = -1;

    if (next_arrival < n) {
        next_time = patients[next_arrival].arrival_time;
    }
    for (int d = 0; d < NUM_DOCTORS; d++) {
        if (running[d] == NULL) {
            continue;
        }
        int candidates[2] = {
            now + running[d]->remaining_time,
            rsim_next_action_time(running[d], now)
        };
        for (int c = 0; c < 2; c++) {
            if (candidates[c] > now &&
                (next_time == -1 || candidates[c] < next_time)) {
                next_time = candidates[c];
            }
        }
    }
    return next_time;
}

static int compare_arrivals(const void *x, const void *y)
{
    const Patient *a = x;
    const Patient *b = y;

    if (a->arrival_time != b->arrival_time) {
        return a->arrival_time < b->arrival_time ? -1 : 1;
    }
    return strcmp(a->id, b->id);
}

int run_event_engine(Patient patients[], int n, BetterFn better,
                     int preemptive)
{
    ReadyQueue ready;
    MemoryManager memory;
    MemoryAdmissionQueue admission;
    ResourceSim rs;
    Patient *running[NUM_DOCTORS] = {NULL};
    int current_time = 0;
    int next_arrival = 0;
    int completed = 0;
    int rc = 0;

    if (n < 0 || n > MAX_PATIENTS) {
        return 1;
    }

    /* Process arrivals chronologically (ties by ID). */
    qsort(patients, (size_t)n, sizeof(Patient), compare_arrivals);

    ready.better = better;
    ready.size = 0;
    memory_manager_init(&memory);
    memory_admission_init(&admission);
    stats_reset();

    for (int i = 0; i < n; i++) {
        patients[i].remaining_time = patients[i].service_time;
        patients[i].first_start_time = -1;
        patients[i].completion_time = -1;
        patients[i].memory_start = -1;
        patients[i].memory_slots = 0;
        patients[i].memory_admission_time = -1;
        patients[i].scheduler_queued = 0;
        patients[i].resource_wait_time = 0;
        patients[i].resource_block_start = 0;
        patients[i].pending_count = 0;
        patients[i].resource_plan.current_action = 0;
        patients[i].state = PROCESS_NEW;
        patients[i].mem_wait_logged = 0;
        patients[i].placed_at = -1;
        if (patients[i].memory_requirement <= 0) {
            patients[i].memory_requirement = 1;
        }
    }

    if (rsim_init(&rs) != 0) {
        fprintf(stderr, "Error: cannot start the resource workers.\n");
        return 1;
    }

    while (completed < n) {
        int event_time = next_event_time(patients, n, next_arrival,
                                         running, current_time);

        if (event_time == -1) {
            /* nothing left that can happen. with prevention on this
             * should never happen, but if it does we say why and stop
             * instead of hanging. */
            if (rsim_detect_deadlock(&rs, patients, n, current_time)) {
                rc = ENGINE_DEADLOCK;
                break;
            }
            fprintf(stderr,
                    "Simulation error: no future event; the remaining "
                    "patients cannot proceed (unsatisfiable request):\n");
            rsim_report_blocked(patients, n, current_time);
            rc = 1;
            break;
        }

        int elapsed = event_time - current_time;

        /* for memory utilization: add up slots used x time passed */
        memory.occupied_slot_time +=
            (long)memory_occupied_slots(&memory) * elapsed;

        for (int d = 0; d < NUM_DOCTORS; d++) {
            if (running[d] != NULL) {
                running[d]->remaining_time -= elapsed;
                stats_note_run(d, running[d]->id,
                               current_time, current_time + elapsed);
            }
        }
        current_time = event_time;

        while (next_arrival < n &&
               patients[next_arrival].arrival_time <= current_time) {
            sim_trace(current_time, "ARRIVE", patients[next_arrival].id,
                      "joins the memory queue");
            memory_admission_enqueue(&admission, &patients[next_arrival]);
            next_arrival++;
        }

        /* Settle this instant until nothing changes any more. */
        for (;;) {
            int changed = rsim_process_due(&rs, patients, n, running,
                                           current_time);

            for (int d = 0; d < NUM_DOCTORS; d++) {
                Patient *p = running[d];

                if (p != NULL && p->remaining_time == 0 &&
                    p->resource_plan.current_action >=
                        p->resource_plan.count) {
                    sim_trace(current_time, "COMPLETE", p->id,
                              "done, frees its memory");
                    p->completion_time = current_time;
                    memory_deallocate(&memory, p);
                    p->state = PROCESS_TERMINATED;
                    running[d] = NULL;
                    completed++;
                    changed++;
                }
            }

            memory_process_admission(&memory, &admission,
                                     memory_get_allocation_algorithm(),
                                     current_time);

            for (int i = 0; i < n; i++) {
                if (patients[i].state == PROCESS_READY &&
                    !patients[i].scheduler_queued) {
                    if (!push(&ready, &patients[i])) {
                        fprintf(stderr, "Error: ready queue is full.\n");
                        rc = 1;
                        goto done;
                    }
                    patients[i].scheduler_queued = 1;
                }
            }

            stats_note_queue_len(ready.size);

            for (int d = 0; d < NUM_DOCTORS; d++) {
                if (running[d] == NULL && ready.size > 0) {
                    running[d] = pop(&ready);
                    running[d]->state = PROCESS_RUNNING;
                    sim_trace(current_time, "DISPATCH", running[d]->id,
                              d == 0 ? "doctor 1" : "doctor 2");
                    stats_note_dispatch();
                    if (running[d]->first_start_time == -1) {
                        running[d]->first_start_time = current_time;
                    }
                    changed++;
                }
            }

            /* Preempt only for a strictly higher priority. */
            while (preemptive && ready.size > 0) {
                int victim = choose_preemption_victim(running);

                if (victim == -1 ||
                    peek(&ready)->priority >= running[victim]->priority) {
                    break;
                }

                Patient *current = running[victim];

                if (!push(&ready, current)) {
                    fprintf(stderr, "Error: ready queue is full.\n");
                    rc = 1;
                    goto done;
                }
                current->state = PROCESS_READY;
                current->scheduler_queued = 1;
                running[victim] = pop(&ready);
                running[victim]->state = PROCESS_RUNNING;
                /* the preempted patient keeps any resources it holds */
                sim_trace(current_time, "PREEMPT", current->id,
                          "back to READY, keeps its resources");
                sim_trace(current_time, "DISPATCH", running[victim]->id,
                          victim == 0 ? "doctor 1" : "doctor 2");
                stats_note_preemption();
                stats_note_dispatch();
                if (running[victim]->first_start_time == -1) {
                    running[victim]->first_start_time = current_time;
                }
                changed++;
            }

            if (!changed) {
                break;
            }
        }

        /* --memmap: show the slots after everything at this time is done */
        memory_print_map(&memory, current_time);

        if (rs.failed) {
            fprintf(stderr, "Error: resource subsystem failure.\n");
            rc = 1;
            break;
        }

        /* --unsafe: if there is a deadlock now, stop here (never hang) */
        if (rsim_is_unsafe() &&
            rsim_detect_deadlock(&rs, patients, n, current_time)) {
            rc = ENGINE_DEADLOCK;
            break;
        }
    }

done:;
    g_memory_report.peak_occupied = memory.peak_occupied;
    g_memory_report.occupied_slot_time = memory.occupied_slot_time;
    g_memory_report.max_external_fragmentation =
        memory.max_observed_external_fragmentation;
    int unresolved = 0;
    for (int i = 0; i < n; i++) {
        if (patients[i].state == PROCESS_BLOCKED_RESOURCE) {
            unresolved++;
        }
    }
    rsim_finish(&rs, completed, unresolved);
    return rc;
}
