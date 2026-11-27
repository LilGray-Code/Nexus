#include "memory.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define TEST_THREAD_COUNT 8U
#define TEST_PAGES_PER_THREAD 128U
#define TEST_PAGE_COUNT (TEST_THREAD_COUNT * TEST_PAGES_PER_THREAD)
#define TEST_MEMORY_BASE 0x100000ULL
#define TEST_MEMORY_SIZE (64ULL * 1024ULL * 1024ULL)
#define TEST_MAX_TRACKED_PAGES (TEST_MEMORY_SIZE / NEXUS_PAGE_SIZE)

static struct limine_memmap_entry memory_region = {
    .base = TEST_MEMORY_BASE,
    .length = TEST_MEMORY_SIZE,
    .type = LIMINE_MEMMAP_USABLE
};
static struct limine_memmap_entry *memory_entries[] = { &memory_region };
static struct limine_memmap_response memory_map = {
    .revision = 0U,
    .entry_count = 1U,
    .entries = memory_entries
};
static struct limine_hhdm_response hhdm = {
    .revision = 0U,
    .offset = 0U
};

static uint64_t allocated_pages[TEST_PAGE_COUNT];
static uint64_t exhaustion_pages[TEST_MAX_TRACKED_PAGES];

struct worker_context {
    size_t first_page;
    bool release_pages;
    bool failed;
};

static void *allocator_worker(void *argument) {
    struct worker_context *context = argument;
    for (size_t i = 0U; i < TEST_PAGES_PER_THREAD; ++i) {
        const size_t slot = context->first_page + i;
        if (context->release_pages) {
            if (!memory_free_page(allocated_pages[slot]))
                context->failed = true;
        } else if (!memory_alloc_page(&allocated_pages[slot])) {
            context->failed = true;
        }
    }
    return NULL;
}

static bool run_parallel_round(bool release_pages) {
    pthread_t threads[TEST_THREAD_COUNT];
    struct worker_context contexts[TEST_THREAD_COUNT];
    size_t created = 0U;

    for (size_t i = 0U; i < TEST_THREAD_COUNT; ++i) {
        contexts[i] = (struct worker_context){
            .first_page = i * TEST_PAGES_PER_THREAD,
            .release_pages = release_pages,
            .failed = false
        };
        if (pthread_create(&threads[i], NULL, allocator_worker,
                           &contexts[i]) != 0) {
            for (size_t joined = 0U; joined < created; ++joined)
                (void)pthread_join(threads[joined], NULL);
            return false;
        }
        ++created;
    }

    bool success = true;
    for (size_t i = 0U; i < created; ++i) {
        if (pthread_join(threads[i], NULL) != 0 || contexts[i].failed)
            success = false;
    }
    return success;
}

static bool check_unique_live_pages(void) {
    for (size_t i = 0U; i < TEST_PAGE_COUNT; ++i) {
        if (allocated_pages[i] == 0U ||
            !memory_page_is_allocated(allocated_pages[i])) return false;
        for (size_t j = i + 1U; j < TEST_PAGE_COUNT; ++j) {
            if (allocated_pages[i] == allocated_pages[j]) return false;
        }
    }
    return true;
}

static bool check_released_pages(void) {
    for (size_t i = 0U; i < TEST_PAGE_COUNT; ++i) {
        if (memory_page_is_allocated(allocated_pages[i])) return false;
    }
    return true;
}

/*
 * Force the allocator into its real out-of-memory path, then prove that
 * freeing every held page restores the exact pre-exhaustion free-byte count.
 */
static bool test_exhaustion_and_recovery(void) {
    struct memory_allocator_stats before;
    if (!memory_get_allocator_stats(&before)) return false;

    size_t allocated = 0U;
    while (allocated < TEST_MAX_TRACKED_PAGES) {
        uint64_t physical = 0U;
        if (!memory_alloc_page(&physical)) break;
        if (physical == 0U) return false;
        exhaustion_pages[allocated++] = physical;
    }
    if (allocated == 0U || allocated == TEST_MAX_TRACKED_PAGES) return false;

    struct memory_allocator_stats exhausted;
    if (!memory_get_allocator_stats(&exhausted) ||
        exhausted.allocation_failures <= before.allocation_failures ||
        exhausted.free_bytes != 0U) return false;

    for (size_t i = 0U; i < allocated; ++i) {
        if (!memory_page_is_allocated(exhaustion_pages[i]) ||
            !memory_free_page(exhaustion_pages[i])) return false;
    }

    struct memory_allocator_stats recovered;
    if (!memory_get_allocator_stats(&recovered) ||
        !memory_allocator_validate()) return false;
    return recovered.free_bytes == before.free_bytes &&
           recovered.used_bytes == before.used_bytes &&
           recovered.allocation_failures > before.allocation_failures &&
           recovered.free_failures == before.free_failures;
}

int main(void) {
    if (!memory_init(&memory_map, &hhdm)) {
        fputs("memory allocator initialization failed\n", stderr);
        return EXIT_FAILURE;
    }

    for (size_t round = 0U; round < 2U; ++round) {
        if (!run_parallel_round(false) || !check_unique_live_pages()) {
            fputs("parallel allocation produced a failure or duplicate page\n",
                  stderr);
            return EXIT_FAILURE;
        }
        if (!run_parallel_round(true) || !check_released_pages() ||
            !memory_allocator_validate()) {
            fputs("parallel free corrupted allocator state\n", stderr);
            return EXIT_FAILURE;
        }
    }

    struct memory_allocator_stats stats;
    if (!memory_get_allocator_stats(&stats) ||
        stats.allocation_failures != 0U ||
        stats.free_failures != 0U ||
        stats.total_bytes != TEST_MEMORY_SIZE ||
        stats.free_bytes != TEST_MEMORY_SIZE) {
        fputs("allocator accounting did not recover after concurrent stress\n",
              stderr);
        return EXIT_FAILURE;
    }

    if (!test_exhaustion_and_recovery()) {
        fputs("allocator failed to recover exact free-page accounting after exhaustion\\n",
              stderr);
        return EXIT_FAILURE;
    }

    puts("PASS: SMP allocator stress plus out-of-memory exhaustion and recovery");
    return EXIT_SUCCESS;
}
