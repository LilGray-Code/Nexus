#include "log_buffer.h"

#include <stdbool.h>
#include <stdint.h>

static char log_bytes[NEXUS_LOG_BUFFER_CAPACITY];
static uint64_t log_head;
static uint64_t log_dropped;
static volatile uint8_t log_lock;

static bool log_try_lock(void) {
    return !__atomic_test_and_set(&log_lock, __ATOMIC_ACQUIRE);
}

static void log_unlock(void) {
    __atomic_clear(&log_lock, __ATOMIC_RELEASE);
}

void nexus_log_buffer_init(void) {
    if (!log_try_lock()) return;
    for (size_t i = 0U; i < NEXUS_LOG_BUFFER_CAPACITY; ++i)
        log_bytes[i] = '\0';
    log_head = 0U;
    log_dropped = 0U;
    log_unlock();
}

void nexus_log_buffer_append(const char *text, size_t length) {
    if (text == NULL || length == 0U) return;
    if (!log_try_lock()) {
        (void)__atomic_add_fetch(&log_dropped, 1U, __ATOMIC_RELAXED);
        return;
    }

    for (size_t i = 0U; i < length; ++i) {
        log_bytes[(size_t)(log_head % NEXUS_LOG_BUFFER_CAPACITY)] = text[i];
        ++log_head;
    }
    log_unlock();
}

size_t nexus_log_buffer_read_recent(char *output, size_t capacity) {
    if (output == NULL || capacity == 0U) return 0U;
    output[0] = '\0';
    if (!log_try_lock()) return 0U;

    uint64_t count64 = log_head;
    if (count64 > NEXUS_LOG_BUFFER_CAPACITY)
        count64 = NEXUS_LOG_BUFFER_CAPACITY;
    if (count64 >= capacity) count64 = capacity - 1U;

    const size_t count = (size_t)count64;
    const uint64_t start = log_head - count64;
    for (size_t i = 0U; i < count; ++i)
        output[i] = log_bytes[(size_t)((start + i) %
                                       NEXUS_LOG_BUFFER_CAPACITY)];
    output[count] = '\0';
    log_unlock();
    return count;
}

uint64_t nexus_log_buffer_dropped(void) {
    return __atomic_load_n(&log_dropped, __ATOMIC_RELAXED);
}
