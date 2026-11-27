#include "log_buffer.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static char log_bytes[NEXUS_LOG_BUFFER_CAPACITY];
static size_t log_write_index;
static size_t log_count;
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

/* The count is reset instead of clearing the whole byte array. No stale byte
 * is observable while count is zero, and initialization stays O(1).
 */
void nexus_log_buffer_init(void) {
    if (!log_try_lock()) return;
    log_write_index = 0U;
    log_count = 0U;
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

    /* An append at least as large as the ring supersedes all old data. Keep
     * only the newest capacity bytes and write them from index zero.
     */
    if (length >= NEXUS_LOG_BUFFER_CAPACITY) {
        text += length - NEXUS_LOG_BUFFER_CAPACITY;
        length = NEXUS_LOG_BUFFER_CAPACITY;
        log_write_index = 0U;
        log_count = 0U;
    }

    log_copy_into_ring(log_write_index, text, length);
    log_write_index += length;
    if (log_write_index >= NEXUS_LOG_BUFFER_CAPACITY)
        log_write_index -= NEXUS_LOG_BUFFER_CAPACITY;

    if (length >= NEXUS_LOG_BUFFER_CAPACITY - log_count)
        log_count = NEXUS_LOG_BUFFER_CAPACITY;
    else
        log_count += length;

    log_unlock();
}

size_t nexus_log_buffer_read_recent(char *output, size_t capacity) {
    if (output == NULL || capacity == 0U) return 0U;
    output[0] = '\0';
    if (!log_try_lock()) return 0U;

    const size_t count = log_count < capacity - 1U
        ? log_count : capacity - 1U;
    const size_t start = log_write_index >= count
        ? log_write_index - count
        : NEXUS_LOG_BUFFER_CAPACITY - (count - log_write_index);
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
