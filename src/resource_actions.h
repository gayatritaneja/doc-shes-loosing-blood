#ifndef RESOURCE_ACTIONS_H
#define RESOURCE_ACTIONS_H

#include "scheduler.h"
#include "resource_manager.h"

/* Initialise a patient's resource plan. */
void resource_plan_init(PatientResourcePlan *plan);

/* Add an acquisition or release action at a service-work offset. */
int resource_plan_add(
    PatientResourcePlan *plan,
    int work_offset,
    ResourceActionType type,
    const char *resource_id,
    int units
);

/* Check that every ACQUIRE is matched by a later RELEASE and that nothing
 * is released or acquired twice. Returns 0, or -1 and fills `why`. */
int resource_plan_validate(const PatientResourcePlan *plan,
                           char *why, size_t why_size);

/* Resources declared in the input file (the R section). */
typedef struct {
    char id[RESOURCE_ID_LENGTH];
    int capacity;
} ResourceDef;

typedef struct {
    ResourceDef defs[MAX_RESOURCES];
    int count;
} ResourceConfig;

extern ResourceConfig g_resource_config;

void resource_config_reset(void);
int resource_config_add(const char *id, int capacity);   /* -1: duplicate */
int resource_config_has(const char *id);

#endif
