#include "tls_heap.h"
#include "rtc.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

/* PSA uses the same bounded heap as the Mbed TLS platform allocator. */
void *calloc(size_t count, size_t size) {
    return nexus_tls_heap_calloc(count, size);
}

void free(void *pointer) {
    nexus_tls_heap_free(pointer);
}

/* Mbed TLS builds with HAVE_TIME use time() for epoch access. Route it to
 * Nexus RTC; an invalid RTC remains an error instead of a guessed date.
 */
time_t time(time_t *output) {
    uint64_t seconds = 0U;
    if (!rtc_get_unix_time(&seconds) || seconds > (uint64_t)INT64_MAX) {
        if (output != NULL) *output = (time_t)-1;
        return (time_t)-1;
    }
    const time_t result = (time_t)seconds;
    if (output != NULL) *output = result;
    return result;
}
