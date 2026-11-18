#include <string.h>
#include "scheduler.h"

/*
 * Counters filled in by the scheduling algorithms while they simulate.
 * Reset at the start of every simulation so each run starts from zero.
 */
SimStats g_stats;

void stats_reset(void)
{
    memset(&g_stats, 0, sizeof(g_stats));
}

void stats_note_dispatch(void)
{
    g_stats.dispatches++;
}

void stats_note_preemption(void)
{
    g_stats.preemptions++;
}

void stats_note_queue_len(int len)
{
    if (len > g_stats.max_ready_len) {
        g_stats.max_ready_len = len;
    }
}

/* Record that `doctor` treated patient `id` during [start, end). */
void stats_note_run(int doctor, const char *id, int start, int end)
{
    if (end <= start || doctor < 0 || doctor >= NUM_DOCTORS) {
        return;
    }

    g_stats.busy_time[doctor] += end - start;

    if (g_stats.segment_count >= MAX_SEGMENTS) {
        g_stats.segments_dropped = 1;
        return;
    }

    RunSegment *s = &g_stats.segments[g_stats.segment_count++];
    s->doctor = doctor;
    s->start = start;
    s->end = end;
    snprintf(s->id, sizeof(s->id), "%s", id);
}

void print_stats(FILE *out)
{
    fprintf(out, "PREEMPTIONS,%d\n", g_stats.preemptions);
    fprintf(out, "DISPATCHES,%d\n", g_stats.dispatches);
    fprintf(out, "MAX_READY_QUEUE,%d\n", g_stats.max_ready_len);

    for (int d = 0; d < NUM_DOCTORS; d++) {
        fprintf(out, "DOCTOR%d_BUSY,%d\n", d + 1, g_stats.busy_time[d]);
    }
}
