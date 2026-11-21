#ifndef NEXUS_PAGING_H
#define NEXUS_PAGING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool paging_map_mmio_page(uint64_t physical_address,
                          volatile uint32_t **virtual_address);
bool paging_enable_nx(void);
bool paging_nx_enabled(void);
bool paging_create_user_address_space(uint64_t *root_physical);
bool paging_destroy_user_address_space(uint64_t root_physical);
bool paging_map_user_page(uint64_t root_physical, uint64_t virtual_address,
                          uint64_t page_physical, bool writable, bool executable);
bool paging_copy_from_user(void *destination, uint64_t source, size_t length);

#endif
