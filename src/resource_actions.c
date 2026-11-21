#include "resource_actions.h"

#include <stdio.h>
#include <string.h>

void resource_plan_init(PatientResourcePlan *plan)
{
    if (plan == NULL) {
        return;
    }

    memset(plan, 0, sizeof(*plan));
}

int resource_plan_add(
    PatientResourcePlan *plan,
    int work_offset,
    ResourceActionType type,
    const char *resource_id,
    int units
)
{
    if (plan == NULL || resource_id == NULL) {
        return -1;
    }

    if (type != RESOURCE_ACTION_ACQUIRE &&
        type != RESOURCE_ACTION_RELEASE) {
        return -1;
    }

    if (work_offset < 0 || units <= 0) {
        return -1;
    }

    if (plan->count >= MAX_RESOURCE_ACTIONS) {
        return -1;
    }

    size_t id_length = strlen(resource_id);

    if (id_length == 0 || id_length >= RESOURCE_ID_LENGTH) {
        return -1;
    }

    /* Actions for a patient must be in nondecreasing offset order. */
    if (plan->count > 0 &&
        work_offset < plan->actions[plan->count - 1].work_offset) {
        return -1;
    }

    ResourceAction *action = &plan->actions[plan->count];

    action->work_offset = work_offset;
    action->type = type;
    action->units = units;

    strcpy(action->resource_id, resource_id);

    plan->count++;

    return 0;
}

int resource_plan_validate(const PatientResourcePlan *plan,
                           char *why, size_t why_size)
{
    const char *held[MAX_RESOURCE_ACTIONS];
    int held_count = 0;

    for (int i = 0; i < plan->count; i++) {
        const ResourceAction *a = &plan->actions[i];
        int at = -1;

        for (int j = 0; j < held_count; j++) {
            if (strcmp(held[j], a->resource_id) == 0) {
                at = j;
            }
        }

        if (a->type == RESOURCE_ACTION_ACQUIRE) {
            if (at >= 0) {
                snprintf(why, why_size,
                         "%s is acquired twice without a release",
                         a->resource_id);
                return -1;
            }
            held[held_count++] = a->resource_id;
        } else {
            if (at < 0) {
                snprintf(why, why_size,
                         "%s is released but not held", a->resource_id);
                return -1;
            }
            held[at] = held[--held_count];
        }
    }

    if (held_count > 0) {
        snprintf(why, why_size, "%s is never released", held[0]);
        return -1;
    }
    return 0;
}

ResourceConfig g_resource_config;

void resource_config_reset(void)
{
    memset(&g_resource_config, 0, sizeof(g_resource_config));
}

int resource_config_has(const char *id)
{
    for (int i = 0; i < g_resource_config.count; i++) {
        if (strcmp(g_resource_config.defs[i].id, id) == 0) {
            return 1;
        }
    }
    return 0;
}

int resource_config_add(const char *id, int capacity)
{
    if (g_resource_config.count >= MAX_RESOURCES ||
        strlen(id) >= RESOURCE_ID_LENGTH || capacity < 1 ||
        resource_config_has(id)) {
        return -1;
    }

    ResourceDef *def = &g_resource_config.defs[g_resource_config.count++];
    strcpy(def->id, id);
    def->capacity = capacity;
    return 0;
}
