/*
 * test.c - automated tests for the hospital scheduler (CS-3710 A1).
 *
 * Build the scheduler first:
 *   gcc -std=c11 -Wall -Wextra -Wpedantic -Werror \
 *     main.c io.c priority_preemptive.c nonpreemptive_core.c fcfs.c priority.c -o scheduler
 *
 * Then build and run this file from the same directory:
 *   gcc -std=c11 -Wall -Wextra -Wpedantic -Werror test.c -o test -lm
 *   ./test           (optionally: ./test path/to/scheduler)
 *
 * Two kinds of checks:
 *   1. Invariants that must hold for ANY correct output (validate()).
 *   2. Hand-computed expected results for small inputs (expect_rows / expect_sum).
 */

#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_P 64
#define TMP_INPUT "tmp_test_input.txt"
#define TOL 0.01
#define SKIP (-1.0)

typedef struct {
    char id[32];
    int arr, pri, svc, start, comp, wait, tat, resp;
} Row;

typedef struct {
    Row rows[MAX_P];
    int n;
    double avgW, avgT, avgR, thr, util;
    int summary_fields;
} Result;

typedef struct {
    const char *id;
    int start, comp, wait, tat, resp;
} Exp;

static const char *exe = "./scheduler";
static int passed = 0, failed = 0;

static void check(int cond, const char *suite, const char *alg, const char *what) {
    if (cond) {
        passed++;
    } else {
        failed++;
        printf("FAIL [%s / %s] %s\n", suite, alg, what);
    }
}

static int close_to(double a, double b) { return fabs(a - b) < TOL; }

/* Write the input text to a temp file, run the scheduler, parse its CSV. */
static int run(const char *text, const char *alg, Result *r) {
    memset(r, 0, sizeof *r);
    FILE *f = fopen(TMP_INPUT, "w");
    if (!f) return 0;
    fputs(text, f);
    fclose(f);

    char cmd[512];
    snprintf(cmd, sizeof cmd, "%s %s %s", exe, TMP_INPUT, alg);
    FILE *p = popen(cmd, "r");
    if (!p) return 0;

    char line[256];
    int in_summary = 0;
    while (fgets(line, sizeof line, p)) {
        if (strncmp(line, "PROCESS_ID", 10) == 0) continue;
        if (strncmp(line, "SUMMARY", 7) == 0) { in_summary = 1; continue; }
        if (!in_summary) {
            if (r->n >= MAX_P) continue;
            Row *w = &r->rows[r->n];
            if (sscanf(line, "%31[^,],%d,%d,%d,%d,%d,%d,%d,%d", w->id, &w->arr, &w->pri,
                       &w->svc, &w->start, &w->comp, &w->wait, &w->tat, &w->resp) == 9)
                r->n++;
        } else {
            double v;
            if (sscanf(line, "AVERAGE_WAITING,%lf", &v) == 1) { r->avgW = v; r->summary_fields++; }
            else if (sscanf(line, "AVERAGE_TURNAROUND,%lf", &v) == 1) { r->avgT = v; r->summary_fields++; }
            else if (sscanf(line, "AVERAGE_RESPONSE,%lf", &v) == 1) { r->avgR = v; r->summary_fields++; }
            else if (sscanf(line, "THROUGHPUT,%lf", &v) == 1) { r->thr = v; r->summary_fields++; }
            else if (sscanf(line, "UTILIZATION,%lf", &v) == 1) { r->util = v; r->summary_fields++; }
        }
    }
    pclose(p);
    return 1;
}

/* Invariants that every correct run must satisfy. */
static void validate(const Result *r, const char *suite, const char *alg, int expected_n,
                     int nonpreemptive) {
    check(r->n == expected_n, suite, alg, "wrong number of process rows");
    check(r->summary_fields == 5, suite, alg, "SUMMARY block missing or incomplete");
    if (r->n != expected_n || r->n == 0) return;

    int min_arr = r->rows[0].arr, max_comp = r->rows[0].comp;
    long busy = 0;
    double sw = 0, st = 0, sr = 0;

    for (int i = 0; i < r->n; i++) {
        const Row *w = &r->rows[i];
        if (i > 0)
            check(strcmp(r->rows[i - 1].id, w->id) < 0, suite, alg,
                  "rows not in strict lexicographic ID order");
        check(w->start >= w->arr, suite, alg, "START before ARRIVAL");
        check(w->comp >= w->start + w->svc, suite, alg, "COMPLETION < START + SERVICE");
        check(w->tat == w->comp - w->arr, suite, alg, "TURNAROUND != COMPLETION - ARRIVAL");
        check(w->wait == w->tat - w->svc, suite, alg, "WAITING != TURNAROUND - SERVICE");
        check(w->resp == w->start - w->arr, suite, alg, "RESPONSE != START - ARRIVAL");
        check(w->wait >= 0, suite, alg, "negative WAITING");
        if (nonpreemptive)
            check(w->comp - w->start == w->svc, suite, alg,
                  "non-preemptive process did not run contiguously");
        if (w->arr < min_arr) min_arr = w->arr;
        if (w->comp > max_comp) max_comp = w->comp;
        busy += w->svc;
        sw += w->wait; st += w->tat; sr += w->resp;
    }

    /* Non-preemptive: never more than 2 processes running at the same instant. */
    if (nonpreemptive) {
        for (int i = 0; i < r->n; i++) {
            int running = 0;
            for (int j = 0; j < r->n; j++)
                if (r->rows[j].start <= r->rows[i].start && r->rows[i].start < r->rows[j].comp)
                    running++;
            check(running <= 2, suite, alg, "more than 2 doctors busy at once");
        }
    }

    double span = (double)(max_comp - min_arr);
    check(close_to(r->avgW, sw / r->n), suite, alg, "AVERAGE_WAITING mismatch");
    check(close_to(r->avgT, st / r->n), suite, alg, "AVERAGE_TURNAROUND mismatch");
    check(close_to(r->avgR, sr / r->n), suite, alg, "AVERAGE_RESPONSE mismatch");
    check(close_to(r->thr, r->n / span), suite, alg, "THROUGHPUT mismatch");
    check(close_to(r->util, busy / (2.0 * span) * 100.0), suite, alg, "UTILIZATION mismatch");
    check(r->util <= 100.0 + TOL, suite, alg, "UTILIZATION > 100");
}

static void expect_rows(const Result *r, const char *suite, const char *alg, const Exp *e, int n) {
    if (r->n != n) { check(0, suite, alg, "row count differs from expected"); return; }
    for (int i = 0; i < n; i++) {
        const Row *w = &r->rows[i];
        char msg[128];
        snprintf(msg, sizeof msg, "%s: expected id/start/comp/wait/tat/resp", e[i].id);
        check(strcmp(w->id, e[i].id) == 0 && w->start == e[i].start && w->comp == e[i].comp &&
                  w->wait == e[i].wait && w->tat == e[i].tat && w->resp == e[i].resp,
              suite, alg, msg);
    }
}

/* Pass SKIP (-1) for any summary value you don't want to check. */
static void expect_sum(const Result *r, const char *suite, const char *alg, double w, double t,
                       double rs, double thr, double util) {
    if (w >= 0) check(close_to(r->avgW, w), suite, alg, "AVERAGE_WAITING != expected");
    if (t >= 0) check(close_to(r->avgT, t), suite, alg, "AVERAGE_TURNAROUND != expected");
    if (rs >= 0) check(close_to(r->avgR, rs), suite, alg, "AVERAGE_RESPONSE != expected");
    if (thr >= 0) check(close_to(r->thr, thr), suite, alg, "THROUGHPUT != expected");
    if (util >= 0) check(close_to(r->util, util), suite, alg, "UTILIZATION != expected");
}

/* ---- inputs ---- */
static const char *IN_SINGLE = "1\n2\nP1,0,1,5,FEVER\n";

static const char *IN_SAME_ARRIVAL = "3\n2\nP1,0,1,4,FEVER\nP2,0,1,4,FEVER\nP3,0,1,4,FEVER\n";

static const char *IN_PREEMPT = "3\n2\nP1,0,3,10,FEVER\nP2,0,3,10,FEVER\nP3,2,1,4,CRITICAL\n";

static const char *IN_PRIO_DIFF =
    "5\n2\nP1,0,3,5,FEVER\nP2,0,3,5,FEVER\nP3,1,3,2,MINOR\nP4,2,3,2,MINOR\nP5,3,1,2,SEVERE\n";

static const char *IN_IDLE = "2\n2\nP1,0,1,3,FEVER\nP2,100,1,2,FEVER\n";

static const char *IN_LONG = "3\n2\nP1,0,1,10000,CRITICAL\nP2,0,2,1,MINOR\nP3,5,1,10000,CRITICAL\n";

static const char *IN_EDGE =
    "10\n2\nP1,0,3,10,FEVER\nP2,0,3,10,FEVER\nP3,1,2,4,FRACTURE\nP4,2,1,12,CRITICAL\n"
    "P5,2,1,12,CRITICAL\nP6,3,2,1,MINOR\nP7,3,2,1,MINOR\nP8,5,4,6,MINOR\n"
    "P9,60,1,3,SEVERE\nP10,60,1,3,SEVERE\n";

static const char *IN_SPEC_SAMPLE =
    "5\n2\nP1,0,3,10,FEVER\nP2,1,2,8,FRACTURE\nP3,2,1,12,CRITICAL\nP4,3,3,5,MINOR\nP5,4,1,7,SEVERE\n";

static const char *ALGS[3] = {"FCFS", "PRIORITY", "PRIORITY_PREEMPTIVE"};

static void invariant_suite(const char *name, const char *input, int n) {
    Result r;
    for (int a = 0; a < 3; a++) {
        if (!run(input, ALGS[a], &r)) { check(0, name, ALGS[a], "could not run scheduler"); continue; }
        validate(&r, name, ALGS[a], n, a < 2);
    }
}

int main(int argc, char **argv) {
    if (argc > 1) exe = argv[1];
    Result r;

    /* 1. Invariants on every input, every algorithm. */
    invariant_suite("single", IN_SINGLE, 1);
    invariant_suite("same-arrival", IN_SAME_ARRIVAL, 3);
    invariant_suite("preempt", IN_PREEMPT, 3);
    invariant_suite("priority-diff", IN_PRIO_DIFF, 5);
    invariant_suite("idle-gap", IN_IDLE, 2);
    invariant_suite("very-long", IN_LONG, 3);
    invariant_suite("edge-mix", IN_EDGE, 10);
    invariant_suite("spec-sample", IN_SPEC_SAMPLE, 5);

    /* 2. Single process: identical for all three algorithms. */
    {
        Exp e[] = {{"P1", 0, 5, 0, 5, 0}};
        for (int a = 0; a < 3; a++) {
            run(IN_SINGLE, ALGS[a], &r);
            expect_rows(&r, "single", ALGS[a], e, 1);
            expect_sum(&r, "single", ALGS[a], 0, 5, 0, 0.2, 50.0);
        }
    }

    /* 3. Same arrival, equal priority: no preemption, ID order, third waits for a free doctor. */
    {
        Exp e[] = {{"P1", 0, 4, 0, 4, 0}, {"P2", 0, 4, 0, 4, 0}, {"P3", 4, 8, 4, 8, 4}};
        for (int a = 0; a < 3; a++) {
            run(IN_SAME_ARRIVAL, ALGS[a], &r);
            expect_rows(&r, "same-arrival", ALGS[a], e, 3);
            expect_sum(&r, "same-arrival", ALGS[a], 4.0 / 3, 16.0 / 3, 4.0 / 3, 0.375, 75.0);
        }
    }

    /* 4. Preemption: P3 (prio 1) at t=2 preempts P2 (equal prio/arrival with P1, larger ID). */
    {
        Exp np[] = {{"P1", 0, 10, 0, 10, 0}, {"P2", 0, 10, 0, 10, 0}, {"P3", 10, 14, 8, 12, 8}};
        Exp pp[] = {{"P1", 0, 10, 0, 10, 0}, {"P2", 0, 14, 4, 14, 0}, {"P3", 2, 6, 0, 4, 0}};
        for (int a = 0; a < 2; a++) {
            run(IN_PREEMPT, ALGS[a], &r);
            expect_rows(&r, "preempt", ALGS[a], np, 3);
            expect_sum(&r, "preempt", ALGS[a], 8.0 / 3, 32.0 / 3, 8.0 / 3, 3.0 / 14, 24.0 / 28 * 100);
        }
        run(IN_PREEMPT, "PRIORITY_PREEMPTIVE", &r);
        expect_rows(&r, "preempt", "PRIORITY_PREEMPTIVE", pp, 3);
        expect_sum(&r, "preempt", "PRIORITY_PREEMPTIVE", 4.0 / 3, 28.0 / 3, 0.0, 3.0 / 14,
                   24.0 / 28 * 100);
    }

    /* 5. PRIORITY vs FCFS differ when several are waiting; PREEMPTIVE differs again. */
    {
        Exp f[] = {{"P1", 0, 5, 0, 5, 0}, {"P2", 0, 5, 0, 5, 0}, {"P3", 5, 7, 4, 6, 4},
                   {"P4", 5, 7, 3, 5, 3}, {"P5", 7, 9, 4, 6, 4}};
        Exp p[] = {{"P1", 0, 5, 0, 5, 0}, {"P2", 0, 5, 0, 5, 0}, {"P3", 5, 7, 4, 6, 4},
                   {"P4", 7, 9, 5, 7, 5}, {"P5", 5, 7, 2, 4, 2}};
        Exp pp[] = {{"P1", 0, 5, 0, 5, 0}, {"P2", 0, 7, 2, 7, 0}, {"P3", 5, 7, 4, 6, 4},
                    {"P4", 7, 9, 5, 7, 5}, {"P5", 3, 5, 0, 2, 0}};
        run(IN_PRIO_DIFF, "FCFS", &r);
        expect_rows(&r, "priority-diff", "FCFS", f, 5);
        run(IN_PRIO_DIFF, "PRIORITY", &r);
        expect_rows(&r, "priority-diff", "PRIORITY", p, 5);
        run(IN_PRIO_DIFF, "PRIORITY_PREEMPTIVE", &r);
        expect_rows(&r, "priority-diff", "PRIORITY_PREEMPTIVE", pp, 5);
    }

    /* 6. Idle gap: P2 must start exactly at its arrival time (100), not earlier. */
    for (int a = 0; a < 3; a++) {
        Exp e[] = {{"P1", 0, 3, 0, 3, 0}, {"P2", 100, 102, 0, 2, 0}};
        run(IN_IDLE, ALGS[a], &r);
        expect_rows(&r, "idle-gap", ALGS[a], e, 2);
    }

    /* 7. Lexicographic ordering: P10 must sort before P2 (checked inside validate(), edge-mix). */

    remove(TMP_INPUT);
    printf("\n%d checks passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}