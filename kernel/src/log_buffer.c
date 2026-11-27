#include "log_buffer.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static char log_bytes[NEXUS_LOG_BUFFER_CAPACITY];
static uint64_t log_head;
static uint64_t log_dropped;
static volatile uint8_t log_lock;

_Static_assert((NEXUS_LOG_BUFFER_CAPACITY &
                (NEXUS_LOG_BUFFER_CAPACITY - 1U)) == 0U,
               "log buffer capacity must be a power of two");

static bool log_try_lock(void) {
    return !__atomic_test_and_set(&log_lock, __ATOMIC_ACQUIRE);
}

static void log_unlock(void) {
    __atomic_clear(&log_lock, __ATOMIC_RELEASE);
}

void nexus_log_buffer_init(void) {
    if (!log_try_lock()) return;
    log_head = 0U;
    log_dropped = 0U;
    log_unlock();
}

static void log_copy_bytes(char *destination, const char *source,
                            size_t length) {
    if (length == 0U) return;
    __asm__ volatile ("cld; rep movsb"
                      : "+D"(destination), "+S"(source), "+c"(length)
                      :
                      : "memory", "cc");
}

static void log_copy_into_ring(size_t destination, const char *source,
                               size_t length) {
    const size_t first = length < NEXUS_LOG_BUFFER_CAPACITY - destination
        ? length : NEXUS_LOG_BUFFER_CAPACITY - destination;
    log_copy_bytes(log_bytes + destination, source, first);
    log_copy_bytes(log_bytes, source + first, length - first);
}

void nexus_log_buffer_append(const char *text, size_t length) {
    if (text == NULL || length == 0U) return;
    if (!log_try_lock()) {
        (void)__atomic_add_fetch(&log_dropped, 1U, __ATOMIC_RELAXED);
        return;
    }

    /* Preserve the low-overhead scalar path for tiny records. */
    if (length < 16U) {
        for (size_t i = 0U; i < length; ++i) {
            log_bytes[(size_t)(log_head % NEXUS_LOG_BUFFER_CAPACITY)] = text[i];
            ++log_head;
        }
        log_unlock();
        return;
    }

    size_t copy_length = length;
    size_t destination =
        (size_t)(log_head & (NEXUS_LOG_BUFFER_CAPACITY - 1U));
    if (copy_length >= NEXUS_LOG_BUFFER_CAPACITY) {
        const size_t skipped = copy_length - NEXUS_LOG_BUFFER_CAPACITY;
        text += skipped;
        copy_length = NEXUS_LOG_BUFFER_CAPACITY;
        destination = (destination +
            (skipped & (NEXUS_LOG_BUFFER_CAPACITY - 1U))) &
            (NEXUS_LOG_BUFFER_CAPACITY - 1U);
    }

    log_copy_into_ring(destination, text, copy_length);
    log_head += (uint64_t)length;
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
    const size_t start = (size_t)((log_head - count64) &
        (NEXUS_LOG_BUFFER_CAPACITY - 1U));
    const size_t first = count < NEXUS_LOG_BUFFER_CAPACITY - start
        ? count : NEXUS_LOG_BUFFER_CAPACITY - start;

    log_copy_bytes(output, log_bytes + start, first);
    log_copy_bytes(output + first, log_bytes, count - first);
    output[count] = '\0';

    log_unlock();
    return count;
}

uint64_t nexus_log_buffer_dropped(void) {
    return __atomic_load_n(&log_dropped, __ATOMIC_RELAXED);
}
