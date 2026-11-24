#ifndef NEXUS_TLS_HEAP_H
#define NEXUS_TLS_HEAP_H

#include <stdbool.h>
#include <stddef.h>

#define NEXUS_TLS_HEAP_CAPACITY (1024U * 1024U)

/* Initialize the bounded allocator once. Safe to call more than once. */
bool nexus_tls_heap_init(void);
void *nexus_tls_heap_calloc(size_t count, size_t size);
void nexus_tls_heap_free(void *pointer);
size_t nexus_tls_heap_free_bytes(void);
size_t nexus_tls_heap_largest_free_block(void);

#endif
