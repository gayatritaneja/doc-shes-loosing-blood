#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include "scheduler.h"

/*
 * Preemptive-priority scheduling implementation for Smart Hospital.
 *
 * The Patient structure and shared constants are defined in scheduler.h.
 * Input parsing, CLI argument handling, and result formatting belong in
 * the separate io.c and main.c modules.
 */

typedef struct{
    Patient *items[MAX_PATIENTS];
    int size;
} PriorityQueue; 

static bool higher_priority (const Patient *a, const Patient *b){
    if (a-> priority != b-> priority){
        return a-> priority < b-> priority; 
    }
    if (a-> arrival_time != b-> arrival_time){
        return a-> arrival_time < b-> arrival_time;
    }
    return strcmp (a-> id, b-> id) <0;
}

static void swap_patients(Patient **a, Patient **b){
    Patient *temp = *a; 
    *a = *b; 
    *b = temp; 
}
static void init_queue (PriorityQueue *q){
    q-> size = 0;
}
static bool is_empty (const PriorityQueue *q){
    return q-> size == 0;
}
static bool push (PriorityQueue *q, Patient *p){
    if (q-> size >= MAX_PATIENTS){
        return false; 
    }
    int i = q-> size ++; 
    q-> items[i] = p; 
    while (i > 0){
        int parent = (i-1)/2;
        if (!higher_priority(q-> items[i], q-> items[parent])){
            break; 
        }
        swap_patients(&q-> items[i], &q-> items[parent]); 
        i = parent; 
    }
    return true;
}
static Patient *peek(const PriorityQueue *q){
    if (is_empty(q)){
        return NULL; 
    }
    return q-> items[0];
}

static Patient *pop(PriorityQueue *q){
    if(is_empty(q)){
        return NULL;
    }
    Patient *result = q->items[0];
    q-> size--; 

    if (q-> size == 0){
        return result;
    }
    q-> items[0]= q-> items[q-> size];
    int i = 0; 
    while (1){
        int left = 2*i+1; 
        int right = 2*i+2; 
        int best = i; 

        if (left < q-> size && higher_priority(q-> items[left], q-> items[best])){
            best = left;
        }
        if (right < q-> size && higher_priority(q-> items[right], q-> items[best])){
            best = right;
        }
        if (best == i){
            break;
        }
        swap_patients(&q->items[i], &q-> items[best]);
        i = best;
    }
    return result;
}
static int choose_preemption_victim(Patient *running[NUM_DOCTORS]) {
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
static int next_event_time(
    const Patient patients[],
    int n,
    int next_arrival,
    Patient *running[NUM_DOCTORS],
    int current_time)
{
        int next_time = -1;
        /* The patients are sorted by arrival time before simulation. */
        if (next_arrival <n){
            next_time = patients[next_arrival].arrival_time;
        }
        for (int d = 0; d < NUM_DOCTORS; d++) {
            if (running[d] == NULL){
                continue;
            }
            int completion = current_time + running[d]-> remaining_time;

            if(next_time ==-1 || completion <next_time){
                next_time = completion;
            }
        }
    return next_time;
}

static int compare_arrivals(const void *x, const void *y) {
    const Patient *a = x;
    const Patient *b = y;

    if (a->arrival_time < b->arrival_time) return -1;
    if (a->arrival_time > b->arrival_time) return 1;

    return strcmp(a->id, b->id);
}


int simulate_priority_preemptive(Patient patients[], int n) {
    /* Process arrivals chronologically while preserving input records. */
    qsort(patients, n, sizeof(Patient), compare_arrivals);
    PriorityQueue ready;
    init_queue(&ready);

    Patient *running[NUM_DOCTORS] = {NULL};

    int current_time = 0;
    int next_arrival = 0;
    int completed = 0;

    while (completed < n) {
        /*
         * Find the next arrival or completion.
         * If nothing is running and no patients are ready,
         * this jumps forward to the next arrival.
         */
        int event_time = next_event_time(
            patients, n, next_arrival,
            running, current_time
        );

        if (event_time == -1) {
            fprintf(stderr, "Simulation error: no future event.\n");
            return 1;
        }

        int elapsed = event_time - current_time;

        /* Update the remaining treatment of running patients. */
        for (int d = 0; d < NUM_DOCTORS; d++) {
            if (running[d] != NULL) {
                running[d]->remaining_time -= elapsed;
            }
        }

        current_time = event_time;

        /* Finish patients whose treatment has ended. */
        for (int d = 0; d < NUM_DOCTORS; d++) {
            if (running[d] != NULL &&
                running[d]->remaining_time == 0) {

                running[d]->completion_time = current_time;
                running[d] = NULL;
                completed++;
            }
        }

        /* Admit EVERY patient arriving at this time. */
        while (next_arrival < n &&
               patients[next_arrival].arrival_time <= current_time) {

            if (!push(&ready, &patients[next_arrival])) {
                fprintf(stderr, "Error: ready queue is full.\n");
                return 1;
            }

            next_arrival++;
        }

        /* Give idle doctors the highest-priority ready patients. */
        for (int d = 0; d < NUM_DOCTORS; d++) {
            if (running[d] == NULL && !is_empty(&ready)) {
                running[d] = pop(&ready);

                if (running[d]->first_start_time == -1) {
                    running[d]->first_start_time = current_time;
                }
            }
        }

        /*
         * Preempt only when the most urgent waiting patient
         * has strictly higher priority than a running patient.
         */
        while (!is_empty(&ready)) {
            int victim = choose_preemption_victim(running);

            if (victim == -1)
                break;

            Patient *waiting = peek(&ready);
            Patient *current = running[victim];

            /* Equal priorities must NOT cause preemption. */
            if (waiting->priority >= current->priority)
                break;

            /* Preserve the interrupted patient's progress. */
            if (!push(&ready, current)) {
                fprintf(stderr, "Error: ready queue is full.\n");
                return 1;
            }

            running[victim] = pop(&ready);

            if (running[victim]->first_start_time == -1) {
                running[victim]->first_start_time = current_time;
            }
        }
    }
    return 0;
}
