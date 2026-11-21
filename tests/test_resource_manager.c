
#include "../src/resource_manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#define ITERATIONS 1000

ResourceManager manager;

pthread_barrier_t start_barrier;
pthread_mutex_t test_mutex;

int active_holders = 0;
int max_active_holders = 0;
int failed = 0;

typedef struct {
    const char *process_id;
} WorkerArgs;

void *worker(void *arg)
{
    WorkerArgs *args = (WorkerArgs *)arg;

    /* Both workers begin competing at the same time. */
    pthread_barrier_wait(&start_barrier);

    for (int i = 0; i < ITERATIONS; i++) {

        /* Request the exclusive XRAY. */
        if (resource_acquire(&manager, "XRAY",
                             args->process_id, 1) != RESOURCE_SUCCESS) {
            pthread_mutex_lock(&test_mutex);
            failed = 1;
            pthread_mutex_unlock(&test_mutex);
            return NULL;
        }

        /* Track how many workers are inside the critical section. */
        pthread_mutex_lock(&test_mutex);

        active_holders++;

        if (active_holders > max_active_holders) {
            max_active_holders = active_holders;
        }

        pthread_mutex_unlock(&test_mutex);

        /* Simulate some work while holding the resource. */
        volatile unsigned long work = 0;

        for (int j = 0; j < 1000; j++) {
            work += (unsigned long)j;
        }

        /* Leave the critical section. */
        pthread_mutex_lock(&test_mutex);
        active_holders--;
        pthread_mutex_unlock(&test_mutex);

        /* Release the XRAY. */
        if (resource_release(&manager, "XRAY",
                             args->process_id, 1) != RESOURCE_SUCCESS) {
            pthread_mutex_lock(&test_mutex);
            failed = 1;
            pthread_mutex_unlock(&test_mutex);
            return NULL;
        }
    }

    return NULL;
}

int main(void)
{
    pthread_t thread1, thread2;

    WorkerArgs args1 = {"P1"};
    WorkerArgs args2 = {"P2"};

    if (resource_manager_init(&manager) != RESOURCE_SUCCESS) {
        printf("Failed to initialize resource manager.\n");
        return EXIT_FAILURE;
    }

    if (resource_manager_add(&manager, "XRAY", 1) != RESOURCE_SUCCESS) {
        printf("Failed to register XRAY.\n");
        resource_manager_destroy(&manager);
        return EXIT_FAILURE;
    }

    pthread_mutex_init(&test_mutex, NULL);
    pthread_barrier_init(&start_barrier, NULL, 2);

    if (pthread_create(&thread1, NULL, worker, &args1) != 0 ||
        pthread_create(&thread2, NULL, worker, &args2) != 0) {
        printf("Failed to create worker threads.\n");
        return EXIT_FAILURE;
    }

    pthread_join(thread1, NULL);
    pthread_join(thread2, NULL);

    printf("\n===== RESOURCE MANAGER TEST =====\n");

    printf("Workers: P1 and P2\n");
    printf("Iterations per worker: %d\n", ITERATIONS);

    printf("Total acquisitions: %d\n",
           resource_get_acquisitions(&manager, "XRAY"));

    printf("Resource wait events: %d\n",
           resource_get_wait_events(&manager, "XRAY"));

    printf("Maximum simultaneous holders: %d\n",
           resource_get_max_holders(&manager, "XRAY"));

    printf("Remaining available units: %d\n",
           resource_get_available(&manager, "XRAY"));

    printf("Maximum workers inside critical section: %d\n",
           max_active_holders);

    if (!failed &&
        resource_get_acquisitions(&manager, "XRAY") == 2 * ITERATIONS &&
        resource_get_available(&manager, "XRAY") == 1 &&
        resource_get_max_holders(&manager, "XRAY") == 1 &&
        max_active_holders == 1) {

        printf("\nTEST PASSED: Mutual exclusion maintained.\n");

    } else {
        printf("\nTEST FAILED: Resource safety violation.\n");
    }

    pthread_barrier_destroy(&start_barrier);
    pthread_mutex_destroy(&test_mutex);
    resource_manager_destroy(&manager);

    return failed || max_active_holders != 1 ? EXIT_FAILURE : EXIT_SUCCESS;
}
