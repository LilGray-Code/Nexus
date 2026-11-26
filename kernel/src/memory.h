#ifndef NEXUS_MEMORY_H
#define NEXUS_MEMORY_H

#include <stdbool.h>
#include <stdint.h>
#include <limine.h>

#define NEXUS_PAGE_SIZE 4096ULL

bool memory_init(const struct limine_memmap_response *map,
                 const struct limine_hhdm_response *hhdm);
bool memory_alloc_page(uint64_t *physical_address);
bool memory_free_page(uint64_t physical_address);
/* True only for a live page currently owned by the physical-page allocator. */
bool memory_page_is_allocated(uint64_t physical_address);
bool memory_allocator_selftest(void);\n/* Verify normalized regions, free-list ownership, and byte accounting. */\nbool memory_allocator_validate(void);
/* Allocate one page wholly below max_address_exclusive for DMA buffers. */
bool memory_alloc_dma_page(uint64_t max_address_exclusive,
                           uint64_t *physical_address);
/* Allocate/free one physically contiguous run for framebuffer DMA backing. */
bool memory_alloc_contiguous_pages(uint64_t page_count,
                                   uint64_t max_address_exclusive,
                                   uint64_t *physical_address);
bool memory_free_contiguous_pages(uint64_t physical_address,
                                  uint64_t page_count);
void *memory_phys_to_virt(uint64_t physical_address);
uint64_t memory_total_bytes(void);
uint64_t memory_free_bytes(void);

#endif
