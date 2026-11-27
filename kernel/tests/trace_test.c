#include "trace.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

int main(void) {
    struct nexus_trace_event event;
    nexus_trace_init();
    assert(nexus_trace_count() == 0U);
    assert(!nexus_trace_get_recent(0U, &event));

    nexus_trace_emit(NEXUS_TRACE_EVENT_BOOT, 0U, 10U, 20U);
    nexus_trace_emit(NEXUS_TRACE_EVENT_SCHEDULER_SWITCH, 99U, 1U, 2U);
    assert(nexus_trace_count() == 2U);
    assert(nexus_trace_get_recent(0U, &event));
    assert(event.type == NEXUS_TRACE_EVENT_SCHEDULER_SWITCH);
    assert(event.timestamp_ticks == 99U);
    assert(event.argument0 == 1U && event.argument1 == 2U);
    assert(event.sequence == 1U);

    for (uint64_t i = 0U; i < NEXUS_TRACE_CAPACITY + 5U; ++i)
        nexus_trace_emit(NEXUS_TRACE_EVENT_SCHEDULER_SWITCH, i, i + 1U, i + 2U);

    assert(nexus_trace_count() == NEXUS_TRACE_CAPACITY);
    assert(nexus_trace_get_recent(0U, &event));
    assert(event.timestamp_ticks == NEXUS_TRACE_CAPACITY + 4U);
    assert(nexus_trace_get_recent(NEXUS_TRACE_CAPACITY - 1U, &event));
    assert(event.sequence == 7U);
    assert(!nexus_trace_get_recent(NEXUS_TRACE_CAPACITY, &event));
    assert(nexus_trace_dropped() == 0U);
    return 0;
}
