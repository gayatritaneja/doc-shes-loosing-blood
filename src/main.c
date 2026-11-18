#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "scheduler.h"

/*
 * Smart Hospital CLI entry point.
 *
 * Usage:
 *   ./scheduler INPUT_FILE ALGORITHM [--gantt] [--stats]
 *
 * ALGORITHM is one of:
 *   FCFS                 first-come, first-served
 *   PRIORITY             non-preemptive priority
 *   PRIORITY_PREEMPTIVE  preemptive priority
 *   COMPARE              run all three and print a comparison table
 *   COMPARE_CSV          same comparison as CSV (used by analysis/)
 *
 * --gantt and --stats print extra information to stderr, so stdout stays
 * in the machine-readable format required by the specification.
 */

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

static void print_usage(const char *program)
{
    fprintf(stderr,
            "Usage: %s INPUT_FILE ALGORITHM [--gantt] [--stats]\n"
            "Algorithms: FCFS, PRIORITY, PRIORITY_PREEMPTIVE, "
            "COMPARE, COMPARE_CSV\n",
            program);
}

int main(int argc, char *argv[])
{
    if (argc < 3 || argc > 5) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    const char *input_file = argv[1];
    const char *algorithm_name = argv[2];
    int show_gantt = 0;
    int show_stats = 0;

    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--gantt") == 0) {
            show_gantt = 1;
        } else if (strcmp(argv[i], "--stats") == 0) {
            show_stats = 1;
        } else {
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    int compare_table = strcmp(algorithm_name, "COMPARE") == 0;
    int compare_csv = strcmp(algorithm_name, "COMPARE_CSV") == 0;
    SchedulerFunction scheduler = find_algorithm(algorithm_name);

    if (scheduler == NULL && !compare_table && !compare_csv) {
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

    if (compare_table || compare_csv) {
        return run_compare(patients, n, algorithms, algorithm_count,
                           compare_csv);
    }

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

    if (show_stats) {
        print_stats(stderr);
    }
    if (show_gantt) {
        print_gantt(stderr);
    }
    return EXIT_SUCCESS;
}
