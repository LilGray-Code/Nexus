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
static uint64_t allocation_requests;
static uint64_t allocation_failures;
static uint64_t pages_allocated_total;
static uint64_t free_requests;
static uint64_t free_failures;
static uint64_t pages_freed_total;

static void saturating_add_u64(uint64_t *value, uint64_t amount) {
    if (UINT64_MAX - *value < amount) *value = UINT64_MAX;
    else *value += amount;
}

static void note_allocation_request(void) {
    saturating_add_u64(&allocation_requests, 1U);
}

static void note_allocation_failure(void) {
    saturating_add_u64(&allocation_failures, 1U);
}

static void note_allocated_pages(uint64_t pages) {
    saturating_add_u64(&pages_allocated_total, pages);
}

static void note_free_request(void) {
    saturating_add_u64(&free_requests, 1U);
}

static void note_free_failure(void) {
    saturating_add_u64(&free_failures, 1U);
}

static void note_freed_pages(uint64_t pages) {
    saturating_add_u64(&pages_freed_total, pages);
}

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
    allocation_requests = 0U;
    allocation_failures = 0U;
    pages_allocated_total = 0U;
    free_requests = 0U;
    free_failures = 0U;
    pages_freed_total = 0U;
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
        /*
         * Firmware maps are normally sorted and non-overlapping, but do not
         * rely on that property here. Insert each usable range in address
         * order and coalesce overlaps/adjacent ranges so a bad ordering can
         * never make the allocator hand out the same physical page twice.
         */
        size_t insert_at = 0U;
        while (insert_at < usable_region_count &&
               usable_regions[insert_at].end < start) {
            ++insert_at;
        }

        uint64_t merged_start = start;
        uint64_t merged_end = end;
        size_t merge_end = insert_at;
        while (merge_end < usable_region_count &&
               usable_regions[merge_end].start <= merged_end) {
            if (usable_regions[merge_end].start < merged_start)
                merged_start = usable_regions[merge_end].start;
            if (usable_regions[merge_end].end > merged_end)
                merged_end = usable_regions[merge_end].end;
            ++merge_end;
        }

        if (merge_end > insert_at) {
            const size_t removed = merge_end - insert_at;
            for (size_t move = merge_end; move < usable_region_count; ++move)
                usable_regions[move - removed] = usable_regions[move];
            usable_region_count -= removed;
        }

        if (usable_region_count >= MEMORY_MAX_REGIONS) {
            usable_region_count = 0U;
            total_bytes = 0U;
            free_bytes = 0U;
            return false;
        }

        for (size_t move = usable_region_count; move > insert_at; --move)
            usable_regions[move] = usable_regions[move - 1U];
        usable_regions[insert_at].start = merged_start;
        usable_regions[insert_at].next = merged_start;
        usable_regions[insert_at].end = merged_end;
        ++usable_region_count;
    }

    /* Compute accounting only after all ranges have been normalized. */
    for (size_t i = 0U; i < usable_region_count; ++i) {
        const uint64_t bytes = usable_regions[i].end - usable_regions[i].start;
        if (UINT64_MAX - total_bytes < bytes) total_bytes = UINT64_MAX;
        else total_bytes += bytes;
        if (UINT64_MAX - free_bytes < bytes) free_bytes = UINT64_MAX;
        else free_bytes += bytes;
    }

    initialized = usable_region_count != 0U;
    return initialized && memory_allocator_validate();
}

bool memory_alloc_page(uint64_t *physical_address) {
    note_allocation_request();
    if (!initialized || physical_address == NULL) {
        note_allocation_failure();
        return false;
    }

    if (freed_page_count != 0U) {
        *physical_address = freed_pages[--freed_page_count];
        if (free_bytes >= NEXUS_PAGE_SIZE) free_bytes -= NEXUS_PAGE_SIZE;
        else free_bytes = 0U;
        note_allocated_pages(1U);
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
                free_bytes = 0U;
            }
            note_allocated_pages(1U);
            return true;
        }
        ++current_region;
    }

    note_allocation_failure();
    return false;
}

/*
 * Allocate one page below a device's DMA address limit. This is deliberately
 * page-sized only: callers that need multiple contiguous pages must request
 * and validate each run through a future contiguous allocator.
 */
bool memory_alloc_dma_page(uint64_t max_address_exclusive,
                           uint64_t *physical_address) {
    note_allocation_request();
    if (!initialized || physical_address == NULL ||
        max_address_exclusive < NEXUS_PAGE_SIZE) {
        note_allocation_failure();
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
            else free_bytes = 0U;
            note_allocated_pages(1U);
            return true;
        }
    }

    for (size_t i = 0U; i < usable_region_count; ++i) {
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
        if (free_bytes >= NEXUS_PAGE_SIZE) free_bytes -= NEXUS_PAGE_SIZE;
        else free_bytes = 0U;
        note_allocated_pages(1U);
        return true;
    }

    note_allocation_failure();
    return false;
}

/*
 * Return a page previously handed out by this allocator. The early allocator
 * uses a bounded LIFO free list; it rejects unaligned, foreign, and duplicate
 * frees. SMP synchronization and a larger bitmap allocator are future work.
 */
bool memory_free_page(uint64_t physical_address) {
    note_free_request();
    if (!initialized ||
        (physical_address & (NEXUS_PAGE_SIZE - 1U)) != 0U ||
        freed_page_count >= MEMORY_MAX_FREED_PAGES) {
        note_free_failure();
        return false;
    }

    for (size_t i = 0U; i < freed_page_count; ++i) {
        if (freed_pages[i] == physical_address) {
            note_free_failure();
            return false;
        }
    }

    bool was_allocated = false;
    for (size_t i = 0U; i < usable_region_count; ++i) {
        const struct memory_region *region = &usable_regions[i];
        if (physical_address >= region->start &&
            physical_address < region->next &&
            region->next - physical_address >= NEXUS_PAGE_SIZE &&
            physical_address < region->end) {
            was_allocated = true;
            break;
        }
    }
    if (!was_allocated) {
        note_free_failure();
        return false;
    }

    freed_pages[freed_page_count++] = physical_address;
    if (UINT64_MAX - free_bytes < NEXUS_PAGE_SIZE) free_bytes = UINT64_MAX;
    else free_bytes += NEXUS_PAGE_SIZE;
    note_freed_pages(1U);
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
    note_allocation_request();
    if (!initialized || physical_address == NULL || page_count == 0U ||
        page_count > UINT64_MAX / NEXUS_PAGE_SIZE) {
        note_allocation_failure();
        return false;
    }

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
        note_allocated_pages(page_count);
        return true;
    }
    note_allocation_failure();
    return false;
}

/*
 * Return a contiguous run to the bounded early free list. Validate the whole
 * run before mutating allocator state, so failure never leaves a partial free.
 */
bool memory_free_contiguous_pages(uint64_t physical_address,
                                  uint64_t page_count) {
    note_free_request();
    if (!initialized || page_count == 0U ||
        page_count > UINT64_MAX / NEXUS_PAGE_SIZE ||
        (physical_address & (NEXUS_PAGE_SIZE - 1U)) != 0U ||
        page_count > MEMORY_MAX_FREED_PAGES - freed_page_count) {
        note_free_failure();
        return false;
    }
    const uint64_t bytes = page_count * NEXUS_PAGE_SIZE;
    if (physical_address > UINT64_MAX - bytes) {
        note_free_failure();
        return false;
    }
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
    if (!contained) {
        note_free_failure();
        return false;
    }

    for (uint64_t page = 0U; page < page_count; ++page) {
        const uint64_t candidate =
            physical_address + page * NEXUS_PAGE_SIZE;
        for (size_t i = 0U; i < freed_page_count; ++i) {
            if (freed_pages[i] == candidate) {
                note_free_failure();
                return false;
            }
        }
    }
    for (uint64_t page = 0U; page < page_count; ++page)
        freed_pages[freed_page_count++] =
            physical_address + page * NEXUS_PAGE_SIZE;
    if (UINT64_MAX - free_bytes < bytes) free_bytes = UINT64_MAX;
    else free_bytes += bytes;
    note_freed_pages(page_count);
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

bool memory_allocator_validate(void) {
    if (!initialized || usable_region_count == 0U ||
        usable_region_count > MEMORY_MAX_REGIONS ||
        freed_page_count > MEMORY_MAX_FREED_PAGES) return false;

    uint64_t calculated_total = 0U;
    uint64_t calculated_free = 0U;
    for (size_t i = 0U; i < usable_region_count; ++i) {
        const struct memory_region *region = &usable_regions[i];
        if ((region->start & (NEXUS_PAGE_SIZE - 1U)) != 0U ||
            (region->next & (NEXUS_PAGE_SIZE - 1U)) != 0U ||
            (region->end & (NEXUS_PAGE_SIZE - 1U)) != 0U ||
            region->start >= region->end ||
            region->next < region->start || region->next > region->end)
            return false;
        if (i != 0U && usable_regions[i - 1U].end >= region->start)
            return false;

        const uint64_t span = region->end - region->start;
        const uint64_t remaining = region->end - region->next;
        if (UINT64_MAX - calculated_total < span)
            calculated_total = UINT64_MAX;
        else
            calculated_total += span;
        if (UINT64_MAX - calculated_free < remaining)
            calculated_free = UINT64_MAX;
        else
            calculated_free += remaining;
    }

    for (size_t i = 0U; i < freed_page_count; ++i) {
        const uint64_t page = freed_pages[i];
        bool belongs_to_allocated_range = false;
        if ((page & (NEXUS_PAGE_SIZE - 1U)) != 0U) return false;
        for (size_t region_index = 0U;
             region_index < usable_region_count; ++region_index) {
            const struct memory_region *region =
                &usable_regions[region_index];
            if (page >= region->start && page < region->next &&
                region->next - page >= NEXUS_PAGE_SIZE &&
                page < region->end) {
                belongs_to_allocated_range = true;
                break;
            }
        }
        if (!belongs_to_allocated_range) return false;
        for (size_t previous = 0U; previous < i; ++previous)
            if (freed_pages[previous] == page) return false;
    }

    const uint64_t reclaimed_bytes =
        (uint64_t)freed_page_count * NEXUS_PAGE_SIZE;
    if (UINT64_MAX - calculated_free < reclaimed_bytes)
        calculated_free = UINT64_MAX;
    else
        calculated_free += reclaimed_bytes;

    return calculated_total == total_bytes &&
           calculated_free == free_bytes;
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
    if (memory_page_is_allocated(first) ||
        memory_page_is_allocated(second) ||
        !memory_allocator_validate()) return false;

    /* Exercise the contiguous DMA-backing path and its all-or-nothing free. */
    uint64_t contiguous = 0U;
    if (!memory_alloc_contiguous_pages(1U, 0U, &contiguous) ||
        !memory_page_is_allocated(contiguous) ||
        !memory_free_contiguous_pages(contiguous, 1U) ||
        memory_page_is_allocated(contiguous)) return false;
    return memory_allocator_validate();
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

bool memory_get_allocator_stats(struct memory_allocator_stats *stats) {
    if (!initialized || stats == NULL) return false;

    uint64_t largest_extent = 0U;
    for (size_t i = 0U; i < usable_region_count; ++i) {
        const struct memory_region *region = &usable_regions[i];
        const uint64_t extent = region->end - region->next;
        if (extent > largest_extent) largest_extent = extent;
    }

    stats->total_bytes = total_bytes;
    stats->free_bytes = free_bytes;
    stats->used_bytes = total_bytes >= free_bytes
        ? total_bytes - free_bytes : 0U;
    stats->largest_unallocated_extent_bytes = largest_extent;
    stats->allocation_requests = allocation_requests;
    stats->allocation_failures = allocation_failures;
    stats->pages_allocated_total = pages_allocated_total;
    stats->free_requests = free_requests;
    stats->free_failures = free_failures;
    stats->pages_freed_total = pages_freed_total;
    stats->free_list_pages = (uint64_t)freed_page_count;
    stats->region_count = (uint64_t)usable_region_count;
    return true;
}

