#ifndef NEXUS_MEMORY_H
#define NEXUS_MEMORY_H

#include <stdbool.h>
#include <stdint.h>
#include <limine.h>

#define NEXUS_PAGE_SIZE 4096ULL

bool memory_init(const struct limine_memmap_response *map,
                 const struct limine_hhdm_response *hhdm);
bool memory_alloc_page(uint64_t *physical_address);
void *memory_phys_to_virt(uint64_t physical_address);
uint64_t memory_total_bytes(void);
uint64_t memory_free_bytes(void);

#endif
