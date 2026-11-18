#include <string.h>
#include "scheduler.h"

/*
 * Text Gantt chart, one line per doctor:
 *   D1 | P1 [0,3) | P2 [3,5) | idle [5,1000) | P3 [1000,1003)
 *
 * Intervals are half-open: [start,end) = busy from start up to end.
 * The schedulers record one segment per simulation step, so a patient who
 * keeps running across several steps appears as several adjacent segments;
 * those are merged here. Gaps between blocks are printed as idle.
 */

/* Print doctor d's timeline, merging adjacent segments of one patient. */
static void print_doctor_line(FILE *out, int d)
{
    char id[MAX_ID_LENGTH] = "";
    int start = 0;
    int end = 0;
    int open = 0;          /* is a block currently being built? */
    int cursor = 0;        /* time up to which the line is already printed */

    fprintf(out, "D%d", d + 1);

    for (int i = 0; i < g_stats.segment_count; i++) {
        const RunSegment *s = &g_stats.segments[i];

        if (s->doctor != d) {
            continue;
        }

        if (open && s->start == end && strcmp(s->id, id) == 0) {
            end = s->end;                    /* same patient, keep going */
            continue;
        }

        if (open) {                          /* flush the finished block */
            fprintf(out, " | %s [%d,%d)", id, start, end);
            cursor = end;
        }

        if (s->start > cursor) {             /* gap before this block */
            fprintf(out, " | idle [%d,%d)", cursor, s->start);
        }

        snprintf(id, sizeof(id), "%s", s->id);
        start = s->start;
        end = s->end;
        open = 1;
    }

    if (open) {
        fprintf(out, " | %s [%d,%d)", id, start, end);
    } else {
        fprintf(out, " | idle");
    }

    fprintf(out, "\n");
}

void print_gantt(FILE *out)
{
    fprintf(out, "GANTT\n");

    for (int d = 0; d < NUM_DOCTORS; d++) {
        print_doctor_line(out, d);
    }

    if (g_stats.segments_dropped) {
        fprintf(out, "(chart truncated: more than %d segments)\n",
                MAX_SEGMENTS);
    }
}
