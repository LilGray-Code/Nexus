#include "paging.h"

#include "memory.h"

#include <stddef.h>

#define PAGE_PRESENT 0x001ULL
#define PAGE_WRITABLE 0x002ULL
#define PAGE_WRITE_THROUGH 0x008ULL
#define PAGE_CACHE_DISABLE 0x010ULL
#define PAGE_ADDRESS_MASK 0x000FFFFFFFFFF000ULL
#define CR4_LA57 (1ULL << 12)

static bool allocate_table(uint64_t *physical,
                           volatile uint64_t **virtual_table) {
    if (physical == NULL || virtual_table == NULL) return false;
    if (!memory_alloc_page(physical)) return false;

    volatile uint64_t *table =
        (volatile uint64_t *)memory_phys_to_virt(*physical);
    if (table == NULL) return false;

    for (unsigned int i = 0; i < 512; ++i) table[i] = 0;
    *virtual_table = table;
    return true;
}

/*
 * Add a dedicated high-half alias for one MMIO page. The direct-map offset
 * is not assumed to cover device memory. We create a fresh top-level slot,
 * allocate intermediate page tables from the bootstrap allocator, map the
 * leaf as PWT+PCD (uncacheable), and invalidate the new virtual address.
 *
 * Both four-level and five-level paging are handled according to CR4.LA57.
 */
bool paging_map_mmio_page(uint64_t physical_address,
                          volatile uint32_t **virtual_address) {
    if (virtual_address == NULL) return false;

    const uint64_t physical_page = physical_address & PAGE_ADDRESS_MASK;
    uint64_t cr3, cr4;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile ("mov %%cr4, %0" : "=r"(cr4));

    const unsigned int levels = (cr4 & CR4_LA57) ? 5U : 4U;
    const uint64_t root_physical = cr3 & PAGE_ADDRESS_MASK;
    volatile uint64_t *root =
        (volatile uint64_t *)memory_phys_to_virt(root_physical);
    if (root == NULL) return false;

    /* Keep the new mapping outside the usual HHDM and kernel PML4/PML5 slots. */
    unsigned int root_index = 256;
    while (root_index < 511 && (root[root_index] & PAGE_PRESENT) != 0) {
        ++root_index;
    }
    if (root_index >= 511) return false;

    const uint64_t virtual_base = levels == 5U
        ? (0xFF00000000000000ULL | ((uint64_t)root_index << 48))
        : (0xFFFF000000000000ULL | ((uint64_t)root_index << 39));

    volatile uint64_t *parent = root;
    for (unsigned int level = levels; level > 1U; --level) {
        uint64_t next_physical;
        volatile uint64_t *next_table;
        if (!allocate_table(&next_physical, &next_table)) return false;

        parent[(level == levels) ? root_index : 0] =
            (next_physical & PAGE_ADDRESS_MASK) |
            PAGE_PRESENT | PAGE_WRITABLE;
        parent = next_table;
    }

    parent[0] = physical_page | PAGE_PRESENT | PAGE_WRITABLE |
                PAGE_WRITE_THROUGH | PAGE_CACHE_DISABLE;

    __asm__ volatile ("invlpg (%0)" : : "r"((uintptr_t)virtual_base) : "memory");
    *virtual_address = (volatile uint32_t *)(uintptr_t)virtual_base;
    return true;
}
