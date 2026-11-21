
#ifndef RESOURCE_MANAGER_H
#define RESOURCE_MANAGER_H

#include <pthread.h>

#define MAX_RESOURCES 64
#define MAX_RESOURCE_HOLDERS 16
#define RESOURCE_ID_LENGTH 32
#define RESOURCE_PROCESS_ID_LENGTH 32
#define RESOURCE_SET_MAX 8

typedef struct {
    char process_id[RESOURCE_PROCESS_ID_LENGTH];
    int units;
} ResourceHolder;

typedef struct {
    char id[RESOURCE_ID_LENGTH];

    int capacity;
    int available;

    ResourceHolder holders[MAX_RESOURCE_HOLDERS];
    int holder_count;

    int acquisitions;
    int wait_events;
    int max_simultaneous_holders;
} SharedResource;

typedef struct {
    SharedResource resources[MAX_RESOURCES];
    int resource_count;

    pthread_mutex_t mutex;
    pthread_cond_t condition;

    int exclusivity_violations;   /* invariant failures; must stay 0 */
    int initialized;
} ResourceManager;

typedef enum {
    RESOURCE_SUCCESS = 0,
    RESOURCE_NOT_FOUND = -1,
    RESOURCE_INVALID_REQUEST = -2,
    RESOURCE_NOT_OWNED = -3,
    RESOURCE_MANAGER_ERROR = -4, 
    RESOURCE_BUSY = -5,
    RESOURCE_TIMEOUT = -6
} ResourceResult;

/* Initialize and destroy the resource manager. */
int resource_manager_init(ResourceManager *manager);
void resource_manager_destroy(ResourceManager *manager);

/* Register resources before starting worker threads. */
int resource_manager_add(ResourceManager *manager,
                         const char *resource_id,
                         int capacity);

/* Acquire and release resource units.
   Acquisition blocks if insufficient units are available. */
int resource_acquire(ResourceManager *manager,
                     const char *resource_id,
                     const char *process_id,
                     int units);
int resource_try_acquire(ResourceManager *manager,
                         const char *resource_id,
                         const char *process_id,
                         int units);
                         
/* Blocking acquire that gives up after timeout_ms (watchdog use). */
int resource_acquire_timed(ResourceManager *manager,
                           const char *resource_id,
                           const char *process_id,
                           int units,
                           int timeout_ms);

/* All-or-nothing acquisition of one unit of each listed resource: either
   every resource is granted atomically or none is (no hold-and-wait).
   Only the first `counted` grants are counted as acquisitions, so a
   re-acquisition after a rollback is not counted twice. The try version
   returns RESOURCE_BUSY at once; the blocking version waits. */
int resource_try_acquire_all(ResourceManager *manager,
                             const char *process_id,
                             const char ids[][RESOURCE_ID_LENGTH],
                             int count, int counted);
int resource_acquire_all(ResourceManager *manager,
                         const char *process_id,
                         const char ids[][RESOURCE_ID_LENGTH],
                         int count, int counted);

/* Copy the ids of the resources currently held by a process. */
int resource_list_held(ResourceManager *manager,
                       const char *process_id,
                       char out[][RESOURCE_ID_LENGTH], int max);

int resource_release(ResourceManager *manager,
                     const char *resource_id,
                     const char *process_id,
                     int units);

/* Read-only statistics, protected internally by the mutex. */
int resource_get_available(ResourceManager *manager,
                           const char *resource_id);

int resource_get_acquisitions(ResourceManager *manager,
                              const char *resource_id);

int resource_get_wait_events(ResourceManager *manager,
                             const char *resource_id);

int resource_get_max_holders(ResourceManager *manager,
                             const char *resource_id);

int resource_get_violations(ResourceManager *manager);

#endif
