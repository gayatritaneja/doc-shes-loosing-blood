#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "resource_sim.h"

/*
 * Spec Section 9.2 output: PROCESS rows with MEM_WAIT / READY_WAIT /
 * RESOURCE_WAIT, SUMMARY, then RESOURCE and CHECK records.
 * (The A2 POOL record is not produced here.)
 */

static int compare_by_id(const void *a, const void *b)
{
    return strcmp(((const Patient *)a)->id, ((const Patient *)b)->id);
}

void print_final_results(Patient patients[], int n)
{
    if (n <= 0) {
        return;
    }

    Patient *sorted = malloc((size_t)n * sizeof(Patient));

    if (sorted == NULL) {
        fprintf(stderr, "Error: out of memory while printing results.\n");
        return;
    }
    memcpy(sorted, patients, (size_t)n * sizeof(Patient));
    qsort(sorted, (size_t)n, sizeof(Patient), compare_by_id);

    double memory_wait_total = 0.0;
    int blocked_for_memory = 0;
    int completed = 0;

    for (int i = 0; i < n; i++) {
        const Patient *p = &sorted[i];
        int turnaround = p->completion_time - p->arrival_time;
        int waiting = turnaround - p->service_time;
        int mem_wait = p->memory_admission_time - p->arrival_time;
        int ready_wait = waiting - mem_wait - p->resource_wait_time;

        printf("PROCESS,%s,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n",
               p->id, p->arrival_time, p->priority, p->service_time,
               p->first_start_time, p->completion_time, waiting,
               turnaround, p->first_start_time - p->arrival_time,
               p->memory_admission_time, mem_wait, ready_wait,
               p->resource_wait_time);

        memory_wait_total += mem_wait;
        blocked_for_memory += mem_wait > 0;
        completed += p->completion_time >= 0;
    }

    Summary sum;
    compute_summary(sorted, n, &sum);

    printf("SUMMARY,AVERAGE_WAITING,%.4f\n", sum.avg_waiting);
    printf("SUMMARY,AVERAGE_TURNAROUND,%.4f\n", sum.avg_turnaround);
    printf("SUMMARY,AVERAGE_RESPONSE,%.4f\n", sum.avg_response);
    printf("SUMMARY,THROUGHPUT,%.4f\n", sum.throughput);
    printf("SUMMARY,SERVICE_UTILIZATION_PCT,%.4f\n", sum.utilization);
    printf("SUMMARY,AVERAGE_MEMORY_WAIT,%.4f\n", memory_wait_total / n);
    printf("SUMMARY,COMPLETED_COUNT,%d\n", completed);
    printf("SUMMARY,BLOCKED_FOR_MEMORY_COUNT,%d\n", blocked_for_memory);

    for (int i = 0; i < g_resource_report.count; i++) {
        const ResourceReportRow *r = &g_resource_report.rows[i];

        printf("RESOURCE,%s,%d,%d,%d,%d\n", r->id, r->capacity,
               r->max_holders, r->acquisitions, r->wait_events);
    }

    printf("CHECK,EXCLUSIVITY_VIOLATIONS,%d\n",
           g_resource_report.exclusivity_violations);
    printf("CHECK,UNRESOLVED_DEADLOCKS,%d\n",
           g_resource_report.unresolved_deadlocks);
    printf("CHECK,COMPLETED_COUNT,%d\n", g_resource_report.completed_count);

    free(sorted);
}
