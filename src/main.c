#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "scheduler.h"
#include "resource_actions.h"
#include "resource_sim.h"

/*
 * Smart Hospital CLI entry point.
 *
 * Usage:
 *   ./scheduler INPUT_FILE ALGORITHM [--gantt] [--stats] [--trace] [--final]
 *               [--memory FIRST_FIT|BEST_FIT|WORST_FIT] [--unsafe]
 *   ./scheduler --deadlock-demo
 *
 * ALGORITHM is one of:
 *   FCFS                 first-come, first-served
 *   PRIORITY             non-preemptive priority
 *   PRIORITY_PREEMPTIVE  preemptive priority
 *   COMPARE              run all three and print a comparison table
 *   COMPARE_CSV          same comparison as CSV (used by analysis/)
 *
 * --trace prints the shared-resource event trace (stderr); --final prints
 * the spec Section 9.2 format (also used whenever the input declares
 * resources); --deadlock-demo runs the bounded deadlock demonstration;
 * --unsafe turns deadlock prevention OFF for a bounded integrated demo
 * (the run stops with exit code 2 as soon as a wait-for cycle forms).
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
            "Usage: %s INPUT_FILE ALGORITHM [--gantt] [--stats] "
            "[--trace] [--final] [--unsafe] [--memmap]\n"
            "       %s INPUT_FILE SCHEDULER ALLOCATOR [OUTPUT_FILE]   "
            "(a2 output format)\n"
            "       %s INPUT_FILE ALGORITHM --memory FIRST_FIT|BEST_FIT|WORST_FIT\n"
            "       %s --deadlock-demo\n"
            "Algorithms: FCFS, PRIORITY, PRIORITY_PREEMPTIVE, "
            "COMPARE, COMPARE_CSV, COMPARE_MEMORY, COMPARE_MEMORY_CSV\n"
            "Allocators: FIRST_FIT, BEST_FIT, WORST_FIT\n",
            program, program, program, program);
}

/* FIRST_FIT / BEST_FIT / WORST_FIT -> sets the allocator, returns 1 if ok */
static int set_allocator(const char *name)
{
    if (strcmp(name, "FIRST_FIT") == 0) {
        memory_set_allocation_algorithm(MEMORY_FIRST_FIT);
    } else if (strcmp(name, "BEST_FIT") == 0) {
        memory_set_allocation_algorithm(MEMORY_BEST_FIT);
    } else if (strcmp(name, "WORST_FIT") == 0) {
        memory_set_allocation_algorithm(MEMORY_WORST_FIT);
    } else {
        return 0;
    }
    return 1;
}

int main(int argc, char *argv[])
{
    if (argc == 2 && strcmp(argv[1], "--deadlock-demo") == 0) {
        return run_deadlock_demo();
    }

    if (argc < 3 || argc > 16) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    const char *input_file = argv[1];
    const char *algorithm_name = argv[2];
    int show_gantt = 0;
    int show_stats = 0;
    int final_format = 0;
    int a2_format = 0;              /* allocator given as 3rd word */
    const char *output_file = NULL; /* 4th word: write the result here */

    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--gantt") == 0) {
            show_gantt = 1;
        } else if (strcmp(argv[i], "--stats") == 0) {
            show_stats = 1;
        } else if (strcmp(argv[i], "--trace") == 0) {
            resource_trace_enable(1);
        } else if (strcmp(argv[i], "--unsafe") == 0) {
            rsim_set_unsafe(1);
        } else if (strcmp(argv[i], "--final") == 0) {
            final_format = 1;
        } else if (strcmp(argv[i], "--memmap") == 0) {
            memory_map_enable(1);
        } else if (strcmp(argv[i], "--memory") == 0) {
            i++;
            if (i >= argc || !set_allocator(argv[i])) {
                print_usage(argv[0]);
                return EXIT_FAILURE;
            }
        } else if (argv[i][0] != '-' && !a2_format && i == 3) {
            /* a2 style: INPUT SCHEDULER ALLOCATOR [OUTPUT_FILE] */
            if (!set_allocator(argv[i])) {
                fprintf(stderr, "Error: unknown allocator: %s\n", argv[i]);
                print_usage(argv[0]);
                return EXIT_FAILURE;
            }
            a2_format = 1;
        } else if (argv[i][0] != '-' && a2_format && output_file == NULL) {
            output_file = argv[i];
        } else {
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    int compare_table = strcmp(algorithm_name, "COMPARE") == 0;
    int compare_csv = strcmp(algorithm_name, "COMPARE_CSV") == 0;
    int compare_memory = strcmp(algorithm_name, "COMPARE_MEMORY") == 0;
    int compare_memory_csv = strcmp(algorithm_name, "COMPARE_MEMORY_CSV") == 0;
    SchedulerFunction scheduler = find_algorithm(algorithm_name);

    if (scheduler == NULL && !compare_table && !compare_csv &&
        !compare_memory && !compare_memory_csv) {
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

    if (compare_memory || compare_memory_csv) {
        return run_compare_memory(patients, n, algorithms, algorithm_count,
                                  compare_memory_csv);
    }
    if (compare_table || compare_csv) {
        return run_compare(patients, n, algorithms, algorithm_count,
                           compare_csv);
    }

    int rc = scheduler(patients, n);

    if (rc == ENGINE_DEADLOCK) {
        /* --unsafe demo: the run stopped as soon as the deadlock
         * happened. print who waits for whom, then the checks. */
        fprintf(stderr,
                "deadlock found (prevention is off because of --unsafe). "
                "run again without --unsafe to see it prevented.\n");
        print_deadlock_report(stdout);
        printf("CHECK,EXCLUSIVITY_VIOLATIONS,%d\n",
               g_resource_report.exclusivity_violations);
        printf("CHECK,UNRESOLVED_DEADLOCKS,%d\n",
               g_resource_report.unresolved_deadlocks);
        printf("CHECK,COMPLETED_COUNT,%d\n",
               g_resource_report.completed_count);
        return ENGINE_DEADLOCK;
    }
    if (rc != 0) {
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

    /* OUTPUT_FILE given: point stdout at that file, so the same print
     * functions write the export (nothing else goes into it) */
    if (output_file != NULL && freopen(output_file, "w", stdout) == NULL) {
        fprintf(stderr, "Error: cannot write %s\n", output_file);
        return EXIT_FAILURE;
    }

    if (final_format || g_resource_config.count > 0) {
        print_final_results(patients, n);
    } else if (a2_format) {
        print_a2_results(patients, n);
    } else {
        print_results(patients, n);
    }

    if (show_stats) {
        print_stats(stderr);
    }
    if (show_gantt) {
        print_gantt(stderr);
    }
    return EXIT_SUCCESS;
}
