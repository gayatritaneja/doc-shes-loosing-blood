#include <string.h>
#include "scheduler.h"

#define MEMORY_QUEUE_CAPACITY MAX_PATIENTS

static MemoryAllocationAlgorithm selected_allocation_algorithm =
    MEMORY_FIRST_FIT;

void memory_set_allocation_algorithm(MemoryAllocationAlgorithm algorithm)
{
    selected_allocation_algorithm = algorithm;
}

MemoryAllocationAlgorithm memory_get_allocation_algorithm(void)
{
    return selected_allocation_algorithm;
}

void memory_manager_init(MemoryManager *manager)
{
    if (manager == NULL) {
        return;
    }

    memset(manager, 0, sizeof(*manager));
    for (int i = 0; i < MEMORY_SLOT_COUNT; i++) {
        manager->slots[i].free = 1;
    }
}

static void update_external_fragmentation(MemoryManager *manager)
{
    int free_slots = 0;
    int largest_run = 0;
    int current_run = 0;

    for (int i = 0; i < MEMORY_SLOT_COUNT; i++) {
        if (manager->slots[i].free) {
            free_slots++;
            current_run++;
            if (current_run > largest_run) {
                largest_run = current_run;
            }
        } else {
            current_run = 0;
        }
    }

    int fragmentation = free_slots - largest_run;
    if (fragmentation > manager->max_observed_external_fragmentation) {
        manager->max_observed_external_fragmentation = fragmentation;
    }
}

int memory_external_fragmentation(const MemoryManager *manager)
{
    if (manager == NULL) {
        return 0;
    }

    int free_slots = 0;
    int largest_run = 0;
    int current_run = 0;

    for (int i = 0; i < MEMORY_SLOT_COUNT; i++) {
        if (manager->slots[i].free) {
            free_slots++;
            current_run++;
            if (current_run > largest_run) {
                largest_run = current_run;
            }
        } else {
            current_run = 0;
        }
    }

    return free_slots - largest_run;
}

int memory_find_free_run(const MemoryManager *manager, int slots,
                         MemoryAllocationAlgorithm algorithm)
{
    if (manager == NULL || slots <= 0 || slots > MEMORY_SLOT_COUNT) {
        return -1;
    }

    int selected_start = -1;
    int selected_run_size = 0;

    for (int start = 0; start < MEMORY_SLOT_COUNT;) {
        if (!manager->slots[start].free) {
            start++;
            continue;
        }

        int end = start;
        while (end < MEMORY_SLOT_COUNT && manager->slots[end].free) {
            end++;
        }

        int run_size = end - start;
        if (run_size >= slots) {
            if (selected_start == -1) {
                selected_start = start;
                selected_run_size = run_size;
            } else if (algorithm == MEMORY_BEST_FIT &&
                       (run_size < selected_run_size ||
                        (run_size == selected_run_size && start < selected_start))) {
                selected_start = start;
                selected_run_size = run_size;
            } else if (algorithm == MEMORY_WORST_FIT &&
                       (run_size > selected_run_size ||
                        (run_size == selected_run_size && start < selected_start))) {
                selected_start = start;
                selected_run_size = run_size;
            } else if (algorithm == MEMORY_FIRST_FIT) {
                break;
            }
        }
        start = end;
    }

    return selected_start;
}

int memory_allocate(MemoryManager *manager, Patient *patient, int start,
                    int allocation_time)
{
    if (manager == NULL || patient == NULL ||
        start < 0 || patient->memory_requirement <= 0 ||
        start + patient->memory_requirement > MEMORY_SLOT_COUNT) {
        return 0;
    }

    for (int i = start;
         i < start + patient->memory_requirement;
         i++) {
        if (!manager->slots[i].free) {
            return 0;
        }
    }

    for (int i = start;
         i < start + patient->memory_requirement;
         i++) {
        manager->slots[i].free = 0;
        snprintf(manager->slots[i].owner, MAX_ID_LENGTH, "%s", patient->id);
    }

    patient->memory_start = start;
    patient->memory_slots = patient->memory_requirement;
    patient->memory_admission_time = allocation_time;
    update_external_fragmentation(manager);
    return 1;
}

int memory_deallocate(MemoryManager *manager, Patient *patient)
{
    if (manager == NULL || patient == NULL ||
        patient->memory_slots <= 0 ||
        patient->memory_start < 0 ||
        patient->memory_start + patient->memory_slots > MEMORY_SLOT_COUNT) {
        return 0;
    }

    for (int i = patient->memory_start;
         i < patient->memory_start + patient->memory_slots;
         i++) {
        if (strcmp(manager->slots[i].owner, patient->id) != 0) {
            continue;
        }
        manager->slots[i].free = 1;
        manager->slots[i].owner[0] = '\0';
    }

    patient->memory_start = -1;
    patient->memory_slots = 0;
    update_external_fragmentation(manager);
    return 1;
}

void memory_admission_init(MemoryAdmissionQueue *queue)
{
    if (queue != NULL) {
        memset(queue, 0, sizeof(*queue));
    }
}

int memory_admission_enqueue(MemoryAdmissionQueue *queue, Patient *patient)
{
    if (queue == NULL || patient == NULL ||
        queue->size >= MEMORY_QUEUE_CAPACITY) {
        return 0;
    }

    int tail = (queue->head + queue->size) % MEMORY_QUEUE_CAPACITY;
    queue->items[tail] = patient;
    queue->size++;
    patient->state = PROCESS_BLOCKED_MEMORY;
    return 1;
}

Patient *memory_admission_front(MemoryAdmissionQueue *queue)
{
    if (queue == NULL || queue->size == 0) {
        return NULL;
    }
    return queue->items[queue->head];
}

void memory_admission_dequeue(MemoryAdmissionQueue *queue)
{
    if (queue == NULL || queue->size == 0) {
        return;
    }

    queue->items[queue->head] = NULL;
    queue->head = (queue->head + 1) % MEMORY_QUEUE_CAPACITY;
    queue->size--;
}

void memory_process_admission(MemoryManager *manager,
                              MemoryAdmissionQueue *queue,
                              MemoryAllocationAlgorithm algorithm,
                              int current_time)
{
    if (manager == NULL || queue == NULL) {
        return;
    }

    while (queue->size > 0) {
        Patient *patient = memory_admission_front(queue);
        int start = memory_find_free_run(
            manager, patient->memory_requirement, algorithm);

        if (start < 0) {
            break;
        }

        if (!memory_allocate(manager, patient, start, current_time)) {
            break;
        }

        patient->state = PROCESS_READY;
        memory_admission_dequeue(queue);
    }
}
