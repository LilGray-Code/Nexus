#include "memory.h"

#include <stddef.h>
#include <stdint.h>

#define MEMORY_MAX_REGIONS 256U
#define MEMORY_FIRST_ALLOCATABLE 0x100000ULL
#define MEMORY_MAX_FREED_PAGES 8192U

struct memory_region {
    uint64_t start;
    uint64_t next;
    uint64_t end;
};

static struct memory_region usable_regions[MEMORY_MAX_REGIONS];
static size_t usable_region_count;
static size_t current_region;
static uint64_t direct_map_offset;
static uint64_t total_bytes;
static uint64_t free_bytes;
static uint64_t freed_pages[MEMORY_MAX_FREED_PAGES];
static size_t freed_page_count;
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
    freed_page_count = 0;
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
        usable_regions[usable_region_count].start = start;
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

    if (freed_page_count != 0U) {
        *physical_address = freed_pages[--freed_page_count];
        if (free_bytes >= NEXUS_PAGE_SIZE) free_bytes -= NEXUS_PAGE_SIZE;
        else free_bytes = 0;
        return true;
    }

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

    /* Reuse reclaimed pages that satisfy the device's DMA address ceiling. */
    for (size_t i = freed_page_count; i > 0U; --i) {
        const size_t index = i - 1U;
        const uint64_t candidate = freed_pages[index];
        if (candidate <= max_address_exclusive - NEXUS_PAGE_SIZE) {
            *physical_address = candidate;
            freed_pages[index] = freed_pages[freed_page_count - 1U];
            --freed_page_count;
            if (free_bytes >= NEXUS_PAGE_SIZE) free_bytes -= NEXUS_PAGE_SIZE;
            else free_bytes = 0;
            return true;
        }
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

/*
 * Return a page previously handed out by this allocator. The early allocator
 * uses a bounded LIFO free list; it rejects unaligned, foreign, and duplicate
 * frees. SMP synchronization and a larger bitmap allocator are future work.
 */
bool memory_free_page(uint64_t physical_address) {
    if (!initialized ||
        (physical_address & (NEXUS_PAGE_SIZE - 1U)) != 0U ||
        freed_page_count >= MEMORY_MAX_FREED_PAGES) {
        return false;
    }

    for (size_t i = 0; i < freed_page_count; ++i) {
        if (freed_pages[i] == physical_address) return false;
    }

    bool was_allocated = false;
    for (size_t i = 0; i < usable_region_count; ++i) {
        const struct memory_region *region = &usable_regions[i];
        if (physical_address >= region->start &&
            physical_address < region->next &&
            region->next - physical_address >= NEXUS_PAGE_SIZE &&
            physical_address < region->end) {
            was_allocated = true;
            break;
        }
    }
    if (!was_allocated) return false;

    freed_pages[freed_page_count++] = physical_address;
    if (UINT64_MAX - free_bytes < NEXUS_PAGE_SIZE) free_bytes = UINT64_MAX;
    else free_bytes += NEXUS_PAGE_SIZE;
    return true;
}

/*
 * Allocate a physically contiguous run from one usable memory region. This
 * is used for scanout backing that a paravirtual GPU needs to DMA as one
 * guest-memory entry. The early allocator is single-CPU and bump-pointer
 * based, so the run is never assembled from unrelated freed pages.
 */
bool memory_alloc_contiguous_pages(uint64_t page_count,
                                   uint64_t max_address_exclusive,
                                   uint64_t *physical_address) {
    if (!initialized || physical_address == NULL || page_count == 0U ||
        page_count > UINT64_MAX / NEXUS_PAGE_SIZE) return false;

    const uint64_t bytes = page_count * NEXUS_PAGE_SIZE;
    const uint64_t limit_address = max_address_exclusive == 0U
        ? UINT64_MAX : max_address_exclusive;
    for (size_t i = 0U; i < usable_region_count; ++i) {
        struct memory_region *region = &usable_regions[i];
        uint64_t limit = region->end;
        if (limit > limit_address) limit = limit_address;
        if (region->next > limit || limit - region->next < bytes) continue;

        const uint64_t start = region->next;
        region->next += bytes;
        *physical_address = start;
        if (free_bytes >= bytes) free_bytes -= bytes;
        else free_bytes = 0U;
        return true;
    }
    return false;
}

/*
 * Return a contiguous run to the bounded early free list. Validate the whole
 * run before mutating allocator state, so failure never leaves a partial free.
 */
bool memory_free_contiguous_pages(uint64_t physical_address,
                                  uint64_t page_count) {
    if (!initialized || page_count == 0U ||
        page_count > UINT64_MAX / NEXUS_PAGE_SIZE ||
        (physical_address & (NEXUS_PAGE_SIZE - 1U)) != 0U ||
        page_count > MEMORY_MAX_FREED_PAGES - freed_page_count)
        return false;
    const uint64_t bytes = page_count * NEXUS_PAGE_SIZE;
    if (physical_address > UINT64_MAX - bytes) return false;
    const uint64_t end = physical_address + bytes;

    bool contained = false;
    for (size_t i = 0U; i < usable_region_count; ++i) {
        const struct memory_region *region = &usable_regions[i];
        if (physical_address >= region->start &&
            end <= region->next && end <= region->end) {
            contained = true;
            break;
        }
    }
    if (!contained) return false;

    for (uint64_t page = 0U; page < page_count; ++page) {
        const uint64_t candidate =
            physical_address + page * NEXUS_PAGE_SIZE;
        for (size_t i = 0U; i < freed_page_count; ++i) {
            if (freed_pages[i] == candidate) return false;
        }
    }
    for (uint64_t page = 0U; page < page_count; ++page)
        freed_pages[freed_page_count++] =
            physical_address + page * NEXUS_PAGE_SIZE;
    if (UINT64_MAX - free_bytes < bytes) free_bytes = UINT64_MAX;
    else free_bytes += bytes;
    return true;
}

bool memory_page_is_allocated(uint64_t physical_address) {
    if (!initialized ||
        (physical_address & (NEXUS_PAGE_SIZE - 1U)) != 0U) return false;

    bool within_allocated_region = false;
    for (size_t i = 0U; i < usable_region_count; ++i) {
        const struct memory_region *region = &usable_regions[i];
        if (physical_address >= region->start &&
            physical_address < region->next &&
            region->next - physical_address >= NEXUS_PAGE_SIZE &&
            physical_address < region->end) {
            within_allocated_region = true;
            break;
        }
    }
    if (!within_allocated_region) return false;

    for (size_t i = 0U; i < freed_page_count; ++i) {
        if (freed_pages[i] == physical_address) return false;
    }
    return true;
}

bool memory_allocator_selftest(void) {
    uint64_t first, second, reused;
    if (!memory_alloc_page(&first) || !memory_alloc_page(&second)) return false;
    if (first == second || !memory_page_is_allocated(first) ||
        !memory_page_is_allocated(second) || !memory_free_page(first))
        return false;
    if (memory_page_is_allocated(first) || memory_free_page(first)) return false;
    if (!memory_alloc_page(&reused) || reused != first ||
        !memory_page_is_allocated(reused)) return false;
    if (!memory_free_page(second) || !memory_free_page(reused)) return false;
    return !memory_page_is_allocated(first) &&
           !memory_page_is_allocated(second);
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
