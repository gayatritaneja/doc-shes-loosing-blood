#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "scheduler.h"

#define INPUT_LINE_SIZE 1024
#define NUM_FIELDS 5

/*
 * Input format:
 * ID,arrival_time,priority,service_time,category
 *
 * Example:
 * P001,0,2,5,Emergency
 * P002,1,1,3,Critical
 *
 * Whitespace-separated input is also supported.
 * Blank lines and lines beginning with # are ignored.
 *
 * Lower numeric priority means higher priority.
 */

static char *trim(char *s)
{
    while (isspace((unsigned char)*s)) {
        s++;
    }

    char *end = s + strlen(s);

    while (end > s && isspace((unsigned char)end[-1])) {
        *--end = '\0';
    }

    return s;
}

static int parse_integer(const char *text, int *value)
{
    char *end;
    long number;

    if (text == NULL || *text == '\0') {
        return 0;
    }

    errno = 0;
    number = strtol(text, &end, 10);

    while (isspace((unsigned char)*end)) {
        end++;
    }

    if (errno != 0 || end == text || *end != '\0' ||
        number < 0 || number > INT_MAX) {
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

    if (strchr(p, ',') != NULL) {
        char *start = p;

        while (1) {
            if (count >= capacity) {
                return -1;
            }

            char *comma = strchr(start, ',');

            if (comma != NULL) {
                *comma = '\0';
            }

            fields[count++] = trim(start);

            if (comma == NULL) {
                break;
            }

            start = comma + 1;
        }
    } else {
        char *token = strtok(p, " \t\r\n");

        while (token != NULL) {
            if (count >= capacity) {
                return -1;
            }

            fields[count++] = token;
            token = strtok(NULL, " \t\r\n");
        }
    }

    return count;
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

    if (filename == NULL || patients == NULL ||
        n == NULL || quantum == NULL) {
        fprintf(stderr, "Error: invalid input arguments.\n");
        return 0;
    }

    file = fopen(filename, "r");

    if (file == NULL) {
        perror(filename);
        return 0;
    }

    *n = 0;
    *quantum = 0;

    while (fgets(line, sizeof(line), file) != NULL) {
        char *content;
        char *fields[NUM_FIELDS];
        int has_newline = strchr(line, '\n') != NULL;
        int field_count;
        int arrival;
        int priority;
        int service_time;

        line_number++;
        content = trim(line);

        /* Ignore blank lines and comments. */
        if (*content == '\0' || *content == '#') {
            continue;
        }

        /* Detect lines that exceed the buffer size without rejecting EOF. */
        if (!has_newline) {
            int next_character = fgetc(file);

            if (next_character != EOF) {
                fprintf(stderr,
                        "%s:%d: input line is too long.\n",
                        filename, line_number);
                fclose(file);
                return 0;
            }
        }

        if (header_value_count < 2) {
            int header_value;

            if (!parse_integer(content, &header_value)) {
                fprintf(stderr,
                        "%s:%d: expected %s header value.\n",
                        filename, line_number,
                        header_value_count == 0
                            ? "patient count"
                            : "quantum");
                fclose(file);
                return 0;
            }

            if (header_value_count == 0) {
                expected_count = header_value;
            } else {
                *quantum = header_value;
            }

            header_value_count++;
            continue;
        }

        field_count = split_fields(
            content, fields, NUM_FIELDS
        );

        if (field_count != NUM_FIELDS) {
            fprintf(stderr,
                    "%s:%d: expected ID,arrival_time,priority,"
                    "service_time,category.\n",
                    filename, line_number);
            fclose(file);
            return 0;
        }

        if (fields[0][0] == '\0' ||
            strlen(fields[0]) >= sizeof(patients[0].id)) {
            fprintf(stderr,
                    "%s:%d: invalid or overly long patient ID.\n",
                    filename, line_number);
            fclose(file);
            return 0;
        }

        if (count >= MAX_PATIENTS) {
            fprintf(stderr,
                    "Error: maximum of %d patients exceeded.\n",
                    MAX_PATIENTS);
            fclose(file);
            return 0;
        }

        if (!parse_integer(fields[1], &arrival) ||
            !parse_integer(fields[2], &priority) ||
            !parse_integer(fields[3], &service_time) ||
            fields[4][0] == '\0' ||
            strlen(fields[4]) >= sizeof(patients[0].category) ||
            service_time == 0) {
            fprintf(stderr,
                    "%s:%d: arrival_time and priority must be "
                    "non-negative integers; service_time must be "
                    "positive and category must not be empty or "
                    "overly long.\n",
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

        /* These values indicate that scheduling has not started. */
        p->first_start_time = -1;
        p->completion_time = -1;

        count++;
    }

    if (ferror(file)) {
        fprintf(stderr,
                "Error: failed while reading %s.\n",
                filename);
        fclose(file);
        return 0;
    }

    fclose(file);

    if (count == 0 || expected_count == 0) {
        fprintf(stderr, "Error: input file contains no patients.\n");
        return 0;
    }

    if (expected_count != count) {
        fprintf(stderr,
                "%s: header declares %d patients, but %d were read.\n",
                filename, expected_count, count);
        return 0;
    }

    *n = count;
    return 1;
}

void print_results(Patient patients[], int n)
{
    printf("id,arrival_time,priority,service_time,category,"
           "first_start_time,completion_time,"
           "turnaround_time,waiting_time,response_time\n");

    for (int i = 0; i < n; i++) {
        int turnaround =
            patients[i].completion_time -
            patients[i].arrival_time;

        int waiting =
            turnaround - patients[i].service_time;

        int response =
            patients[i].first_start_time -
            patients[i].arrival_time;

        printf("%s,%d,%d,%d,%s,%d,%d,%d,%d,%d\n",
               patients[i].id,
               patients[i].arrival_time,
               patients[i].priority,
               patients[i].service_time,
               patients[i].category,
               patients[i].first_start_time,
               patients[i].completion_time,
               turnaround,
               waiting,
               response);
    }
}
