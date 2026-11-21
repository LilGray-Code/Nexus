#include "memory.h"

#include <stddef.h>
#include <stdint.h>

#define MEMORY_MAX_REGIONS 256U
#define MEMORY_FIRST_ALLOCATABLE 0x100000ULL

struct memory_region {
    uint64_t next;
    uint64_t end;
};

static struct memory_region usable_regions[MEMORY_MAX_REGIONS];
static size_t usable_region_count;
static size_t current_region;
static uint64_t direct_map_offset;
static uint64_t total_bytes;
static uint64_t free_bytes;
static bool initialized;

static bool add_overflow_u64(uint64_t a, uint64_t b, uint64_t *result) {
    *result = a + b;
    return *result < a;
}

bool memory_init(const struct limine_memmap_response *map,
                 const struct limine_hhdm_response *hhdm) {
    usable_region_count = 0;
    current_region = 0;
    total_bytes = 0;
    free_bytes = 0;
    initialized = false;

    if (map == NULL || hhdm == NULL || map->entries == NULL) return false;
    if (map->entry_count > 4096) return false;

    direct_map_offset = hhdm->offset;

    for (uint64_t i = 0; i < map->entry_count; ++i) {
        const struct limine_memmap_entry *entry = map->entries[i];
        uint64_t end;

        if (entry == NULL || entry->type != LIMINE_MEMMAP_USABLE ||
            entry->length == 0) {
            continue;
        }

        if (add_overflow_u64(entry->base, entry->length, &end)) continue;

        uint64_t start = entry->base;
        if (start < MEMORY_FIRST_ALLOCATABLE) {
            start = MEMORY_FIRST_ALLOCATABLE;
        }
        if (start > UINT64_MAX - (NEXUS_PAGE_SIZE - 1)) continue;

        start = (start + (NEXUS_PAGE_SIZE - 1)) & ~(NEXUS_PAGE_SIZE - 1);
        end &= ~(NEXUS_PAGE_SIZE - 1);
        if (start >= end) continue;
        if (usable_region_count >= MEMORY_MAX_REGIONS) break;

        const uint64_t bytes = end - start;
        usable_regions[usable_region_count].next = start;
        usable_regions[usable_region_count].end = end;
        ++usable_region_count;

        if (UINT64_MAX - total_bytes < bytes) {
            total_bytes = UINT64_MAX;
        } else {
            total_bytes += bytes;
        }
        if (UINT64_MAX - free_bytes < bytes) {
            free_bytes = UINT64_MAX;
        } else {
            free_bytes += bytes;
        }
    }

    initialized = usable_region_count != 0;
    return initialized;
}

bool memory_alloc_page(uint64_t *physical_address) {
    if (!initialized || physical_address == NULL) return false;

    while (current_region < usable_region_count) {
        struct memory_region *region = &usable_regions[current_region];
        if (region->next <= region->end &&
            region->end - region->next >= NEXUS_PAGE_SIZE) {
            *physical_address = region->next;
            region->next += NEXUS_PAGE_SIZE;
            if (free_bytes >= NEXUS_PAGE_SIZE) {
                free_bytes -= NEXUS_PAGE_SIZE;
            } else {
                free_bytes = 0;
            }
            return true;
        }
        ++current_region;
    }

    return false;
}

/*
 * Allocate one page below a device's DMA address limit. This is deliberately
 * page-sized only: callers that need multiple contiguous pages must request
 * and validate each run through a future contiguous allocator.
 */
bool memory_alloc_dma_page(uint64_t max_address_exclusive,
                           uint64_t *physical_address) {
    if (!initialized || physical_address == NULL ||
        max_address_exclusive < NEXUS_PAGE_SIZE) {
        return false;
    }

    for (size_t i = 0; i < usable_region_count; ++i) {
        struct memory_region *region = &usable_regions[i];
        if (region->next >= region->end) continue;

        uint64_t limit = region->end;
        if (limit > max_address_exclusive) limit = max_address_exclusive;
        if (region->next > limit ||
            limit - region->next < NEXUS_PAGE_SIZE) {
            continue;
        }

        *physical_address = region->next;
        region->next += NEXUS_PAGE_SIZE;
        if (free_bytes >= NEXUS_PAGE_SIZE) {
            free_bytes -= NEXUS_PAGE_SIZE;
        } else {
            free_bytes = 0;
        }
        return true;
    }

    return false;
}

void *memory_phys_to_virt(uint64_t physical_address) {
    if (!initialized || physical_address > UINTPTR_MAX - direct_map_offset) {
        return NULL;
    }
    return (void *)(uintptr_t)(direct_map_offset + physical_address);
}

uint64_t memory_total_bytes(void) {
    return total_bytes;
}

uint64_t memory_free_bytes(void) {
    return free_bytes;
}
