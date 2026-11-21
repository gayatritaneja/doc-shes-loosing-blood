
#include "../src/workers.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int submit_task(WorkerPool *pool,
                       int id,
                       WorkerOperation operation,
                       const char *process,
                       const char *resource)
{
    WorkerTask task = {0};

    task.task_id = id;
    task.operation = operation;

    strcpy(task.process_id, process);
    strcpy(task.resource_id, resource);

    task.units = 1;

    return worker_pool_submit(pool, &task);
}

static int get_expected_result(WorkerPool *pool,
                               int expected_id,
                               int expected_status)
{
    WorkerResult result;

    if (worker_pool_get_result(pool, &result) != 0) {
        return -1;
    }

    printf("Task %d | Process %s | Resource %s | Status %d\n",
           result.task_id,
           result.process_id,
           result.resource_id,
           result.status);

    if (result.task_id != expected_id ||
        result.status != expected_status) {
        return -1;
    }

    return 0;
}
static int get_two_expected_results(WorkerPool *pool)
{
    int seen_1 = 0;
    int seen_2 = 0;

    for (int i = 0; i < 2; i++) {
        WorkerResult result;

        if (worker_pool_get_result(pool, &result) != 0)
            return -1;

        printf("Task %d | Process %s | Resource %s | Status %d\n",
               result.task_id,
               result.process_id,
               result.resource_id,
               result.status);

        if (result.status != RESOURCE_SUCCESS)
            return -1;

        if (result.task_id == 1)
            seen_1 = 1;
        else if (result.task_id == 2)
            seen_2 = 1;
        else
            return -1;
    }

    return seen_1 && seen_2 ? 0 : -1;
}

int main(void)
{
    ResourceManager manager;
    WorkerPool pool;

    if (resource_manager_init(&manager) != RESOURCE_SUCCESS) {
        printf("Resource manager initialization failed.\n");
        return EXIT_FAILURE;
    }

    if (resource_manager_add(&manager, "XRAY", 1) != RESOURCE_SUCCESS ||
        resource_manager_add(&manager, "MRI", 1) != RESOURCE_SUCCESS) {
        printf("Resource registration failed.\n");
        resource_manager_destroy(&manager);
        return EXIT_FAILURE;
    }

    if (worker_pool_init(&pool, &manager, 2) != 0) {
        printf("Worker pool initialization failed.\n");
        resource_manager_destroy(&manager);
        return EXIT_FAILURE;
    }

    int failed = 0;

    printf("\n===== WORKER POOL TEST =====\n");

    /* Test 1: Different resources can be acquired. */

    printf("\nTest 1: Independent resource acquisition\n");

    failed |= submit_task(&pool, 1, WORKER_ACQUIRE, "P1", "XRAY") != 0;
    failed |= submit_task(&pool, 2, WORKER_ACQUIRE, "P2", "MRI") != 0;

    failed |= get_two_expected_results(&pool) != 0;

    /* Test 2: Competing acquisition of the same resource. */

    printf("\nTest 2: Competing resource acquisition\n");

    failed |= submit_task(&pool, 3, WORKER_ACQUIRE, "P2", "XRAY") != 0;

    failed |= get_expected_result(&pool, 3, RESOURCE_BUSY) != 0;

    /* Test 3: Release XRAY and retry. */

    printf("\nTest 3: Release and retry\n");

    failed |= submit_task(&pool, 4, WORKER_RELEASE, "P1", "XRAY") != 0;

    failed |= get_expected_result(&pool, 4, RESOURCE_SUCCESS) != 0;

    failed |= submit_task(&pool, 5, WORKER_ACQUIRE, "P2", "XRAY") != 0;

    failed |= get_expected_result(&pool, 5, RESOURCE_SUCCESS) != 0;

    /* Release all remaining resources. */

    failed |= submit_task(&pool, 6, WORKER_RELEASE, "P2", "XRAY") != 0;
    failed |= submit_task(&pool, 7, WORKER_RELEASE, "P2", "MRI") != 0;

    failed |= get_expected_result(&pool, 6, RESOURCE_SUCCESS) != 0;
    failed |= get_expected_result(&pool, 7, RESOURCE_SUCCESS) != 0;

    /* Verify final resource state. */

    printf("\n===== FINAL RESOURCE STATE =====\n");

    int xray_available =
        resource_get_available(&manager, "XRAY");

    int mri_available =
        resource_get_available(&manager, "MRI");

    printf("XRAY available: %d/1\n", xray_available);
    printf("MRI available: %d/1\n", mri_available);

    if (xray_available != 1 || mri_available != 1) {
        failed = 1;
    }

    /* All submitted tasks have been collected before shutdown. */

    worker_pool_destroy(&pool);
    resource_manager_destroy(&manager);

    if (failed) {
        printf("\nTEST FAILED.\n");
        return EXIT_FAILURE;
    }

    printf("\nTEST PASSED: Worker pool operates correctly.\n");

    return EXIT_SUCCESS;
}
