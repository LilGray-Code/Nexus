#include "tls_heap.h"

#include <assert.h>
#include <stdint.h>

int main(void) {
    assert(nexus_tls_heap_init());
    const size_t initial_free = nexus_tls_heap_free_bytes();
    assert(initial_free > 900U * 1024U);
    assert(nexus_tls_heap_largest_free_block() == initial_free);

    uint8_t *a = (uint8_t *)nexus_tls_heap_calloc(1U, 128U);
    uint8_t *b = (uint8_t *)nexus_tls_heap_calloc(8U, 128U);
    uint8_t *c = (uint8_t *)nexus_tls_heap_calloc(1U, 256U);
    assert(a != NULL && b != NULL && c != NULL);
    for (size_t i = 0U; i < 1024U; ++i) assert(b[i] == 0U);
    b[0] = 0xA5U;
    b[1023] = 0x5AU;

    const size_t used_free = nexus_tls_heap_free_bytes();
    assert(used_free < initial_free);

    nexus_tls_heap_free(b);
    nexus_tls_heap_free(b); /* Double free is rejected. */
    assert(nexus_tls_heap_free_bytes() > used_free);

    uint8_t *d = (uint8_t *)nexus_tls_heap_calloc(8U, 128U);
    assert(d == b);
    for (size_t i = 0U; i < 1024U; ++i) assert(d[i] == 0U);
    nexus_tls_heap_free(d);

    nexus_tls_heap_free(a);
    nexus_tls_heap_free(c);
    nexus_tls_heap_free((void *)(uintptr_t)0x1234U);
    assert(nexus_tls_heap_free_bytes() == initial_free);
    assert(nexus_tls_heap_largest_free_block() == initial_free);

    assert(nexus_tls_heap_calloc(SIZE_MAX, 2U) == NULL);
    assert(nexus_tls_heap_calloc(1U, 0U) == NULL);

    void *all = nexus_tls_heap_calloc(1U, NEXUS_TLS_HEAP_CAPACITY);
    assert(all == NULL);
    return 0;
}
