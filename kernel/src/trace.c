#include "trace.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static struct nexus_trace_event trace_events[NEXUS_TRACE_CAPACITY];
static size_t trace_next;
static size_t trace_count_value;
static uint64_t trace_sequence;
static uint64_t trace_dropped_value;
static volatile uint8_t trace_lock;

static bool trace_try_lock(void) {
    return !__atomic_test_and_set(&trace_lock, __ATOMIC_ACQUIRE);
}

static void trace_unlock(void) {
    __atomic_clear(&trace_lock, __ATOMIC_RELEASE);
}

void nexus_trace_init(void) {
    if (!trace_try_lock()) return;
    trace_next = 0U;
    trace_count_value = 0U;
    trace_sequence = 0U;
    trace_dropped_value = 0U;
    for (size_t i = 0U; i < NEXUS_TRACE_CAPACITY; ++i) {
        trace_events[i].sequence = 0U;
        trace_events[i].timestamp_ticks = 0U;
        trace_events[i].argument0 = 0U;
        trace_events[i].argument1 = 0U;
        trace_events[i].type = 0U;
        trace_events[i].reserved = 0U;
    }
    trace_unlock();
}

void nexus_trace_emit(uint32_t type, uint64_t timestamp_ticks,
                      uint64_t argument0, uint64_t argument1) {
    if (type == 0U) return;
    if (!trace_try_lock()) {
        (void)__atomic_add_fetch(&trace_dropped_value, 1U, __ATOMIC_RELAXED);
        return;
    }

    struct nexus_trace_event *event = &trace_events[trace_next];
    event->sequence = trace_sequence++;
    event->timestamp_ticks = timestamp_ticks;
    event->argument0 = argument0;
    event->argument1 = argument1;
    event->type = type;
    event->reserved = 0U;

    trace_next = (trace_next + 1U) % NEXUS_TRACE_CAPACITY;
    if (trace_count_value < NEXUS_TRACE_CAPACITY) ++trace_count_value;
    trace_unlock();
}

size_t nexus_trace_count(void) {
    if (!trace_try_lock()) return 0U;
    const size_t count = trace_count_value;
    trace_unlock();
    return count;
}

bool nexus_trace_get_recent(size_t reverse_index,
                            struct nexus_trace_event *event) {
    if (event == NULL || !trace_try_lock()) return false;
    if (reverse_index >= trace_count_value) {
        trace_unlock();
        return false;
    }

    const size_t index =
        (trace_next + NEXUS_TRACE_CAPACITY - 1U - reverse_index) %
        NEXUS_TRACE_CAPACITY;
    *event = trace_events[index];
    trace_unlock();
    return true;
}

uint64_t nexus_trace_dropped(void) {
    return __atomic_load_n(&trace_dropped_value, __ATOMIC_RELAXED);
}
