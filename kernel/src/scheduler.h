#ifndef NEXUS_SCHEDULER_H
#define NEXUS_SCHEDULER_H

#include <stdbool.h>
#include <stdint.h>

typedef void (*nexus_thread_entry_t)(void *argument);

#define NEXUS_SCHEDULER_MAX_THREADS 16U

void scheduler_init(void);
bool scheduler_create_thread(nexus_thread_entry_t entry, void *argument);
void scheduler_start(void);
void scheduler_wait(void);
void scheduler_yield(void);
/* Wake deadline-expired workers from the timer IRQ; does not context-switch. */
void scheduler_timer_tick(uint64_t now);
/* Sleep the current scheduler context for at least the requested timer ticks. */
bool scheduler_sleep_ticks(uint64_t ticks);
void scheduler_preempt(void);
void scheduler_set_preemption_enabled(bool enabled);

#endif
