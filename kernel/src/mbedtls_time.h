#ifndef NEXUS_MBEDTLS_TIME_H
#define NEXUS_MBEDTLS_TIME_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

/*
 * Convert UTC Unix seconds into a C struct tm without libc or timezone
 * dependencies. Returns false when the value cannot be represented.
 */
bool nexus_unix_time_to_utc_tm(uint64_t unix_seconds, struct tm *output);

/* Install Nexus's RTC/monotonic timer callbacks into Mbed TLS. */
bool nexus_mbedtls_time_init(void);

#endif
