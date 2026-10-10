#include <stdio.h>
#include <stdlib.h> 
#include <stdbool.h>
#include <string.h>

#define MAX_PATIENTS 1000
#define MAX_ID_LENGTH 32
#define NUM_DOCTOR 2

typedef struct{
    char id[MAX_ID_LENGTH];
    char category[MAX_ID_LENGTH];

    int arrival_time;
    int priority;
    int service_time; 
    int remaining_time; 

    int first_start_time; 
    int completion_time;
} Patient;

typedef struct{
    Patient *items[MAX_PATIENTS];
    int size;
} PriorityQueue; 

bool higher_priority (const Patient *a, const Patient *b){
    if (a-> priority != b-> priority){
        return a-> priority < b-> priority; 
    }
    if (a-> arrival_time != b-> arrival_time){
        return a-> arrival_time < b-> arrival_time;
    }
    return strcmp (a-> id, b-> id) <0;
}

void swap_patients(Patient **a, Patient **b){
    Patient *temp = *a; 
    *a = *b; 
    *b = temp; 
}
int init_queue (PriorityQueue *q){
    q-> size = 0;
}
bool is_empty (const PriorityQueue *q){
    return q-> size == 0;
}
bool push (PriorityQueue *q, Patient *p){
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
Patient *peek(const PriorityQueue *q){
    if (is_empty(q)){
        return NULL; 
    }
    return q-> items[0];
}

Patient *pop(PriorityQueue *q){
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
int choose_preemption_victim(Patient *running[NUM_DOCTOR]) {
    int victim = -1;

    for (int d = 0; d < NUM_DOCTOR; d++) {
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
int next_event_time(
    Patient patients[],
    int n, 
    int next_arrival, 
    Patient *running[NUM_DOCTOR], 
    int current_time){
        int next_time = -1;
        /* Next patient arrival, must be sorted by arrival time*/
        if (next_arrival <n){
            next_time = patients[next_arrival].arrival_time;
        }
        for (int d= 0; d<NUM_DOCTOR; d++){
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

int compare_arrivals(const void *x, const void *y) {
    const Patient *a = x;
    const Patient *b = y;

    if (a->arrival_time < b->arrival_time) return -1;
    if (a->arrival_time > b->arrival_time) return 1;

    return strcmp(a->id, b->id);
}


void simulate_preemptive_priority(Patient patients[], int n) {
    PriorityQueue ready;
    init_queue(&ready);

    Patient *running[NUM_DOCTOR] = {NULL, NULL};

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
            return;
        }

        int elapsed = event_time - current_time;

        /* Update the remaining treatment of running patients. */
        for (int d = 0; d < NUM_DOCTOR; d++) {
            if (running[d] != NULL) {
                running[d]->remaining_time -= elapsed;
            }
        }

        current_time = event_time;

        /* Finish patients whose treatment has ended. */
        for (int d = 0; d < NUM_DOCTOR; d++) {
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
                return;
            }

            next_arrival++;
        }

        /* Give idle doctors the highest-priority ready patients. */
        for (int d = 0; d < NUM_DOCTOR; d++) {
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
                return;
            }

            running[victim] = pop(&ready);

            if (running[victim]->first_start_time == -1) {
                running[victim]->first_start_time = current_time;
            }
        }
    }
}


/* Remove whitespace from the beginning and end of a string. */
void trim(char *s) {
    char *start = s;

    while (*start == ' ' || *start == '\t')
        start++;

    if (start != s)
        memmove(s, start, strlen(start) + 1);

    size_t len = strlen(s);

    while (len > 0 &&
           (s[len - 1] == ' ' || s[len - 1] == '\t' ||
            s[len - 1] == '\r' || s[len - 1] == '\n')) {
        s[--len] = '\0';
    }
}

int read_input(
    const char *filename,
    Patient patients[],
    int *n,
    int *quantum
) {
    FILE *file = fopen(filename, "r");

    if (file == NULL) {
        fprintf(stderr, "Error: cannot open input file.\n");
        return 0;
    }

    if (fscanf(file, "%d", n) != 1 ||
        *n < 1 || *n > MAX_PATIENTS) {
        fprintf(stderr, "Error: invalid patient count.\n");
        fclose(file);
        return 0;
    }

    if (fscanf(file, "%d", quantum) != 1 || *quantum <= 0) {
        fprintf(stderr, "Error: invalid Round Robin quantum.\n");
        fclose(file);
        return 0;
    }

    /* Consume the remainder of the line after the quantum. */
    int ch;
    while ((ch = fgetc(file)) != '\n' && ch != EOF) {}

    char line[256];
    int count = 0;
    int line_number = 2;

    while (count < *n && fgets(line, sizeof(line), file) != NULL) {
        line_number++;

        trim(line);

        /* Permit blank lines between records. */
        if (line[0] == '\0')
            continue;

        Patient p = {0};

        int fields = sscanf(
            line,
            "%31[^,],%d,%d,%d,%31[^,\r\n]",
            p.id,
            &p.arrival_time,
            &p.priority,
            &p.service_time,
            p.category
        );

        if (fields != 5) {
            fprintf(stderr,
                    "Error: invalid patient record at line %d.\n",
                    line_number);
            fclose(file);
            return 0;
        }

        trim(p.id);
        trim(p.category);

        if (p.id[0] == '\0' || p.category[0] == '\0' ||
            p.arrival_time < 0 || p.priority <= 0 ||
            p.service_time <= 0) {
            fprintf(stderr,
                    "Error: invalid values at line %d.\n",
                    line_number);
            fclose(file);
            return 0;
        }

        /* Reject duplicate patient IDs. */
        for (int i = 0; i < count; i++) {
            if (strcmp(patients[i].id, p.id) == 0) {
                fprintf(stderr, "Error: duplicate patient ID.\n");
                fclose(file);
                return 0;
            }
        }

        p.remaining_time = p.service_time;
        p.first_start_time = -1;
        p.completion_time = -1;

        patients[count++] = p;
    }

    fclose(file);

    if (count != *n) {
        fprintf(stderr,
                "Error: expected %d patient records, found %d.\n",
                *n, count);
        return 0;
    }

    return 1;
}

int compare_ids(const void *x, const void *y) {
    const Patient *a = x;
    const Patient *b = y;
    return strcmp(a->id, b->id);
}

void print_results(Patient patients[], int n) {
    double total_waiting = 0;
    double total_turnaround = 0;
    double total_response = 0;
    double total_service = 0;

    int first_arrival = patients[0].arrival_time;
    int last_completion = patients[0].completion_time;

    for (int i = 0; i < n; i++) {
        Patient *p = &patients[i];

        int turnaround =
            p->completion_time - p->arrival_time;

        int waiting =
            turnaround - p->service_time;

        int response =
            p->first_start_time - p->arrival_time;

        total_waiting += waiting;
        total_turnaround += turnaround;
        total_response += response;
        total_service += p->service_time;

        if (p->arrival_time < first_arrival)
            first_arrival = p->arrival_time;

        if (p->completion_time > last_completion)
            last_completion = p->completion_time;
    }

    /* The required output order is lexicographic patient ID. */
    qsort(patients, n, sizeof(Patient), compare_ids);
    printf("%-10s %7s %9s %12s %8s %8s %8s %10s %8s\n",
       "ID", "Arrival", "Priority", "Service", "Start",
       "Finish", "Waiting", "Turnaround", "Response");

printf("--------------------------------------------------------------------------------\n");
    for (int i = 0; i < n; i++) {
        Patient *p = &patients[i];

        int turnaround =
            p->completion_time - p->arrival_time;

        int waiting =
            turnaround - p->service_time;

        int response =
            p->first_start_time - p->arrival_time;
        printf("%-10s %7d %9d %12d %8d %8d %8d %10d %8d\n",
       p->id,
       p->arrival_time,
       p->priority,
       p->service_time,
       p->first_start_time,
       p->completion_time,
       waiting,
       turnaround,
       response);
    }

    double elapsed = last_completion - first_arrival;

    double throughput = elapsed > 0 ? n / elapsed : 0.0;

    double utilization = elapsed > 0
        ? (total_service / (NUM_DOCTOR * elapsed)) * 100.0
        : 0.0;

    printf("SUMMARY\n");
    printf("AVERAGE_WAITING:  %.2f\n", total_waiting / n);
    printf("AVERAGE_TURNAROUND: %.2f\n", total_turnaround / n);
    printf("AVERAGE_RESPONSE: %.2f\n", total_response / n);
    printf("THROUGHPUT: %.6f\n", throughput);
    printf("UTILIZATION: %.2f\n", utilization);
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr,
                "Usage: %s INPUT_FILE ALGORITHM\n",
                argv[0]);
        return 1;
    }

    if (strcmp(argv[2], "PRIORITY_PREEMPTIVE") != 0) {
        fprintf(stderr,
                "Error: unsupported algorithm: %s\n",
                argv[2]);
        return 1;
    }

    Patient patients[MAX_PATIENTS];
    int n;
    int quantum;

    if (!read_input(argv[1], patients, &n, &quantum))
        return 1;

    /* Process arrivals chronologically. */
    qsort(patients, n, sizeof(Patient), compare_arrivals);

    simulate_preemptive_priority(patients, n);

    /* Do not print results if simulation failed. */
    for (int i = 0; i < n; i++) {
        if (patients[i].completion_time == -1) {
            fprintf(stderr,
                    "Error: simulation did not complete all patients.\n");
            return 1;
        }
    }

    print_results(patients, n);

    return 0;
}
