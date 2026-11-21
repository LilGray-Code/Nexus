#ifndef NEXUS_PAGING_H
#define NEXUS_PAGING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool paging_map_mmio_page(uint64_t physical_address,
                          volatile uint32_t **virtual_address);
bool paging_copy_from_user(void *destination, uint64_t source, size_t length);

#endif
