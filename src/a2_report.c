#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "scheduler.h"

/*
 * assignment 2 output (a2 spec section 9.2):
 *   PROCESS,ID,ARRIVAL,PRIORITY,SERVICE,START,COMPLETION,WAITING,
 *           TURNAROUND,RESPONSE,MEM_ADMIT,MEM_WAIT,READY_WAIT
 *   8 SUMMARY lines
 *   POOL,SPACE,6,PEAK_OCCUPIED,UTILIZATION_PCT,MAX_EXT_FRAG_SLOTS
 * no header row, sorted by patient id.
 */

/* the memory numbers of one finished run (also used by COMPARE_MEMORY) */
void compute_memory_summary(const Patient patients[], int n,
                            const Summary *sum, MemorySummary *out)
{
    double total_mem_wait = 0;
    double span = (double)(sum->last_completion - sum->first_arrival);

    out->blocked_for_memory = 0;
    for (int i = 0; i < n; i++) {
        int mem_wait = patients[i].memory_admission_time -
                       patients[i].arrival_time;

        total_mem_wait += mem_wait;
        if (mem_wait > 0) {
            out->blocked_for_memory++;   /* didn't get memory on arrival */
        }
    }
    out->avg_memory_wait = n > 0 ? total_mem_wait / n : 0;

    /* pool utilization = (slots used x time, added up) / (6 x span) */
    out->utilization_pct = span > 0
        ? 100.0 * (double)g_memory_report.occupied_slot_time /
              (MEMORY_SLOT_COUNT * span)
        : 0;
    out->peak_occupied = g_memory_report.peak_occupied;
    out->max_external_fragmentation =
        g_memory_report.max_external_fragmentation;
}

static int by_id(const void *a, const void *b)
{
    return strcmp(((const Patient *)a)->id, ((const Patient *)b)->id);
}

void print_a2_results(Patient patients[], int n)
{
    Patient *sorted = malloc((size_t)n * sizeof(Patient));

    if (sorted == NULL) {
        fprintf(stderr, "Error: out of memory while printing results.\n");
        return;
    }
    memcpy(sorted, patients, (size_t)n * sizeof(Patient));
    qsort(sorted, (size_t)n, sizeof(Patient), by_id);

    for (int i = 0; i < n; i++) {
        const Patient *p = &sorted[i];
        int turnaround = p->completion_time - p->arrival_time;
        int waiting = turnaround - p->service_time;
        int mem_wait = p->memory_admission_time - p->arrival_time;

        printf("PROCESS,%s,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n",
               p->id, p->arrival_time, p->priority, p->service_time,
               p->first_start_time, p->completion_time, waiting, turnaround,
               p->first_start_time - p->arrival_time,
               p->memory_admission_time, mem_wait, waiting - mem_wait);
    }

    Summary sum;
    MemorySummary mem;

    compute_summary(sorted, n, &sum);
    compute_memory_summary(sorted, n, &sum, &mem);

    printf("SUMMARY,AVERAGE_WAITING,%.4f\n", sum.avg_waiting);
    printf("SUMMARY,AVERAGE_TURNAROUND,%.4f\n", sum.avg_turnaround);
    printf("SUMMARY,AVERAGE_RESPONSE,%.4f\n", sum.avg_response);
    printf("SUMMARY,THROUGHPUT,%.4f\n", sum.throughput);
    printf("SUMMARY,SERVICE_UTILIZATION_PCT,%.4f\n", sum.utilization);
    printf("SUMMARY,AVERAGE_MEMORY_WAIT,%.4f\n", mem.avg_memory_wait);
    printf("SUMMARY,COMPLETED_COUNT,%d\n", n);
    printf("SUMMARY,BLOCKED_FOR_MEMORY_COUNT,%d\n", mem.blocked_for_memory);
    printf("POOL,SPACE,%d,%d,%.4f,%d\n", MEMORY_SLOT_COUNT,
           mem.peak_occupied, mem.utilization_pct,
           mem.max_external_fragmentation);

    free(sorted);
}
