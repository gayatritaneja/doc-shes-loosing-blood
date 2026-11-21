
#include "workers.h"

#include <string.h>
#include <stdio.h>

static void *worker_thread(void *arg)
{
    WorkerArg *self = (WorkerArg *)arg;
    WorkerPool *pool = self->pool;

    while (1) {
        WorkerTask task;

        pthread_mutex_lock(&pool->mutex);

        /* Wait until a task arrives or shutdown begins. */
        while (pool->task_count == 0 && !pool->stopping) {
            pthread_cond_wait(
                &pool->task_available,
                &pool->mutex
            );
        }

        /* Exit only after all queued tasks are processed. */
        if (pool->stopping && pool->task_count == 0) {
            pthread_mutex_unlock(&pool->mutex);
            return NULL;
        }

        task = pool->tasks[pool->task_head];

        pool->task_head =
            (pool->task_head + 1) % MAX_WORKER_TASKS;

        pool->task_count--;

        pthread_cond_signal(&pool->task_space_available);

        pthread_mutex_unlock(&pool->mutex);

        /* Execute the resource operation outside the queue lock. */
        int status;

        if (task.operation == WORKER_ACQUIRE) {
            status = resource_try_acquire(
                pool->resource_manager,
                task.resource_id,
                task.process_id,
                task.units
            );
        } else if (task.operation == WORKER_ACQUIRE_ALL) {
            status = resource_try_acquire_all(
                pool->resource_manager,
                task.process_id,
                (const char (*)[RESOURCE_ID_LENGTH])task.resource_set,
                task.resource_count,
                task.counted
            );
        } else if (task.operation == WORKER_RELEASE) {
            status = resource_release(
                pool->resource_manager,
                task.resource_id,
                task.process_id,
                task.units
            );
        } else {
            status = RESOURCE_INVALID_REQUEST;
        }

        /* Construct the completion result. */
        WorkerResult result;

        result.task_id = task.task_id;
        result.operation = task.operation;
        result.status = status;
        result.worker = self->index;

        strcpy(result.process_id, task.process_id);
        strcpy(result.resource_id, task.resource_id);

        /* Insert result into the shared result queue. */
        pthread_mutex_lock(&pool->mutex);

        while (pool->result_count == MAX_WORKER_TASKS) {
            pthread_cond_wait(
                &pool->result_space_available,
                &pool->mutex
            );
        }

        pool->results[pool->result_tail] = result;

        pool->result_tail =
            (pool->result_tail + 1) % MAX_WORKER_TASKS;

        pool->result_count++;

        pthread_cond_signal(&pool->result_available);

        pthread_mutex_unlock(&pool->mutex);
    }
}

int worker_pool_init(WorkerPool *pool,
                     ResourceManager *manager,
                     int worker_count)
{
    if (pool == NULL || manager == NULL ||
        !manager->initialized ||
        worker_count <= 0 || worker_count > MAX_WORKERS) {
        return -1;
    }

    memset(pool, 0, sizeof(*pool));

    pool->resource_manager = manager;

    if (pthread_mutex_init(&pool->mutex, NULL) != 0) {
        return -1;
    }

    if (pthread_cond_init(&pool->task_available, NULL) != 0)
        goto mutex_error;

    if (pthread_cond_init(&pool->task_space_available, NULL) != 0)
        goto task_available_error;

    if (pthread_cond_init(&pool->result_available, NULL) != 0)
        goto task_space_error;

    if (pthread_cond_init(&pool->result_space_available, NULL) != 0)
        goto result_available_error;

    pool->initialized = 1;

    for (int i = 0; i < worker_count; i++) {
        pool->args[i].pool = pool;
        pool->args[i].index = i;

        if (pthread_create(
                &pool->threads[i],
                NULL,
                worker_thread,
                &pool->args[i]) != 0) {

            pthread_mutex_lock(&pool->mutex);
            pool->stopping = 1;
            pthread_cond_broadcast(&pool->task_available);
            pthread_mutex_unlock(&pool->mutex);

            for (int j = 0; j < i; j++) {
                pthread_join(pool->threads[j], NULL);
            }

            pool->initialized = 0;

            pthread_cond_destroy(&pool->result_space_available);
            pthread_cond_destroy(&pool->result_available);
            pthread_cond_destroy(&pool->task_space_available);
            pthread_cond_destroy(&pool->task_available);
            pthread_mutex_destroy(&pool->mutex);

            return -1;
        }

        pool->worker_count++;
    }

    return 0;

result_available_error:
    pthread_cond_destroy(&pool->task_space_available);

task_space_error:
    pthread_cond_destroy(&pool->task_available);

task_available_error:
    pthread_mutex_destroy(&pool->mutex);
    return -1;

mutex_error:
    return -1;
}

int worker_pool_submit(WorkerPool *pool,
                       const WorkerTask *task)
{
    if (pool == NULL || task == NULL ||
        !pool->initialized ||
        task->process_id[0] == '\0' ||
        task->resource_id[0] == '\0' ||
        task->units <= 0 ||
        task->task_id < 0 ||
        strlen(task->process_id) >= RESOURCE_PROCESS_ID_LENGTH ||
        strlen(task->resource_id) >= RESOURCE_ID_LENGTH ||
        (task->operation != WORKER_ACQUIRE &&
         task->operation != WORKER_RELEASE &&
         task->operation != WORKER_ACQUIRE_ALL)) {
        return -1;
    }

    pthread_mutex_lock(&pool->mutex);

    while (pool->task_count == MAX_WORKER_TASKS &&
           !pool->stopping) {
        pthread_cond_wait(
            &pool->task_space_available,
            &pool->mutex
        );
    }

    if (pool->stopping) {
        pthread_mutex_unlock(&pool->mutex);
        return -1;
    }

    pool->tasks[pool->task_tail] = *task;

    pool->task_tail =
        (pool->task_tail + 1) % MAX_WORKER_TASKS;

    pool->task_count++;

    pthread_cond_signal(&pool->task_available);

    pthread_mutex_unlock(&pool->mutex);

    return 0;
}

int worker_pool_get_result(WorkerPool *pool,
                           WorkerResult *result)
{
    if (pool == NULL || result == NULL ||
        !pool->initialized) {
        return -1;
    }

    pthread_mutex_lock(&pool->mutex);

    while (pool->result_count == 0) {
        if (pool->stopping) {
            pthread_mutex_unlock(&pool->mutex);
            return -1;
        }

        pthread_cond_wait(
            &pool->result_available,
            &pool->mutex
        );
    }

    *result = pool->results[pool->result_head];

    pool->result_head =
        (pool->result_head + 1) % MAX_WORKER_TASKS;

    pool->result_count--;

    pthread_cond_signal(&pool->result_space_available);

    pthread_mutex_unlock(&pool->mutex);

    return 0;
}

void worker_pool_destroy(WorkerPool *pool)
{
    if (pool == NULL || !pool->initialized) {
        return;
    }

    pthread_mutex_lock(&pool->mutex);

    pool->stopping = 1;

    pthread_cond_broadcast(&pool->task_available);
    pthread_cond_broadcast(&pool->task_space_available);
    pthread_cond_broadcast(&pool->result_available);
    pthread_cond_broadcast(&pool->result_space_available);

    pthread_mutex_unlock(&pool->mutex);

    for (int i = 0; i < pool->worker_count; i++) {
        pthread_join(pool->threads[i], NULL);
    }

    pthread_cond_destroy(&pool->task_available);
    pthread_cond_destroy(&pool->task_space_available);
    pthread_cond_destroy(&pool->result_available);
    pthread_cond_destroy(&pool->result_space_available);

    pthread_mutex_destroy(&pool->mutex);

    pool->initialized = 0;
}
