#include "scheduler.h"
#include <string.h>

/*
 * Shared non-preemptive engine for NUM_DOCTORS identical doctors.
 * Each time a doctor becomes free, the best arrived patient (per `better`)
 * is started and runs to completion. If nobody has arrived yet, that
 * doctor idles until the next arrival.
 */
int run_nonpreemptive(Patient patients[], int n, BetterFn better)
{
    int free_at[NUM_DOCTORS] = {0};
    int scheduled[MAX_PATIENTS] = {0};
    MemoryManager memory;
    MemoryAdmissionQueue admission;

    if (n < 0 || n > MAX_PATIENTS) {
        return 1;
    }
    stats_reset();
    memory_manager_init(&memory);
    memory_admission_init(&admission);
    for (int i = 0; i < n; i++) {
        patients[i].remaining_time = patients[i].service_time;
        patients[i].first_start_time = -1;
        patients[i].completion_time = -1;
        patients[i].memory_start = -1;
        patients[i].memory_slots = 0;
        patients[i].memory_admission_time = -1;
        patients[i].state = PROCESS_NEW;
        if (patients[i].memory_requirement <= 0) {
            patients[i].memory_requirement = 1;
        }
    }

    for (int done = 0; done < n; ) {
        /* Pick the doctor who is free earliest (lowest index on ties). */
        int d = 0;
        for (int k = 1; k < NUM_DOCTORS; k++) {
            if (free_at[k] < free_at[d]) {
                d = k;
            }
        }
        int t = free_at[d];

        /* Finish processes completing at this timestamp, in ID order. */
        for (;;) {
            int finish = -1;
            for (int i = 0; i < n; i++) {
                if (patients[i].state == PROCESS_RUNNING &&
                    patients[i].completion_time == t) {
                    if (finish < 0 ||
                        strcmp(patients[i].id, patients[finish].id) < 0) {
                        finish = i;
                    }
                }
            }
            if (finish < 0) {
                break;
            }
            memory_deallocate(&memory, &patients[finish]);
            patients[finish].state = PROCESS_TERMINATED;
        }

        /* Register all arrivals due at this timestamp in ID order. */
        for (;;) {
            int next = -1;
            for (int i = 0; i < n; i++) {
                if (patients[i].state == PROCESS_NEW &&
                    patients[i].arrival_time <= t) {
                    if (next < 0 ||
                        strcmp(patients[i].id, patients[next].id) < 0) {
                        next = i;
                    }
                }
            }
            if (next < 0) {
                break;
            }
            memory_admission_enqueue(&admission, &patients[next]);
        }

        memory_process_admission(&memory, &admission,
                                 memory_get_allocation_algorithm(), t);

        /* Choose the best patient that has already arrived by time t. */
        int best = -1;
        int next_arrival = -1;
        int waiting = 0;                 /* arrived but not yet started */
        for (int i = 0; i < n; i++) {
            if (scheduled[i]) {
                continue;
            }
            if (patients[i].state == PROCESS_READY) {
                waiting++;
                if (best < 0 || better(&patients[i], &patients[best])) {
                    best = i;
                }
            } else if (patients[i].state == PROCESS_NEW &&
                       (next_arrival < 0 ||
                        patients[i].arrival_time < next_arrival)) {
                next_arrival = patients[i].arrival_time;
            }
        }

        if (best < 0) {
            if (next_arrival >= 0) {
                free_at[d] = next_arrival;
            } else if (admission.size > 0) {
                int next_completion = -1;
                for (int k = 0; k < NUM_DOCTORS; k++) {
                    if (free_at[k] > t &&
                        (next_completion < 0 || free_at[k] < next_completion)) {
                        next_completion = free_at[k];
                    }
                }
                if (next_completion >= 0) {
                    free_at[d] = next_completion;
                } else {
                    return 1;
                }
            } else {
                return 1;
            }
            continue;
        }

        /* Instrumentation: queue length just before this dispatch. */
        stats_note_queue_len(waiting);
        stats_note_dispatch();
        stats_note_run(d, patients[best].id, t,
                       t + patients[best].service_time);

        patients[best].first_start_time = t;
        patients[best].completion_time = t + patients[best].service_time;
        patients[best].remaining_time = 0;
        patients[best].state = PROCESS_RUNNING;
        free_at[d] = patients[best].completion_time;
        scheduled[best] = 1;
        done++;
    }

    for (int i = 0; i < n; i++) {
        if (patients[i].memory_slots > 0) {
            memory_deallocate(&memory, &patients[i]);
        }
        patients[i].state = PROCESS_TERMINATED;
    }
    return 0;
}