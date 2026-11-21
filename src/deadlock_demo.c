#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include "resource_manager.h"
#include "scheduler.h"

/*
 * Bounded demonstration (./scheduler --deadlock-demo), three phases on real
 * threads and the real ResourceManager:
 *   1. mutual exclusion: two threads hammer one XRAY; a plain counter that
 *      is only touched while holding XRAY must not lose an update;
 *   2. unsafe pattern: one thread takes XRAY then MRI, the other MRI then
 *      XRAY. Both hold their first resource before asking for the second
 *      (circular wait). A timed wait is the watchdog; it detects the cycle
 *      and the victims release what they hold, so the demo always ends;
 *   3. prevention: the same two threads, same timing, but each requests
 *      its pair all-or-nothing (no hold-and-wait), so both finish.
 */

#define WATCHDOG_MS 300
#define ROUNDS 5000

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int arrived;
    int total;
} Rendezvous;

typedef struct {
    ResourceManager *manager;
    Rendezvous *meeting;
    const char *name;
    const char *first;
    const char *second;
    int prevented;
    int outcome;               /* 0 finished, 1 watchdog fired */
} DemoThread;

static void rendezvous_wait(Rendezvous *r)
{
    pthread_mutex_lock(&r->mutex);
    r->arrived++;
    pthread_cond_broadcast(&r->cond);
    while (r->arrived < r->total) {
        pthread_cond_wait(&r->cond, &r->mutex);
    }
    pthread_mutex_unlock(&r->mutex);
}

static void *pair_thread(void *arg)
{
    DemoThread *t = arg;
    ResourceManager *m = t->manager;

    if (t->prevented) {
        char pair[2][RESOURCE_ID_LENGTH];

        strcpy(pair[0], t->first);
        strcpy(pair[1], t->second);
        rendezvous_wait(t->meeting);          /* same timing as phase 2 */
        resource_acquire_all(m, t->name, (const char (*)[RESOURCE_ID_LENGTH])pair, 2, 2);
        resource_release(m, t->first, t->name, 1);
        resource_release(m, t->second, t->name, 1);
        return NULL;
    }

    resource_acquire(m, t->first, t->name, 1);
    printf("  %s holds %s\n", t->name, t->first);
    rendezvous_wait(t->meeting);              /* both hold their first */

    if (resource_acquire_timed(m, t->second, t->name, 1, WATCHDOG_MS)
        == RESOURCE_TIMEOUT) {
        t->outcome = 1;
        printf("  %s: still waiting for %s after %d ms -> circular wait\n",
               t->name, t->second, WATCHDOG_MS);
        resource_release(m, t->first, t->name, 1);   /* recovery */
        return NULL;
    }
    resource_release(m, t->second, t->name, 1);
    resource_release(m, t->first, t->name, 1);
    return NULL;
}

typedef struct {
    ResourceManager *manager;
    const char *name;
    long *counter;
} HammerThread;

static void *hammer_thread(void *arg)
{
    HammerThread *h = arg;

    for (int i = 0; i < ROUNDS; i++) {
        resource_acquire(h->manager, "XRAY", h->name, 1);
        (*h->counter)++;                      /* critical section */
        resource_release(h->manager, "XRAY", h->name, 1);
    }
    return NULL;
}

static int run_pair(ResourceManager *m, int prevented)
{
    Rendezvous meeting = {PTHREAD_MUTEX_INITIALIZER,
                          PTHREAD_COND_INITIALIZER, 0, 2};
    DemoThread a = {m, &meeting, "T1", "XRAY", "MRI", prevented, 0};
    DemoThread b = {m, &meeting, "T2", "MRI", "XRAY", prevented, 0};
    pthread_t ta, tb;

    pthread_create(&ta, NULL, pair_thread, &a);
    pthread_create(&tb, NULL, pair_thread, &b);
    pthread_join(ta, NULL);
    pthread_join(tb, NULL);
    return a.outcome + b.outcome;
}

int run_deadlock_demo(void)
{
    ResourceManager m;
    int failed = 0;

    resource_manager_init(&m);
    resource_manager_add(&m, "XRAY", 1);
    resource_manager_add(&m, "MRI", 1);

    printf("PHASE 1: mutual exclusion under real contention\n");
    {
        long counter = 0;
        HammerThread h1 = {&m, "T1", &counter};
        HammerThread h2 = {&m, "T2", &counter};
        pthread_t t1, t2;

        pthread_create(&t1, NULL, hammer_thread, &h1);
        pthread_create(&t2, NULL, hammer_thread, &h2);
        pthread_join(t1, NULL);
        pthread_join(t2, NULL);

        printf("  counter=%ld (expected %d), MAX_SIMULTANEOUS_HOLDERS=%d, "
               "WAIT_EVENTS=%d, EXCLUSIVITY_VIOLATIONS=%d\n",
               counter, 2 * ROUNDS,
               resource_get_max_holders(&m, "XRAY"),
               resource_get_wait_events(&m, "XRAY"),
               resource_get_violations(&m));
        failed |= counter != 2 * ROUNDS ||
                  resource_get_max_holders(&m, "XRAY") != 1 ||
                  resource_get_violations(&m) != 0;
    }

    printf("PHASE 2: unsafe order (T1: XRAY then MRI, T2: MRI then XRAY)\n");
    int stuck = run_pair(&m, 0);
    printf("  watchdog fired for %d thread(s): DEADLOCK-PRONE PATTERN %s\n",
           stuck, stuck > 0 ? "CONFIRMED" : "not reproduced");
    failed |= stuck == 0 ||
              resource_get_available(&m, "XRAY") != 1 ||
              resource_get_available(&m, "MRI") != 1;

    printf("PHASE 3: same timing with all-or-nothing acquisition\n");
    stuck = run_pair(&m, 1);
    printf("  both threads finished, no deadlock (resources free: "
           "XRAY=%d MRI=%d)\n",
           resource_get_available(&m, "XRAY"),
           resource_get_available(&m, "MRI"));
    failed |= stuck != 0 ||
              resource_get_available(&m, "XRAY") != 1 ||
              resource_get_available(&m, "MRI") != 1;

    resource_manager_destroy(&m);
    printf("DEMO %s\n", failed ? "FAILED" : "PASSED");
    return failed ? 1 : 0;
}
