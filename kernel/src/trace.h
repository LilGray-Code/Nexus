#ifndef NEXUS_TRACE_H
#define NEXUS_TRACE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NEXUS_TRACE_CAPACITY 128U

enum nexus_trace_event_type {
    NEXUS_TRACE_EVENT_BOOT = 1U,
    NEXUS_TRACE_EVENT_SCHEDULER_SWITCH = 2U
};

struct nexus_trace_event {
    uint64_t sequence;
    uint64_t timestamp_ticks;
    uint64_t argument0;
    uint64_t argument1;
    uint32_t type;
    uint32_t reserved;
};

/* Fixed-capacity, non-blocking event ring. Events are diagnostic hints, not a
 * stable ABI and not a substitute for cycle-accurate hardware tracing.
 */
void nexus_trace_init(void);
void nexus_trace_emit(uint32_t type, uint64_t timestamp_ticks,
                      uint64_t argument0, uint64_t argument1);
size_t nexus_trace_count(void);
/* reverse_index 0 is newest; returns false when no such event is retained. */
bool nexus_trace_get_recent(size_t reverse_index,
                            struct nexus_trace_event *event);
uint64_t nexus_trace_dropped(void);

#endif
