#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "scheduler.h"
#include "resource_actions.h"

#define INPUT_LINE_SIZE 1024
#define NUM_FIELDS 7

/*
 * Input format (spec, 7 fields):
 * ID,arrival_time,priority,service_time,category,SPACE_REQ,TABLE_REQ
 * TABLE_REQ is only used by the restaurant, so for the hospital it must
 * be 0. the older 5-field (a1) and 6-field rows are still accepted.
 *
 * Example:
 * P001,0,2,5,Emergency,2
 * P002,1,1,3,Critical,1
 *
 * Whitespace-separated input is also supported.
 * Blank lines and lines beginning with # are ignored.
 *
 * Lower numeric priority means higher priority.
 */

static char *trim(char *s)
{
    while (isspace((unsigned char)*s))
    {
        s++;
    }

    char *end = s + strlen(s);

    while (end > s && isspace((unsigned char)end[-1]))
    {
        *--end = '\0';
    }

    return s;
}

static int parse_integer(const char *text, int *value)
{
    char *end;
    long number;

    if (text == NULL || *text == '\0')
    {
        return 0;
    }

    errno = 0;
    number = strtol(text, &end, 10);

    while (isspace((unsigned char)*end))
    {
        end++;
    }

    if (errno != 0 || end == text || *end != '\0' ||
        number < 0 || number > INT_MAX)
    {
        return 0;
    }

    *value = (int)number;
    return 1;
}

/*
 * Split a line into five fields.
 * Supports either comma-separated or whitespace-separated input.
 */
static int split_fields(char *line, char *fields[], int capacity)
{
    int count = 0;
    char *p = trim(line);

    if (strchr(p, ',') != NULL)
    {
        char *start = p;

        while (1)
        {
            if (count >= capacity)
            {
                return -1;
            }

            char *comma = strchr(start, ',');

            if (comma != NULL)
            {
                *comma = '\0';
            }

            fields[count++] = trim(start);

            if (comma == NULL)
            {
                break;
            }

            start = comma + 1;
        }
    }
    else
    {
        char *token = strtok(p, " \t\r\n");

        while (token != NULL)
        {
            if (count >= capacity)
            {
                return -1;
            }

            fields[count++] = token;
            token = strtok(NULL, " \t\r\n");
        }
    }

    return count;
}

/*
 * Optional sections after the N process rows (spec Section 8):
 *   R / R rows "RESOURCE_ID,CAPACITY" / K / K rows
 *   "ID,WORK_OFFSET,ACTION,RESOURCE_ID,UNITS".
 * Stages: 0 expect R, 1 resource rows, 2 expect K, 3 action rows, 4 done.
 */
typedef struct {
    int stage;
    int expected;
    int seen;
} ExtraState;

static int extra_error(const char *filename, int line, const char *msg)
{
    fprintf(stderr, "%s:%d: %s\n", filename, line, msg);
    return 0;
}

static int parse_extra_line(ExtraState *st, char *content,
                            Patient patients[], int count,
                            const char *filename, int line)
{
    char *fields[5];

    switch (st->stage) {
    case 0:
    case 2:
        if (!parse_integer(content, &st->expected)) {
            return extra_error(filename, line,
                st->stage == 0 ? "expected resource count R."
                               : "expected action count K.");
        }
        st->seen = 0;
        if (st->expected == 0) {
            st->stage = st->stage == 0 ? 2 : 4;
        } else {
            st->stage++;
        }
        return 1;

    case 1:
        if (split_fields(content, fields, 5) != 2) {
            return extra_error(filename, line,
                               "expected RESOURCE_ID,CAPACITY.");
        }
        {
            int capacity;

            if (!parse_integer(fields[1], &capacity) || capacity < 1 ||
                fields[0][0] == '\0' ||
                resource_config_add(fields[0], capacity) != 0) {
                return extra_error(filename, line,
                    "invalid, duplicate or over-long resource, or "
                    "CAPACITY below 1.");
            }
        }
        if (++st->seen == st->expected) {
            st->stage = 2;
        }
        return 1;

    case 3:
        if (split_fields(content, fields, 5) != 5) {
            return extra_error(filename, line,
                "expected ID,WORK_OFFSET,ACTION,RESOURCE_ID,UNITS.");
        }
        {
            Patient *p = NULL;
            int offset;
            int units;
            ResourceActionType type;

            for (int i = 0; i < count; i++) {
                if (strcmp(patients[i].id, fields[0]) == 0) {
                    p = &patients[i];
                }
            }
            if (p == NULL) {
                return extra_error(filename, line,
                                   "action refers to an unknown process.");
            }
            if (strcmp(fields[2], "ACQUIRE") == 0) {
                type = RESOURCE_ACTION_ACQUIRE;
            } else if (strcmp(fields[2], "RELEASE") == 0) {
                type = RESOURCE_ACTION_RELEASE;
            } else {
                return extra_error(filename, line,
                                   "ACTION must be ACQUIRE or RELEASE.");
            }
            if (!resource_config_has(fields[3])) {
                return extra_error(filename, line,
                                   "action uses an undeclared resource.");
            }
            if (!parse_integer(fields[1], &offset) ||
                offset > p->service_time) {
                return extra_error(filename, line,
                    "WORK_OFFSET must be between 0 and SERVICE.");
            }
            if (!parse_integer(fields[4], &units) || units != 1) {
                return extra_error(filename, line, "UNITS must be 1.");
            }
            if (resource_plan_add(&p->resource_plan, offset, type,
                                  fields[3], units) != 0) {
                return extra_error(filename, line,
                    "too many actions, or offsets not in nondecreasing "
                    "order for this process.");
            }
        }
        if (++st->seen == st->expected) {
            st->stage = 4;
        }
        return 1;

    default:
        return extra_error(filename, line, "unexpected extra line.");
    }
}

int read_input(const char *filename,
               Patient patients[],
               int *n,
               int *quantum)
{
    FILE *file;
    char line[INPUT_LINE_SIZE];
    int count = 0;
    int line_number = 0;
    int header_value_count = 0;
    int expected_count = -1;
    ExtraState extra = {0, 0, 0};

    resource_config_reset();

    if (filename == NULL || patients == NULL ||
        n == NULL || quantum == NULL)
    {
        fprintf(stderr, "Error: invalid input arguments.\n");
        return 0;
    }

    file = fopen(filename, "r");

    if (file == NULL)
    {
        perror(filename);
        return 0;
    }

    *n = 0;
    *quantum = 0;

    while (fgets(line, sizeof(line), file) != NULL)
    {
        char *content;
        char *fields[NUM_FIELDS];
        int has_newline = strchr(line, '\n') != NULL;
        int field_count;
        int arrival;
        int priority;
        int service_time;
        int memory_requirement;

        line_number++;
        content = trim(line);

        /* Ignore blank lines and comments. */
        if (*content == '\0' || *content == '#')
        {
            continue;
        }

        /* Detect lines that exceed the buffer size without rejecting EOF. */
        if (!has_newline)
        {
            int next_character = fgetc(file);

            if (next_character != EOF)
            {
                fprintf(stderr,
                        "%s:%d: input line is too long.\n",
                        filename, line_number);
                fclose(file);
                return 0;
            }
        }

        if (header_value_count < 2)
        {
            int header_value;

            if (!parse_integer(content, &header_value))
            {
                fprintf(stderr,
                        "%s:%d: expected %s header value.\n",
                        filename, line_number,
                        header_value_count == 0
                            ? "patient count"
                            : "quantum");
                fclose(file);
                return 0;
            }

            if (header_value_count == 0)
            {
                expected_count = header_value;
            }
            else
            {
                /* a2 spec: Q is a positive number (even without RR) */
                if (header_value < 1)
                {
                    fprintf(stderr,
                            "%s:%d: quantum Q must be a positive number.\n",
                            filename, line_number);
                    fclose(file);
                    return 0;
                }
                *quantum = header_value;
            }

            header_value_count++;
            continue;
        }

        /* All N process rows read: what follows is the R/K sections. */
        if (count >= expected_count)
        {
            if (!parse_extra_line(&extra, content, patients, count,
                                  filename, line_number))
            {
                fclose(file);
                return 0;
            }
            continue;
        }

        field_count = split_fields(
            content, fields, NUM_FIELDS);

        if (field_count < 5 || field_count > NUM_FIELDS)
        {
            fprintf(stderr,
                    "%s:%d: expected ID,arrival_time,priority,"
                    "service_time,category,SPACE_REQ,TABLE_REQ.\n",
                    filename, line_number);
            fclose(file);
            return 0;
        }

        /* a2 spec: ids can't be empty or have spaces in them */
        if (fields[0][0] == '\0' ||
            strpbrk(fields[0], " \t") != NULL ||
            strlen(fields[0]) >= sizeof(patients[0].id))
        {
            fprintf(stderr,
                    "%s:%d: invalid or overly long patient ID.\n",
                    filename, line_number);
            fclose(file);
            return 0;
        }

        /* Spec Section 12.2: process IDs are unique within a file. */
        for (int j = 0; j < count; j++)
        {
            if (strcmp(patients[j].id, fields[0]) == 0)
            {
                fprintf(stderr,
                        "%s:%d: duplicate patient ID %s.\n",
                        filename, line_number, fields[0]);
                fclose(file);
                return 0;
            }
        }

        if (count >= MAX_PATIENTS)
        {
            fprintf(stderr,
                    "Error: maximum of %d patients exceeded.\n",
                    MAX_PATIENTS);
            fclose(file);
            return 0;
        }

        memory_requirement = 1;
        if (field_count >= 6 &&
            !parse_integer(fields[5], &memory_requirement)) {
            memory_requirement = 0;
        }

        /* the hospital has no table pool, so TABLE_REQ must be 0 */
        if (field_count == 7) {
            int table_requirement = -1;

            if (!parse_integer(fields[6], &table_requirement) ||
                table_requirement != 0) {
                fprintf(stderr,
                        "%s:%d: TABLE_REQ must be 0 for the hospital "
                        "(there is no table pool).\n",
                        filename, line_number);
                fclose(file);
                return 0;
            }
        }

        if (!parse_integer(fields[1], &arrival) ||
            !parse_integer(fields[2], &priority) ||
            !parse_integer(fields[3], &service_time) ||
            fields[4][0] == '\0' ||
            strlen(fields[4]) >= sizeof(patients[0].category) ||
            service_time == 0 ||
            priority < 1 ||
            memory_requirement < 1 ||
            memory_requirement > MEMORY_SLOT_COUNT)
        {
            fprintf(stderr,
                    "%s:%d: ARRIVAL must be 0 or more, PRIORITY and "
                    "SERVICE must be positive, SPACE_REQ must be 1-6 "
                    "and CATEGORY must not be empty or overly long.\n",
                    filename, line_number);
            fclose(file);
            return 0;
        }

        Patient *p = &patients[count];

        memset(p, 0, sizeof(*p));

        snprintf(p->id, sizeof(p->id), "%s", fields[0]);
        snprintf(p->category, sizeof(p->category), "%s", fields[4]);

        p->arrival_time = arrival;
        p->priority = priority;
        p->service_time = service_time;
        p->remaining_time = service_time;
        p->memory_requirement = memory_requirement;
        p->memory_start = -1;
        p->memory_slots = 0;
        p->memory_admission_time = -1;
        p->scheduler_queued = 0;
        p->state = PROCESS_NEW;

        /* These values indicate that scheduling has not started. */
        p->first_start_time = -1;
        p->completion_time = -1;

        count++;
    }

    if (ferror(file))
    {
        fprintf(stderr,
                "Error: failed while reading %s.\n",
                filename);
        fclose(file);
        return 0;
    }

    fclose(file);

    if (count == 0 || expected_count == 0)
    {
        fprintf(stderr, "Error: input file contains no patients.\n");
        return 0;
    }

    if (expected_count != count)
    {
        fprintf(stderr,
                "%s: header declares %d patients, but %d were read.\n",
                filename, expected_count, count);
        return 0;
    }

    if (extra.stage == 1 || extra.stage == 3 || extra.stage == 2)
    {
        fprintf(stderr,
                "%s: resource/action section is incomplete "
                "(R and K sections must both be complete).\n", filename);
        return 0;
    }

    for (int i = 0; i < count; i++)
    {
        char why[96];

        if (resource_plan_validate(&patients[i].resource_plan,
                                   why, sizeof(why)) != 0)
        {
            fprintf(stderr, "%s: process %s: %s.\n",
                    filename, patients[i].id, why);
            return 0;
        }
    }

    *n = count;
    return 1;
}

/* Orders patients by ID (lexicographic), as specifiied for the output. */
static int compare_by_id(const void *a, const void *b)
{
    const Patient *pa = a;
    const Patient *pb = b;

    return strcmp(pa->id, pb->id);
}

/* Averages, throughput and utilization exactly as in spec Section 10. */
void compute_summary(const Patient patients[], int n, Summary *out)
{
    double total_waiting = 0.0;
    double total_turnaround = 0.0;
    double total_response = 0.0;
    double busy_time = 0.0;
    int first_arrival = patients[0].arrival_time;
    int last_completion = patients[0].completion_time;

    for (int i = 0; i < n; i++)
    {
        int turnaround = patients[i].completion_time - patients[i].arrival_time;

        total_turnaround += turnaround;
        total_waiting += turnaround - patients[i].service_time;
        total_response +=
            patients[i].first_start_time - patients[i].arrival_time;
        busy_time += patients[i].service_time;

        if (patients[i].arrival_time < first_arrival)
        {
            first_arrival = patients[i].arrival_time;
        }
        if (patients[i].completion_time > last_completion)
        {
            last_completion = patients[i].completion_time;
        }
    }

    double elapsed = (double)(last_completion - first_arrival);

    out->avg_waiting = total_waiting / n;
    out->avg_turnaround = total_turnaround / n;
    out->avg_response = total_response / n;
    out->throughput = elapsed > 0 ? n / elapsed : 0.0;
    out->utilization =
        elapsed > 0 ? busy_time / (NUM_DOCTORS * elapsed) * 100.0 : 0.0;
    out->first_arrival = first_arrival;
    out->last_completion = last_completion;
}

void print_results(Patient patients[], int n)
{
    if (n <= 0)
    {
        return;
    }

    /* Sort a copy so the caller's array keeps its order. */
    Patient *sorted = malloc((size_t)n * sizeof(Patient));

    if (sorted == NULL)
    {
        fprintf(stderr, "Error: out of memory while printing results.\n");
        return;
    }

    memcpy(sorted, patients, (size_t)n * sizeof(Patient));
    qsort(sorted, (size_t)n, sizeof(Patient), compare_by_id);

    printf("PROCESS_ID,ARRIVAL,PRIORITY,SERVICE,START,COMPLETION,"
           "WAITING,TURNAROUND,RESPONSE\n");

    for (int i = 0; i < n; i++)
    {
        int turnaround = sorted[i].completion_time - sorted[i].arrival_time;
        int waiting = turnaround - sorted[i].service_time;
        int response = sorted[i].first_start_time - sorted[i].arrival_time;

        printf("%s,%d,%d,%d,%d,%d,%d,%d,%d\n",
               sorted[i].id,
               sorted[i].arrival_time,
               sorted[i].priority,
               sorted[i].service_time,
               sorted[i].first_start_time,
               sorted[i].completion_time,
               waiting,
               turnaround,
               response);
    }

    Summary sum;
    compute_summary(sorted, n, &sum);

    printf("SUMMARY\n");
    printf("AVERAGE_WAITING,%.2f\n", sum.avg_waiting);
    printf("AVERAGE_TURNAROUND,%.2f\n", sum.avg_turnaround);
    printf("AVERAGE_RESPONSE,%.2f\n", sum.avg_response);
    printf("THROUGHPUT,%.4f\n", sum.throughput);
    printf("UTILIZATION,%.2f\n", sum.utilization);

    free(sorted);
}
