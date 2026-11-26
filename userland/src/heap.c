#include "nexus/runtime.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Bootstrap allocator for the early single-foreground-process runtime.
 * This is deliberately bounded and not thread-safe. It is not a replacement
 * for a future kernel-backed process heap or a complete ISO C allocator.
 */
#define NEXUS_HEAP_CAPACITY (64U * 1024U)

union nexus_heap_block {
    struct {
        size_t size;
        union nexus_heap_block *next;
        union nexus_heap_block *previous;
        bool free;
    } fields;
    max_align_t alignment;
};

static unsigned char heap_storage[NEXUS_HEAP_CAPACITY]
    __attribute__((aligned(__alignof__(max_align_t))));
static union nexus_heap_block *heap_first;
static bool heap_initialized;

static size_t heap_alignment(void) {
    return (size_t)_Alignof(max_align_t);
}

static bool align_size(size_t value, size_t *aligned) {
    const size_t alignment = heap_alignment();
    if (aligned == NULL || value > SIZE_MAX - (alignment - 1U))
        return false;
    *aligned = (value + alignment - 1U) & ~(alignment - 1U);
    return true;
}

static void heap_initialize_once(void) {
    if (heap_initialized) return;
    heap_first = (union nexus_heap_block *)(void *)heap_storage;
    heap_first->fields.size =
        sizeof(heap_storage) - sizeof(union nexus_heap_block);
    heap_first->fields.next = NULL;
    heap_first->fields.previous = NULL;
    heap_first->fields.free = true;
    heap_initialized = true;
}

static union nexus_heap_block *heap_find_payload(const void *pointer) {
    for (union nexus_heap_block *block = heap_first;
         block != NULL; block = block->fields.next) {
        if ((const void *)(block + 1) == pointer) return block;
    }
    return NULL;
}

static void heap_split(union nexus_heap_block *block, size_t wanted) {
    const size_t header_size = sizeof(union nexus_heap_block);
    const size_t alignment = heap_alignment();
    if (block == NULL || block->fields.size < wanted ||
        block->fields.size - wanted < header_size + alignment)
        return;

    union nexus_heap_block *remainder =
        (union nexus_heap_block *)(void *)
        ((unsigned char *)(block + 1) + wanted);
    remainder->fields.size = block->fields.size - wanted - header_size;
    remainder->fields.next = block->fields.next;
    remainder->fields.previous = block;
    remainder->fields.free = true;
    if (remainder->fields.next != NULL)
        remainder->fields.next->fields.previous = remainder;
    block->fields.next = remainder;
    block->fields.size = wanted;
}

static void heap_merge_next(union nexus_heap_block *block) {
    if (block == NULL || block->fields.next == NULL ||
        !block->fields.next->fields.free)
        return;
    union nexus_heap_block *next = block->fields.next;
    block->fields.size += sizeof(union nexus_heap_block) +
                          next->fields.size;
    block->fields.next = next->fields.next;
    if (block->fields.next != NULL)
        block->fields.next->fields.previous = block;
}

void nexus_heap_init(void) {
    heap_initialize_once();
}

void *nexus_malloc(size_t size) {
    if (size == 0U) return NULL;
    size_t wanted = 0U;
    if (!align_size(size, &wanted)) return NULL;
    heap_initialize_once();

    for (union nexus_heap_block *block = heap_first;
         block != NULL; block = block->fields.next) {
        if (!block->fields.free || block->fields.size < wanted) continue;
        heap_split(block, wanted);
        block->fields.free = false;
        return (void *)(block + 1);
    }
    return NULL;
}

void nexus_free(void *pointer) {
    if (pointer == NULL) return;
    heap_initialize_once();
    union nexus_heap_block *block = heap_find_payload(pointer);
    if (block == NULL || block->fields.free) return;

    block->fields.free = true;
    heap_merge_next(block);
    if (block->fields.previous != NULL &&
        block->fields.previous->fields.free) {
        block = block->fields.previous;
        heap_merge_next(block);
    }
}

void *nexus_calloc(size_t count, size_t size) {
    if (count == 0U || size == 0U) return NULL;
    if (count > SIZE_MAX / size) return NULL;
    const size_t total = count * size;
    void *memory = nexus_malloc(total);
    if (memory == NULL) return NULL;
    (void)nexus_memset(memory, 0, total);
    return memory;
}

void *nexus_realloc(void *pointer, size_t size) {
    if (pointer == NULL) return nexus_malloc(size);
    if (size == 0U) {
        nexus_free(pointer);
        return NULL;
    }

    size_t wanted = 0U;
    if (!align_size(size, &wanted)) return NULL;
    heap_initialize_once();
    union nexus_heap_block *block = heap_find_payload(pointer);
    if (block == NULL || block->fields.free) return NULL;

    if (block->fields.size >= wanted) {
        heap_split(block, wanted);
        return pointer;
    }

    void *replacement = nexus_malloc(size);
    if (replacement == NULL) return NULL;
    const size_t copy_size =
        block->fields.size < size ? block->fields.size : size;
    (void)nexus_memcpy(replacement, pointer, copy_size);
    nexus_free(pointer);
    return replacement;
}
