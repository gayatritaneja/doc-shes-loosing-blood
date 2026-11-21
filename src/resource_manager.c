#define _POSIX_C_SOURCE 200809L
#include "resource_manager.h"

#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

/* All functions below that access resources are called
   while manager->mutex is locked. */

static SharedResource *find_resource(ResourceManager *manager,
                                     const char *resource_id)
{
    for (int i = 0; i < manager->resource_count; i++) {
        if (strcmp(manager->resources[i].id, resource_id) == 0) {
            return &manager->resources[i];
        }
    }

    return NULL;
}

static int find_holder(SharedResource *resource,
                       const char *process_id)
{
    for (int i = 0; i < resource->holder_count; i++) {
        if (strcmp(resource->holders[i].process_id,
                   process_id) == 0) {
            return i;
        }
    }

    return -1;
}

/* Ownership invariant, checked after every state change (mutex held):
   units available + units held == capacity, and nothing is negative. */
static void verify_invariant(ResourceManager *manager,
                             const SharedResource *resource)
{
    int held = 0;

    for (int i = 0; i < resource->holder_count; i++) {
        held += resource->holders[i].units;
    }

    if (resource->available < 0 ||
        held > resource->capacity ||
        held + resource->available != resource->capacity) {
        manager->exclusivity_violations++;
    }
}

/* Grant one unit to a process; the caller verified availability. */
static int grant_one(ResourceManager *manager, SharedResource *resource,
                     const char *process_id, int count_it)
{
    int index = find_holder(resource, process_id);

    if (index >= 0) {
        resource->holders[index].units += 1;
    } else {
        if (resource->holder_count >= MAX_RESOURCE_HOLDERS) {
            return RESOURCE_MANAGER_ERROR;
        }
        ResourceHolder *holder =
            &resource->holders[resource->holder_count++];
        strcpy(holder->process_id, process_id);
        holder->units = 1;
    }

    resource->available -= 1;
    if (count_it) {
        resource->acquisitions++;
    }
    if (resource->holder_count > resource->max_simultaneous_holders) {
        resource->max_simultaneous_holders = resource->holder_count;
    }
    verify_invariant(manager, resource);
    return RESOURCE_SUCCESS;
}

int resource_manager_init(ResourceManager *manager)
{
    if (manager == NULL) {
        return RESOURCE_MANAGER_ERROR;
    }

    memset(manager, 0, sizeof(*manager));

    if (pthread_mutex_init(&manager->mutex, NULL) != 0) {
        return RESOURCE_MANAGER_ERROR;
    }

    if (pthread_cond_init(&manager->condition, NULL) != 0) {
        pthread_mutex_destroy(&manager->mutex);
        return RESOURCE_MANAGER_ERROR;
    }

    manager->initialized = 1;

    return RESOURCE_SUCCESS;
}

void resource_manager_destroy(ResourceManager *manager)
{
    if (manager == NULL || !manager->initialized) {
        return;
    }

    pthread_cond_destroy(&manager->condition);
    pthread_mutex_destroy(&manager->mutex);

    manager->initialized = 0;
}

int resource_manager_add(ResourceManager *manager,
                         const char *resource_id,
                         int capacity)
{
    if (manager == NULL || !manager->initialized ||
        resource_id == NULL || resource_id[0] == '\0' ||
        strlen(resource_id) >= RESOURCE_ID_LENGTH ||
        capacity <= 0 ||
        capacity > MAX_RESOURCE_HOLDERS) {
        return RESOURCE_INVALID_REQUEST;
    }

    pthread_mutex_lock(&manager->mutex);

    if (manager->resource_count >= MAX_RESOURCES ||
        find_resource(manager, resource_id) != NULL) {
        pthread_mutex_unlock(&manager->mutex);
        return RESOURCE_INVALID_REQUEST;
    }

    SharedResource *resource =
        &manager->resources[manager->resource_count++];

    memset(resource, 0, sizeof(*resource));

    strcpy(resource->id, resource_id);

    resource->capacity = capacity;
    resource->available = capacity;

    pthread_mutex_unlock(&manager->mutex);

    return RESOURCE_SUCCESS;
}

int resource_try_acquire(ResourceManager *manager,
                         const char *resource_id,
                         const char *process_id,
                         int units)
{
    if (manager == NULL || !manager->initialized ||
        resource_id == NULL || process_id == NULL ||
        process_id[0] == '\0' ||
        strlen(process_id) >= RESOURCE_PROCESS_ID_LENGTH ||
        units <= 0) {
        return RESOURCE_INVALID_REQUEST;
    }

    pthread_mutex_lock(&manager->mutex);

    SharedResource *resource =
        find_resource(manager, resource_id);

    if (resource == NULL) {
        pthread_mutex_unlock(&manager->mutex);
        return RESOURCE_NOT_FOUND;
    }

    if (units > resource->capacity) {
        pthread_mutex_unlock(&manager->mutex);
        return RESOURCE_INVALID_REQUEST;
    }

    int index = find_holder(resource, process_id);

    if (index >= 0 &&
        resource->holders[index].units + units >
        resource->capacity) {
        pthread_mutex_unlock(&manager->mutex);
        return RESOURCE_INVALID_REQUEST;
    }

    /* Do not block the worker if the resource is unavailable. */
    if (resource->available < units) {
        resource->wait_events++;
        pthread_mutex_unlock(&manager->mutex);
        return RESOURCE_BUSY;
    }

    /* Allocate the resource atomically. */
    if (index >= 0) {
        resource->holders[index].units += units;
    } else {
        if (resource->holder_count >= MAX_RESOURCE_HOLDERS) {
            pthread_mutex_unlock(&manager->mutex);
            return RESOURCE_MANAGER_ERROR;
        }

        ResourceHolder *holder =
            &resource->holders[resource->holder_count++];

        strcpy(holder->process_id, process_id);
        holder->units = units;
    }

    resource->available -= units;
    resource->acquisitions++;

    if (resource->holder_count >
        resource->max_simultaneous_holders) {
        resource->max_simultaneous_holders =
            resource->holder_count;
    }

    verify_invariant(manager, resource);
    pthread_mutex_unlock(&manager->mutex);

    return RESOURCE_SUCCESS;
}

static int acquire_impl(ResourceManager *manager,
                        const char *resource_id,
                        const char *process_id,
                        int units,
                        const struct timespec *deadline)
{
    if (manager == NULL || !manager->initialized ||
        resource_id == NULL || process_id == NULL ||
        process_id[0] == '\0' ||
        strlen(process_id) >= RESOURCE_PROCESS_ID_LENGTH ||
        units <= 0) {
        return RESOURCE_INVALID_REQUEST;
    }

    pthread_mutex_lock(&manager->mutex);

    SharedResource *resource =
        find_resource(manager, resource_id);

    if (resource == NULL) {
        pthread_mutex_unlock(&manager->mutex);
        return RESOURCE_NOT_FOUND;
    }

    if (units > resource->capacity) {
        pthread_mutex_unlock(&manager->mutex);
        return RESOURCE_INVALID_REQUEST;
    }

    int holder_index = find_holder(resource, process_id);

    if (holder_index >= 0 &&
        resource->holders[holder_index].units + units >
        resource->capacity) {
        pthread_mutex_unlock(&manager->mutex);
        return RESOURCE_INVALID_REQUEST;
    }

    int waited = 0;

    while (resource->available < units) {
        if (!waited) {
            resource->wait_events++;
            waited = 1;
        }

        int result = deadline == NULL
            ? pthread_cond_wait(&manager->condition, &manager->mutex)
            : pthread_cond_timedwait(&manager->condition,
                                     &manager->mutex, deadline);

        if (result == ETIMEDOUT && resource->available < units) {
            pthread_mutex_unlock(&manager->mutex);
            return RESOURCE_TIMEOUT;
        }

        if (result != 0 && result != ETIMEDOUT) {
            pthread_mutex_unlock(&manager->mutex);
            return RESOURCE_MANAGER_ERROR;
        }
    }

    /* Resource is available. Update ownership atomically. */

    holder_index = find_holder(resource, process_id);

    if (holder_index >= 0) {
        resource->holders[holder_index].units += units;
    } else {
        if (resource->holder_count >= MAX_RESOURCE_HOLDERS) {
            pthread_mutex_unlock(&manager->mutex);
            return RESOURCE_MANAGER_ERROR;
        }

        ResourceHolder *holder =
            &resource->holders[resource->holder_count++];

        strcpy(holder->process_id, process_id);
        holder->units = units;
    }

    resource->available -= units;
    resource->acquisitions++;

    if (resource->holder_count >
        resource->max_simultaneous_holders) {
        resource->max_simultaneous_holders =
            resource->holder_count;
    }

    verify_invariant(manager, resource);
    pthread_mutex_unlock(&manager->mutex);

    return RESOURCE_SUCCESS;
}

int resource_acquire(ResourceManager *manager,
                     const char *resource_id,
                     const char *process_id,
                     int units)
{
    return acquire_impl(manager, resource_id, process_id, units, NULL);
}

int resource_acquire_timed(ResourceManager *manager,
                           const char *resource_id,
                           const char *process_id,
                           int units,
                           int timeout_ms)
{
    struct timespec deadline;

    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += timeout_ms / 1000;
    deadline.tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_sec++;
        deadline.tv_nsec -= 1000000000L;
    }

    return acquire_impl(manager, resource_id, process_id, units, &deadline);
}

/* Check a whole set under the lock: SUCCESS if every resource exists, is
   not already held by this process and has a free unit; BUSY otherwise. */
static int check_set(ResourceManager *manager, const char *process_id,
                     const char ids[][RESOURCE_ID_LENGTH], int count)
{
    int busy = 0;

    for (int i = 0; i < count; i++) {
        SharedResource *resource = find_resource(manager, ids[i]);

        if (resource == NULL) {
            return RESOURCE_NOT_FOUND;
        }
        if (find_holder(resource, process_id) >= 0) {
            return RESOURCE_INVALID_REQUEST;
        }
        if (resource->available < 1) {
            busy = 1;
        }
    }

    return busy ? RESOURCE_BUSY : RESOURCE_SUCCESS;
}

static int acquire_all_impl(ResourceManager *manager,
                            const char *process_id,
                            const char ids[][RESOURCE_ID_LENGTH],
                            int count, int counted, int blocking)
{
    if (manager == NULL || !manager->initialized || process_id == NULL ||
        process_id[0] == '\0' || ids == NULL ||
        count <= 0 || count > RESOURCE_SET_MAX) {
        return RESOURCE_INVALID_REQUEST;
    }

    pthread_mutex_lock(&manager->mutex);

    int status = check_set(manager, process_id, ids, count);
    int waited = 0;

    while (blocking && status == RESOURCE_BUSY) {
        if (!waited) {
            for (int i = 0; i < count; i++) {
                find_resource(manager, ids[i])->wait_events++;
            }
            waited = 1;
        }
        pthread_cond_wait(&manager->condition, &manager->mutex);
        status = check_set(manager, process_id, ids, count);
    }

    if (status == RESOURCE_SUCCESS) {
        for (int i = 0; i < count && status == RESOURCE_SUCCESS; i++) {
            status = grant_one(manager, find_resource(manager, ids[i]),
                               process_id, i < counted);
        }
    }

    pthread_mutex_unlock(&manager->mutex);
    return status;
}

int resource_try_acquire_all(ResourceManager *manager,
                             const char *process_id,
                             const char ids[][RESOURCE_ID_LENGTH],
                             int count, int counted)
{
    return acquire_all_impl(manager, process_id, ids, count, counted, 0);
}

int resource_acquire_all(ResourceManager *manager,
                         const char *process_id,
                         const char ids[][RESOURCE_ID_LENGTH],
                         int count, int counted)
{
    return acquire_all_impl(manager, process_id, ids, count, counted, 1);
}

int resource_list_held(ResourceManager *manager,
                       const char *process_id,
                       char out[][RESOURCE_ID_LENGTH], int max)
{
    int found = 0;

    pthread_mutex_lock(&manager->mutex);

    for (int i = 0; i < manager->resource_count && found < max; i++) {
        if (find_holder(&manager->resources[i], process_id) >= 0) {
            strcpy(out[found++], manager->resources[i].id);
        }
    }

    pthread_mutex_unlock(&manager->mutex);
    return found;
}

int resource_release(ResourceManager *manager,
                     const char *resource_id,
                     const char *process_id,
                     int units)
{
    if (manager == NULL || !manager->initialized ||
        resource_id == NULL || process_id == NULL ||
        units <= 0) {
        return RESOURCE_INVALID_REQUEST;
    }

    pthread_mutex_lock(&manager->mutex);

    SharedResource *resource =
        find_resource(manager, resource_id);

    if (resource == NULL) {
        pthread_mutex_unlock(&manager->mutex);
        return RESOURCE_NOT_FOUND;
    }

    int index = find_holder(resource, process_id);

    if (index < 0 ||
        resource->holders[index].units < units) {
        pthread_mutex_unlock(&manager->mutex);
        return RESOURCE_NOT_OWNED;
    }

    resource->holders[index].units -= units;
    resource->available += units;

    if (resource->holders[index].units == 0) {
        for (int i = index; i < resource->holder_count - 1; i++) {
            resource->holders[i] = resource->holders[i + 1];
        }

        resource->holder_count--;
    }

    verify_invariant(manager, resource);

    /* Wake threads waiting for available resource units. */
    pthread_cond_broadcast(&manager->condition);

    pthread_mutex_unlock(&manager->mutex);

    return RESOURCE_SUCCESS;
}

/* Statistics accessors */

int resource_get_available(ResourceManager *manager,
                           const char *resource_id)
{
    pthread_mutex_lock(&manager->mutex);

    SharedResource *r = find_resource(manager, resource_id);
    int result = r ? r->available : RESOURCE_NOT_FOUND;

    pthread_mutex_unlock(&manager->mutex);
    return result;
}

int resource_get_acquisitions(ResourceManager *manager,
                              const char *resource_id)
{
    pthread_mutex_lock(&manager->mutex);

    SharedResource *r = find_resource(manager, resource_id);
    int result = r ? r->acquisitions : RESOURCE_NOT_FOUND;

    pthread_mutex_unlock(&manager->mutex);
    return result;
}

int resource_get_wait_events(ResourceManager *manager,
                             const char *resource_id)
{
    pthread_mutex_lock(&manager->mutex);

    SharedResource *r = find_resource(manager, resource_id);
    int result = r ? r->wait_events : RESOURCE_NOT_FOUND;

    pthread_mutex_unlock(&manager->mutex);
    return result;
}

int resource_get_max_holders(ResourceManager *manager,
                             const char *resource_id)
{
    pthread_mutex_lock(&manager->mutex);

    SharedResource *r = find_resource(manager, resource_id);
    int result = r ? r->max_simultaneous_holders : RESOURCE_NOT_FOUND;

    pthread_mutex_unlock(&manager->mutex);
    return result;
}

int resource_get_violations(ResourceManager *manager)
{
    pthread_mutex_lock(&manager->mutex);
    int result = manager->exclusivity_violations;
    pthread_mutex_unlock(&manager->mutex);
    return result;
}
