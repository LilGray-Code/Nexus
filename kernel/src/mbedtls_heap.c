#include "tls_heap.h"

#include "mbedtls/platform.h"

#include <stddef.h>

/*
 * Register Nexus's fixed 1 MiB heap with Mbed TLS before psa_crypto_init()
 * or any TLS context allocation. No libc malloc fallback is provided.
 */
bool nexus_mbedtls_heap_init(void) {
    if (!nexus_tls_heap_init()) return false;
    return mbedtls_platform_set_calloc_free(
        nexus_tls_heap_calloc, nexus_tls_heap_free) == 0;
}
