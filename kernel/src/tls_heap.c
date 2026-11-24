#include "tls_heap.h"

#include <limits.h>
#include <stdint.h>

#define TLS_HEAP_ALIGNMENT 16U
#define TLS_HEAP_BLOCK_MAGIC 0x4E585448U

struct tls_heap_block {
    size_t size;
    uint32_t magic;
    bool free;
    uint8_t reserved[3];
    struct tls_heap_block *previous;
    struct tls_heap_block *next;
} __attribute__((aligned(TLS_HEAP_ALIGNMENT)));

#define TLS_HEAP_HEADER_SIZE \
    ((sizeof(struct tls_heap_block) + TLS_HEAP_ALIGNMENT - 1U) & \
     ~(size_t)(TLS_HEAP_ALIGNMENT - 1U))

static uint8_t heap_storage[NEXUS_TLS_HEAP_CAPACITY]
    __attribute__((aligned(TLS_HEAP_ALIGNMENT)));
static struct tls_heap_block *first_block;
static bool initialized;

static uintptr_t lock_interrupts(void) {
#if defined(NEXUS_TLS_HEAP_HOST_TEST)
    return 0U;
#else
    uintptr_t flags = 0U;
    __asm__ volatile ("pushfq; popq %0; cli"
                      : "=r"(flags) : : "memory");
    return flags;
#endif
}

static void unlock_interrupts(uintptr_t flags) {
#if !defined(NEXUS_TLS_HEAP_HOST_TEST)
    if ((flags & (1U << 9U)) != 0U)
        __asm__ volatile ("sti" : : : "memory");
#else
    (void)flags;
#endif
}

static bool align_size(size_t requested, size_t *aligned) {
    if (aligned == NULL || requested > SIZE_MAX - (TLS_HEAP_ALIGNMENT - 1U))
        return false;
    *aligned = (requested + TLS_HEAP_ALIGNMENT - 1U) &
               ~(size_t)(TLS_HEAP_ALIGNMENT - 1U);
    return true;
}

bool nexus_tls_heap_init(void) {
    const uintptr_t flags = lock_interrupts();
    if (initialized) {
        unlock_interrupts(flags);
        return true;
    }

    if (NEXUS_TLS_HEAP_CAPACITY <= TLS_HEAP_HEADER_SIZE) {
        unlock_interrupts(flags);
        return false;
    }

    first_block = (struct tls_heap_block *)(void *)heap_storage;
    first_block->size = NEXUS_TLS_HEAP_CAPACITY - TLS_HEAP_HEADER_SIZE;
    first_block->magic = TLS_HEAP_BLOCK_MAGIC;
    first_block->free = true;
    first_block->reserved[0] = 0U;
    first_block->reserved[1] = 0U;
    first_block->reserved[2] = 0U;
    first_block->previous = NULL;
    first_block->next = NULL;
    initialized = true;
    unlock_interrupts(flags);
    return true;
}

static bool pointer_to_block(void *pointer, struct tls_heap_block **result) {
    if (pointer == NULL || result == NULL || !initialized) return false;
    const uintptr_t begin = (uintptr_t)heap_storage;
    const uintptr_t end = begin + sizeof(heap_storage);
    const uintptr_t address = (uintptr_t)pointer;
    if (address < begin + TLS_HEAP_HEADER_SIZE || address >= end)
        return false;
    const uintptr_t block_address = address - TLS_HEAP_HEADER_SIZE;
    if ((block_address & (TLS_HEAP_ALIGNMENT - 1U)) != 0U)
        return false;
    struct tls_heap_block *block =
        (struct tls_heap_block *)(void *)block_address;
    if (block->magic != TLS_HEAP_BLOCK_MAGIC ||
        (uint8_t *)(void *)block + TLS_HEAP_HEADER_SIZE !=
            (uint8_t *)pointer)
        return false;
    *result = block;
    return true;
}

void *nexus_tls_heap_calloc(size_t count, size_t size) {
    if (count == 0U || size == 0U || count > SIZE_MAX / size)
        return NULL;
    if (!nexus_tls_heap_init()) return NULL;

    size_t requested = count * size;
    size_t aligned = 0U;
    if (!align_size(requested, &aligned) || aligned == 0U)
        return NULL;

    const uintptr_t flags = lock_interrupts();
    struct tls_heap_block *block = first_block;
    while (block != NULL) {
        if (block->magic != TLS_HEAP_BLOCK_MAGIC) {
            unlock_interrupts(flags);
            return NULL;
        }
        if (block->free && block->size >= aligned) break;
        block = block->next;
    }

    if (block == NULL) {
        unlock_interrupts(flags);
        return NULL;
    }

    const size_t remaining = block->size - aligned;
    if (remaining >= TLS_HEAP_HEADER_SIZE + TLS_HEAP_ALIGNMENT) {
        uint8_t *split_address = (uint8_t *)(void *)block +
                                 TLS_HEAP_HEADER_SIZE + aligned;
        struct tls_heap_block *split =
            (struct tls_heap_block *)(void *)split_address;
        split->size = remaining - TLS_HEAP_HEADER_SIZE;
        split->magic = TLS_HEAP_BLOCK_MAGIC;
        split->free = true;
        split->reserved[0] = 0U;
        split->reserved[1] = 0U;
        split->reserved[2] = 0U;
        split->previous = block;
        split->next = block->next;
        if (split->next != NULL) split->next->previous = split;
        block->next = split;
        block->size = aligned;
    }

    block->free = false;
    uint8_t *payload = (uint8_t *)(void *)block + TLS_HEAP_HEADER_SIZE;
    for (size_t i = 0U; i < block->size; ++i) payload[i] = 0U;
    unlock_interrupts(flags);
    return payload;
}

static void merge_with_next(struct tls_heap_block *block) {
    struct tls_heap_block *next = block->next;
    if (next == NULL || !next->free ||
        next->magic != TLS_HEAP_BLOCK_MAGIC) return;
    if (block->size > SIZE_MAX - TLS_HEAP_HEADER_SIZE - next->size) return;

    block->size += TLS_HEAP_HEADER_SIZE + next->size;
    block->next = next->next;
    if (block->next != NULL) block->next->previous = block;
    next->magic = 0U;
    next->previous = NULL;
    next->next = NULL;
}

void nexus_tls_heap_free(void *pointer) {
    if (pointer == NULL) return;
    const uintptr_t flags = lock_interrupts();
    struct tls_heap_block *block = NULL;
    if (!pointer_to_block(pointer, &block) || block->free) {
        unlock_interrupts(flags);
        return;
    }

    volatile uint8_t *bytes = (volatile uint8_t *)pointer;
    for (size_t i = 0U; i < block->size; ++i) bytes[i] = 0U;
    block->free = true;

    merge_with_next(block);
    if (block->previous != NULL && block->previous->free &&
        block->previous->magic == TLS_HEAP_BLOCK_MAGIC) {
        block = block->previous;
        merge_with_next(block);
    }
    unlock_interrupts(flags);
}

size_t nexus_tls_heap_free_bytes(void) {
    const uintptr_t flags = lock_interrupts();
    size_t total = 0U;
    for (struct tls_heap_block *block = first_block; block != NULL;
         block = block->next) {
        if (block->magic != TLS_HEAP_BLOCK_MAGIC) break;
        if (block->free && total <= SIZE_MAX - block->size)
            total += block->size;
    }
    unlock_interrupts(flags);
    return total;
}

size_t nexus_tls_heap_largest_free_block(void) {
    const uintptr_t flags = lock_interrupts();
    size_t largest = 0U;
    for (struct tls_heap_block *block = first_block; block != NULL;
         block = block->next) {
        if (block->magic != TLS_HEAP_BLOCK_MAGIC) break;
        if (block->free && block->size > largest) largest = block->size;
    }
    unlock_interrupts(flags);
    return largest;
}
