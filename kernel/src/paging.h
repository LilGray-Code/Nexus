#ifndef NEXUS_PAGING_H
#define NEXUS_PAGING_H

#include <stdbool.h>
#include <stdint.h>

bool paging_map_mmio_page(uint64_t physical_address,
                          volatile uint32_t **virtual_address);

#endif
