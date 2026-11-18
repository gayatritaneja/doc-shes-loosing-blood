#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "scheduler.h"

/*
 * COMPARE mode: run every algorithm on the same input and print the
 * spec metrics next to the scheduler statistics.
 *
 * Every simulation sorts and modifies the patient array, so each algorithm
 * gets its own copy of the freshly parsed input.
 */

typedef struct
{
    const char *name;
    Summary summary;
    SimStats *stats;               /* heap copy of g_stats after the run */
    int longest_wait;
    char longest_wait_id[MAX_ID_LENGTH];
    int busy_total;
} CompareRow;

/* Find the patient with the largest waiting time (ties: smaller ID). */
static void find_longest_wait(const Patient patients[], int n,
                              CompareRow *row)
{
    row->longest_wait = -1;
    row->longest_wait_id[0] = '\0';

    for (int i = 0; i < n; i++) {
        int waiting = patients[i].completion_time -
                      patients[i].arrival_time -
                      patients[i].service_time;

        if (waiting > row->longest_wait ||
            (waiting == row->longest_wait &&
             strcmp(patients[i].id, row->longest_wait_id) < 0)) {
            row->longest_wait = waiting;
            snprintf(row->longest_wait_id, sizeof(row->longest_wait_id),
                     "%s", patients[i].id);
        }
    }
}

/* Run one algorithm on a private copy of the input and fill in its row. */
static int run_one(const Patient original[], int n,
                   const AlgorithmEntry *algorithm, CompareRow *row)
{
    Patient *work = malloc((size_t)n * sizeof(Patient));

    if (work == NULL) {
        return 0;
    }
    memcpy(work, original, (size_t)n * sizeof(Patient));

    if (algorithm->run(work, n) != 0) {
        free(work);
        return 0;
    }

    row->name = algorithm->name;
    compute_summary(work, n, &row->summary);
    find_longest_wait(work, n, row);

    row->stats = malloc(sizeof(SimStats));
    if (row->stats == NULL) {
        free(work);
        return 0;
    }
    memcpy(row->stats, &g_stats, sizeof(SimStats));

    row->busy_total = 0;
    for (int d = 0; d < NUM_DOCTORS; d++) {
        row->busy_total += row->stats->busy_time[d];
    }

    /* Sanity check: busy time seen by the doctors must equal total service. */
    int service_total = 0;
    for (int i = 0; i < n; i++) {
        service_total += work[i].service_time;
    }
    if (service_total != row->busy_total) {
        fprintf(stderr,
                "Warning: %s busy time %d != total service %d.\n",
                algorithm->name, row->busy_total, service_total);
    }

    free(work);
    return 1;
}

static void print_csv(const CompareRow rows[], size_t count)
{
    printf("algorithm,avg_waiting,avg_turnaround,avg_response,throughput,"
           "utilization,preemptions,dispatches,max_ready_queue,"
           "longest_wait,longest_wait_id");
    for (int d = 0; d < NUM_DOCTORS; d++) {
        printf(",doctor%d_busy", d + 1);
    }
    printf("\n");

    for (size_t i = 0; i < count; i++) {
        const CompareRow *r = &rows[i];

        printf("%s,%.2f,%.2f,%.2f,%.4f,%.2f,%d,%d,%d,%d,%s",
               r->name, r->summary.avg_waiting, r->summary.avg_turnaround,
               r->summary.avg_response, r->summary.throughput,
               r->summary.utilization, r->stats->preemptions,
               r->stats->dispatches, r->stats->max_ready_len,
               r->longest_wait, r->longest_wait_id);
        for (int d = 0; d < NUM_DOCTORS; d++) {
            printf(",%d", r->stats->busy_time[d]);
        }
        printf("\n");
    }
}

static void print_table(const CompareRow rows[], size_t count)
{
    printf("== Spec metrics ==\n");
    printf("%-22s %8s %8s %8s %10s %8s\n", "Algorithm", "AvgWait",
           "AvgTAT", "AvgResp", "Throughput", "Util%");
    for (size_t i = 0; i < count; i++) {
        const Summary *s = &rows[i].summary;
        printf("%-22s %8.2f %8.2f %8.2f %10.4f %8.2f\n", rows[i].name,
               s->avg_waiting, s->avg_turnaround, s->avg_response,
               s->throughput, s->utilization);
    }

    printf("\n== Scheduler activity ==\n");
    printf("%-22s %8s %9s %8s %12s", "Algorithm", "Preempt", "Dispatch",
           "MaxQueue", "LongestWait");
    for (int d = 0; d < NUM_DOCTORS; d++) {
        printf(" %6s%d", "D", d + 1);
    }
    printf("   (D# = busy time units)\n");

    for (size_t i = 0; i < count; i++) {
        const CompareRow *r = &rows[i];
        char longest[48];

        snprintf(longest, sizeof(longest), "%d (%s)", r->longest_wait,
                 r->longest_wait_id);
        printf("%-22s %8d %9d %8d %12s", r->name, r->stats->preemptions,
               r->stats->dispatches, r->stats->max_ready_len, longest);
        for (int d = 0; d < NUM_DOCTORS; d++) {
            printf(" %7d", r->stats->busy_time[d]);
        }
        printf("\n");
    }
}

int run_compare(const Patient original[], int n,
                const AlgorithmEntry algorithms[], size_t count, int csv)
{
    CompareRow *rows = calloc(count, sizeof(CompareRow));
    int ok = rows != NULL;

    for (size_t i = 0; ok && i < count; i++) {
        ok = run_one(original, n, &algorithms[i], &rows[i]);
    }

    if (ok) {
        if (csv) {
            print_csv(rows, count);
        } else {
            print_table(rows, count);
        }
    }

    if (rows != NULL) {
        for (size_t i = 0; i < count; i++) {
            free(rows[i].stats);
        }
    }
    free(rows);
    return ok ? 0 : 1;
}
