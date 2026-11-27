#define _POSIX_C_SOURCE 200809L

#include "log_buffer.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#define CAPACITY NEXUS_LOG_BUFFER_CAPACITY
#define APPEND_ITERATIONS 1000000U
#define SMALL_READ_ITERATIONS 1000000U
#define FULL_READ_ITERATIONS 10000U

static char legacy_bytes[CAPACITY];
static uint64_t legacy_head;
static volatile uint8_t legacy_lock;
static volatile uint64_t checksum;

static int legacy_try_lock(void) {
    return !__atomic_test_and_set(&legacy_lock, __ATOMIC_ACQUIRE);
}

static void legacy_unlock(void) {
    __atomic_clear(&legacy_lock, __ATOMIC_RELEASE);
}

/* Baseline implementation from before the optimization: one modulo operation
 * for every byte copied into and out of the ring.
 */
static void legacy_init(void) {
    if (!legacy_try_lock()) return;
    for (size_t i = 0U; i < CAPACITY; ++i) legacy_bytes[i] = '\0';
    legacy_head = 0U;
    legacy_unlock();
}

static void legacy_append(const char *text, size_t length) {
    if (text == NULL || length == 0U || !legacy_try_lock()) return;
    for (size_t i = 0U; i < length; ++i) {
        legacy_bytes[(size_t)(legacy_head % CAPACITY)] = text[i];
        ++legacy_head;
    }
    legacy_unlock();
}

static size_t legacy_read(char *output, size_t capacity) {
    if (output == NULL || capacity == 0U) return 0U;
    output[0] = '\0';
    if (!legacy_try_lock()) return 0U;

    uint64_t count64 = legacy_head;
    if (count64 > CAPACITY) count64 = CAPACITY;
    if (count64 >= capacity) count64 = capacity - 1U;
    const size_t count = (size_t)count64;
    const uint64_t start = legacy_head - count64;
    for (size_t i = 0U; i < count; ++i)
        output[i] = legacy_bytes[(size_t)((start + i) % CAPACITY)];
    output[count] = '\0';
    legacy_unlock();
    return count;
}

static uint64_t now_ns(void) {
    struct timespec value;
    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0) return 0U;
    return (uint64_t)value.tv_sec * 1000000000ULL +
           (uint64_t)value.tv_nsec;
}

typedef void (*append_fn)(const char *, size_t);
typedef size_t (*read_fn)(char *, size_t);

static uint64_t bench_append(append_fn append, const char *payload,
                             size_t length, size_t iterations) {
    const uint64_t start = now_ns();
    for (size_t i = 0U; i < iterations; ++i) append(payload, length);
    const uint64_t end = now_ns();
    return end >= start ? end - start : 0U;
}

static uint64_t bench_read(read_fn read_recent, size_t capacity,
                           size_t iterations) {
    char output[CAPACITY + 1U];
    const uint64_t start = now_ns();
    for (size_t i = 0U; i < iterations; ++i) {
        const size_t count = read_recent(output, capacity);
        if (count != 0U) checksum += (uint8_t)output[i % count];
    }
    const uint64_t end = now_ns();
    return end >= start ? end - start : 0U;
}

static void report(const char *name, uint64_t legacy, uint64_t optimized,
                   size_t iterations) {
    const uint64_t old_avg = iterations != 0U ? legacy / iterations : 0U;
    const uint64_t new_avg = iterations != 0U ? optimized / iterations : 0U;
    const double speedup = optimized != 0U
        ? (double)legacy / (double)optimized : 0.0;
    printf("%s: baseline=%" PRIu64 " ns total (%" PRIu64
           " ns/op), optimized=%" PRIu64 " ns total (%" PRIu64
           " ns/op), speedup=%.2fx\n",
           name, legacy, old_avg, optimized, new_avg, speedup);
}

int main(void) {
    static const char payload[] =
        "NEXUS performance benchmark data 0123456789";
    char legacy_output[CAPACITY + 1U];
    char optimized_output[CAPACITY + 1U];

    legacy_init();
    nexus_log_buffer_init();

    const size_t payload_length = sizeof(payload) - 1U;
    const uint64_t legacy_append_ns = bench_append(
        legacy_append, payload, payload_length, APPEND_ITERATIONS);
    const uint64_t optimized_append_ns = bench_append(
        nexus_log_buffer_append, payload, payload_length, APPEND_ITERATIONS);
    report("append-payload", legacy_append_ns, optimized_append_ns,
           APPEND_ITERATIONS);

    (void)legacy_read(legacy_output, sizeof(legacy_output));
    (void)nexus_log_buffer_read_recent(optimized_output,
                                       sizeof(optimized_output));
    const uint64_t legacy_small = bench_read(
        legacy_read, 112U, SMALL_READ_ITERATIONS);
    const uint64_t optimized_small = bench_read(
        nexus_log_buffer_read_recent, 112U, SMALL_READ_ITERATIONS);
    report("read-112B-capacity", legacy_small, optimized_small,
           SMALL_READ_ITERATIONS);

    const uint64_t legacy_full = bench_read(
        legacy_read, CAPACITY + 1U, FULL_READ_ITERATIONS);
    const uint64_t optimized_full = bench_read(
        nexus_log_buffer_read_recent, CAPACITY + 1U, FULL_READ_ITERATIONS);
    report("read-4KiB", legacy_full, optimized_full,
           FULL_READ_ITERATIONS);

    printf("checksum=%" PRIu64 " (prevents dead-code elimination)\n",
           checksum);
    return 0;
}
