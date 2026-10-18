#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "scheduler.h"

/*
 * Smart Hospital CLI entry point.
 *
 * Usage:
 *   ./program INPUT_FILE ALGORITHM
 *
 * Supported algorithm:
 *   PRIORITY_PREEMPTIVE       (preemptive priority)
 *
 */

typedef int (*SchedulerFunction)(Patient patients[], int n);

typedef struct {
    const char *name;
    SchedulerFunction run;
} AlgorithmEntry;

static const AlgorithmEntry algorithms[] = {
    {"FCFS", simulate_fcfs},
    {"PRIORITY", simulate_priority_nonpreemptive},
    {"PRIORITY_PREEMPTIVE", simulate_priority_preemptive}
};

static const size_t algorithm_count =
    sizeof(algorithms) / sizeof(algorithms[0]);

static SchedulerFunction find_algorithm(const char *name)
{
    for (size_t i = 0; i < algorithm_count; i++) {
        if (strcmp(name, algorithms[i].name) == 0) {
            return algorithms[i].run;
        }
    }

    return NULL;
}

int main(int argc, char *argv[])
{
    if (argc != 3) {
        fprintf(stderr,
                "Usage: %s INPUT_FILE ALGORITHM\n"
                "Supported algorithm: PRIORITY_PREEMPTIVE\n",
                argv[0]);
        return EXIT_FAILURE;
    }

    const char *input_file = argv[1];
    const char *algorithm_name = argv[2];

    SchedulerFunction scheduler = find_algorithm(algorithm_name);
    if (scheduler == NULL) {
        fprintf(stderr, "Error: unsupported algorithm: %s\n",
                algorithm_name);
        return EXIT_FAILURE;
    }

    Patient patients[MAX_PATIENTS];
    int n = 0;
    int quantum = 0;

    if (!read_input(input_file, patients, &n, &quantum)) {
        /* The parser should write diagnostics to stderr, not stdout. */
        return EXIT_FAILURE;
    }

    /*
     * The quantum is retained in the shared input interface for algorithms
     * that may use time slicing; priority scheduling does not use it.
     */
    (void)quantum;

    if (scheduler(patients, n) != 0) {
        fprintf(stderr, "Error: scheduling algorithm failed.\n");
        return EXIT_FAILURE;
    }

    /*
     * Guard against accidentally printing incomplete simulation results.
     * A valid simulation should set completion_time for every patient.
     */
    for (int i = 0; i < n; i++) {
        if (patients[i].completion_time < 0 ||
            patients[i].first_start_time < 0) {
            fprintf(stderr,
                    "Error: simulation did not complete all patients.\n");
            return EXIT_FAILURE;
        }
    }

    print_results(patients, n);
    return EXIT_SUCCESS;
}
