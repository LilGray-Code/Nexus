#include "tls_heap.h"

#include "mbedtls/platform.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

static void *(*registered_calloc)(size_t, size_t);
static void (*registered_free)(void *);

int mbedtls_platform_set_calloc_free(void *(*calloc_func)(size_t, size_t),
                                     void (*free_func)(void *)) {
    registered_calloc = calloc_func;
    registered_free = free_func;
    return 0;
}

int main(void) {
    assert(nexus_mbedtls_heap_init());
    assert(registered_calloc != NULL);
    assert(registered_free != NULL);

    uint8_t *buffer = (uint8_t *)registered_calloc(16U, 16U);
    assert(buffer != NULL);
    for (size_t i = 0U; i < 256U; ++i) assert(buffer[i] == 0U);
    buffer[42] = 0xA5U;
    registered_free(buffer);
    assert(nexus_tls_heap_free_bytes() > 900U * 1024U);
    return 0;
}
