#include "scheduler.h"

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

    if (n < 0 || n > MAX_PATIENTS) {
        return 1;
    }
    for (int i = 0; i < n; i++) {
        patients[i].remaining_time = patients[i].service_time;
        patients[i].first_start_time = -1;
        patients[i].completion_time = -1;
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

        /* Choose the best patient that has already arrived by time t. */
        int best = -1;
        int next_arrival = -1;
        for (int i = 0; i < n; i++) {
            if (scheduled[i]) {
                continue;
            }
            if (patients[i].arrival_time <= t) {
                if (best < 0 || better(&patients[i], &patients[best])) {
                    best = i;
                }
            } else if (next_arrival < 0 ||
                       patients[i].arrival_time < next_arrival) {
                next_arrival = patients[i].arrival_time;
            }
        }

        if (best < 0) {
            free_at[d] = next_arrival;   /* idle until someone arrives */
            continue;
        }

        patients[best].first_start_time = t;
        patients[best].completion_time = t + patients[best].service_time;
        patients[best].remaining_time = 0;
        free_at[d] = patients[best].completion_time;
        scheduled[best] = 1;
        done++;
    }
    return 0;
}