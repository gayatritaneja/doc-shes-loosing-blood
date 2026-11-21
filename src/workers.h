
#ifndef WORKERS_H
#define WORKERS_H

#include <pthread.h>
#include "resource_manager.h"

#define MAX_WORKERS 2
#define MAX_WORKER_TASKS 128

typedef enum {
    WORKER_ACQUIRE,        /* try one resource, never blocks        */
    WORKER_RELEASE,
    WORKER_ACQUIRE_ALL     /* all-or-nothing over resource_set[]    */
} WorkerOperation;

typedef struct {
    int task_id;
    WorkerOperation operation;

    char process_id[RESOURCE_PROCESS_ID_LENGTH];
    char resource_id[RESOURCE_ID_LENGTH];

    int units;

    /* WORKER_ACQUIRE_ALL only (resource_id is set to resource_set[0]). */
    char resource_set[RESOURCE_SET_MAX][RESOURCE_ID_LENGTH];
    int resource_count;
    int counted;
} WorkerTask;

typedef struct {
    int task_id;
    WorkerOperation operation;

    char process_id[RESOURCE_PROCESS_ID_LENGTH];
    char resource_id[RESOURCE_ID_LENGTH];

    int status;
    int worker;            /* index of the worker thread that ran it */
} WorkerResult;

typedef struct WorkerPool WorkerPool;

typedef struct {
    WorkerPool *pool;
    int index;
} WorkerArg;

struct WorkerPool {
    pthread_t threads[MAX_WORKERS];
    WorkerArg args[MAX_WORKERS];
    int worker_count;

    WorkerTask tasks[MAX_WORKER_TASKS];
    int task_head;
    int task_tail;
    int task_count;

    WorkerResult results[MAX_WORKER_TASKS];
    int result_head;
    int result_tail;
    int result_count;

    ResourceManager *resource_manager;

    pthread_mutex_t mutex;

    pthread_cond_t task_available;
    pthread_cond_t task_space_available;

    pthread_cond_t result_available;
    pthread_cond_t result_space_available;

    int stopping;
    int initialized;
};

/* Initialize the pool and start its worker threads. */
int worker_pool_init(WorkerPool *pool,
                     ResourceManager *manager,
                     int worker_count);

/* Submit a resource operation. */
int worker_pool_submit(WorkerPool *pool,
                       const WorkerTask *task);

/* Retrieve a completed operation. Blocks until a result is available. */
int worker_pool_get_result(WorkerPool *pool,
                           WorkerResult *result);

/* Stop workers and release synchronization resources. */
void worker_pool_destroy(WorkerPool *pool);

#endif
