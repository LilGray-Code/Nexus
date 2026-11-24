#ifndef NEXUS_SCHEDULER_H
#define NEXUS_SCHEDULER_H

#include <stdbool.h>

typedef void (*nexus_thread_entry_t)(void *argument);

void scheduler_init(void);
bool scheduler_create_thread(nexus_thread_entry_t entry, void *argument);
void scheduler_start(void);
void scheduler_wait(void);
void scheduler_yield(void);
void scheduler_preempt(void);
void scheduler_set_preemption_enabled(bool enabled);

#endif
