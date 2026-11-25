#include "elf64.h"
#include "memory.h"
#include "paging.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define MOCK_PAGE_COUNT 64U
#define MOCK_MAPPING_COUNT 32U
#define MOCK_PHYSICAL_BASE 0x00100000ULL

static uint8_t mock_pages[MOCK_PAGE_COUNT][4096U];
static bool mock_page_used[MOCK_PAGE_COUNT];

struct mock_mapping {
    uint64_t root;
    uint64_t virtual_address;
    uint64_t physical_address;
    bool writable;
    bool active;
};

static struct mock_mapping mock_mappings[MOCK_MAPPING_COUNT];

static int mock_page_index(uint64_t physical_address) {
    if (physical_address < MOCK_PHYSICAL_BASE) return -1;
    const uint64_t offset = physical_address - MOCK_PHYSICAL_BASE;
    if ((offset & 4095U) != 0U) return -1;
    const uint64_t index = offset / 4096U;
    return index < MOCK_PAGE_COUNT ? (int)index : -1;
}

bool memory_alloc_page(uint64_t *physical_address) {
    if (physical_address == NULL) return false;
    for (uint32_t i = 0U; i < MOCK_PAGE_COUNT; ++i) {
        if (!mock_page_used[i]) {
            mock_page_used[i] = true;
            memset(mock_pages[i], 0, sizeof(mock_pages[i]));
            *physical_address = MOCK_PHYSICAL_BASE +
                               (uint64_t)i * 4096U;
            return true;
        }
    }
    return false;
}

bool memory_free_page(uint64_t physical_address) {
    const int index = mock_page_index(physical_address);
    if (index < 0 || !mock_page_used[index]) return false;
    mock_page_used[index] = false;
    memset(mock_pages[index], 0, sizeof(mock_pages[index]));
    return true;
}

bool memory_page_is_allocated(uint64_t physical_address) {
    const int index = mock_page_index(physical_address);
    return index >= 0 && mock_page_used[index];
}

void *memory_phys_to_virt(uint64_t physical_address) {
    const int index = mock_page_index(
        physical_address & ~4095ULL);
    if (index < 0 || !mock_page_used[index]) return NULL;
    return mock_pages[index] + (size_t)(physical_address & 4095ULL);
}

bool paging_create_user_address_space(uint64_t *root_physical) {
    return memory_alloc_page(root_physical);
}

bool paging_destroy_user_address_space(uint64_t root_physical) {
    bool success = true;
    for (uint32_t i = 0U; i < MOCK_MAPPING_COUNT; ++i) {
        if (mock_mappings[i].active &&
            mock_mappings[i].root == root_physical) {
            success = memory_free_page(
                mock_mappings[i].physical_address) && success;
            mock_mappings[i].active = false;
        }
    }
    return memory_free_page(root_physical) && success;
}

bool paging_map_user_page(uint64_t root_physical, uint64_t virtual_address,
                          uint64_t page_physical, bool writable,
                          bool executable) {
    (void)executable;
    if (!memory_page_is_allocated(root_physical) ||
        !memory_page_is_allocated(page_physical) ||
        (virtual_address & 4095U) != 0U)
        return false;
    for (uint32_t i = 0U; i < MOCK_MAPPING_COUNT; ++i) {
        if (mock_mappings[i].active &&
            mock_mappings[i].root == root_physical &&
            mock_mappings[i].virtual_address == virtual_address)
            return false;
    }
    for (uint32_t i = 0U; i < MOCK_MAPPING_COUNT; ++i) {
        if (!mock_mappings[i].active) {
            mock_mappings[i].root = root_physical;
            mock_mappings[i].virtual_address = virtual_address;
            mock_mappings[i].physical_address = page_physical;
            mock_mappings[i].writable = writable;
            mock_mappings[i].active = true;
            return true;
        }
    }
    return false;
}

static bool mock_copy(uint64_t root_physical, uint64_t address,
                      void *buffer, size_t length, bool to_user) {
    if (buffer == NULL && length != 0U) return false;
    uint8_t *bytes = (uint8_t *)buffer;
    uint64_t current = address;
    size_t remaining = length;
    while (remaining != 0U) {
        struct mock_mapping *mapping = NULL;
        for (uint32_t i = 0U; i < MOCK_MAPPING_COUNT; ++i) {
            if (mock_mappings[i].active &&
                mock_mappings[i].root == root_physical &&
                current >= mock_mappings[i].virtual_address &&
                current - mock_mappings[i].virtual_address < 4096U) {
                mapping = &mock_mappings[i];
                break;
            }
        }
        if (mapping == NULL || (to_user && !mapping->writable)) return false;
        const size_t page_offset =
            (size_t)(current - mapping->virtual_address);
        size_t chunk = 4096U - page_offset;
        if (chunk > remaining) chunk = remaining;
        uint8_t *page = (uint8_t *)memory_phys_to_virt(
            mapping->physical_address);
        if (page == NULL) return false;
        if (to_user)
            memcpy(page + page_offset, bytes, chunk);
        else
            memcpy(bytes, page + page_offset, chunk);
        bytes += chunk;
        current += chunk;
        remaining -= chunk;
    }
    return true;
}

bool paging_copy_to_address_space(uint64_t root_physical,
                                  uint64_t destination,
                                  const void *source, size_t length) {
    return mock_copy(root_physical, destination, (void *)source, length, true);
}

bool paging_copy_from_address_space(uint64_t root_physical,
                                    void *destination,
                                    uint64_t source, size_t length) {
    return mock_copy(root_physical, source, destination, length, false);
}

int main(void) {
    assert(nexus_elf64_selftest());
    puts("NEXUS ELF64 ET_EXEC/STATIC-PIE PARSER AND RELOCATION TESTS PASSED");
    return 0;
}
