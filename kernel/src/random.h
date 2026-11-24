#ifndef NEXUS_RANDOM_H
#define NEXUS_RANDOM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Hardware random source for cryptographic consumers.
 *
 * The API deliberately has no timestamp, counter, or software-PRNG fallback.
 * Callers that need cryptographic nonces or secrets must fail closed when
 * the CPU does not advertise RDRAND or the instruction reports failure.
 */
bool nexus_rng_available(void);
bool nexus_rng_fill(void *output, size_t length);
bool nexus_rng_u32(uint32_t *value);

#endif
