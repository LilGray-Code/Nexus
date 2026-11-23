#include "paging.h"

#include "memory.h"

#include <stddef.h>
#include <stdint.h>

#define PAGE_PRESENT 0x001ULL
#define PAGE_WRITABLE 0x002ULL
#define PAGE_USER 0x004ULL
#define PAGE_WRITE_THROUGH 0x008ULL
#define PAGE_CACHE_DISABLE 0x010ULL
#define PAGE_LARGE 0x080ULL
#define PAGE_NO_EXECUTE (1ULL << 63)
#define PAGE_ADDRESS_MASK 0x000FFFFFFFFFF000ULL
#define CR4_LA57 (1ULL << 12)
#define IA32_EFER_MSR 0xC0000080U
#define IA32_EFER_NXE (1ULL << 11)
#define CPUID_EXTENDED_MAX 0x80000000U
#define CPUID_EXTENDED_FEATURES 0x80000001U
#define CPUID_NX_BIT (1U << 20)

static bool nx_enabled;

static bool allocate_table(uint64_t *physical,
                           volatile uint64_t **virtual_table) {
    if (physical == NULL || virtual_table == NULL) return false;
    *virtual_table = NULL;
    if (!memory_alloc_page(physical)) return false;

    volatile uint64_t *table =
        (volatile uint64_t *)memory_phys_to_virt(*physical);
    if (table == NULL) {
        (void)memory_free_page(*physical);
        *physical = 0U;
        return false;
    }

    for (unsigned int i = 0; i < 512; ++i) table[i] = 0;
    *virtual_table = table;
    return true;
}

/*
 * Enable execute-disable support only when the processor advertises NX.
 * Long mode guarantees RDMSR/WRMSR support; NXE is set before any user leaf
 * mapping is marked non-executable.
 */
bool paging_enable_nx(void) {
    uint32_t eax = CPUID_EXTENDED_MAX;
    uint32_t ebx, ecx = 0U, edx;
    __asm__ volatile ("cpuid"
                      : "+a"(eax), "=b"(ebx), "+c"(ecx), "=d"(edx));
    if (eax < CPUID_EXTENDED_FEATURES) {
        nx_enabled = false;
        return false;
    }

    eax = CPUID_EXTENDED_FEATURES;
    ecx = 0U;
    __asm__ volatile ("cpuid"
                      : "+a"(eax), "=b"(ebx), "+c"(ecx), "=d"(edx));
    if ((edx & CPUID_NX_BIT) == 0U) {
        nx_enabled = false;
        return false;
    }

    uint32_t efer_low, efer_high;
    __asm__ volatile ("rdmsr"
                      : "=a"(efer_low), "=d"(efer_high)
                      : "c"(IA32_EFER_MSR));
    uint64_t efer = ((uint64_t)efer_high << 32) | efer_low;
    efer |= IA32_EFER_NXE;
    __asm__ volatile ("wrmsr"
                      :
                      : "c"(IA32_EFER_MSR),
                        "a"((uint32_t)efer),
                        "d"((uint32_t)(efer >> 32))
                      : "memory");
    nx_enabled = true;
    return true;
}

bool paging_nx_enabled(void) {
    return nx_enabled;
}

/*
 * Add a dedicated high-half alias for one MMIO page. The direct-map offset
 * is not assumed to cover device memory. We create a fresh top-level slot,
 * allocate intermediate page tables from the bootstrap allocator, map the
 * leaf as PWT+PCD (uncacheable), and invalidate the new virtual address.
 *
 * Both four-level and five-level paging are handled according to CR4.LA57.
 */
bool paging_map_mmio_page(uint64_t physical_address,
                          volatile uint32_t **virtual_address) {
    if (virtual_address == NULL) return false;
    *virtual_address = NULL;
    if ((physical_address & ~(PAGE_ADDRESS_MASK | 0xFFFULL)) != 0U)
        return false;

    const uint64_t physical_page = physical_address & PAGE_ADDRESS_MASK;
    uint64_t cr3, cr4;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile ("mov %%cr4, %0" : "=r"(cr4));

    const unsigned int levels = (cr4 & CR4_LA57) ? 5U : 4U;
    const uint64_t root_physical = cr3 & PAGE_ADDRESS_MASK;
    volatile uint64_t *root =
        (volatile uint64_t *)memory_phys_to_virt(root_physical);
    if (root == NULL) return false;

    /* Keep the new mapping outside the usual HHDM and kernel PML4/PML5 slots. */
    unsigned int root_index = 256;
    while (root_index < 511 && (root[root_index] & PAGE_PRESENT) != 0) {
        ++root_index;
    }
    if (root_index >= 511) return false;

    const uint64_t virtual_base = levels == 5U
        ? (0xFF00000000000000ULL | ((uint64_t)root_index << 48))
        : (0xFFFF000000000000ULL | ((uint64_t)root_index << 39));

    volatile uint64_t *parent = root;
    volatile uint64_t *created_entries[4];
    uint64_t created_pages[4];
    size_t created_count = 0U;
    for (unsigned int level = levels; level > 1U; --level) {
        uint64_t next_physical;
        volatile uint64_t *next_table;
        const unsigned int parent_index =
            (level == levels) ? root_index : 0U;
        if (!allocate_table(&next_physical, &next_table)) {
            while (created_count != 0U) {
                --created_count;
                *created_entries[created_count] = 0U;
                (void)memory_free_page(created_pages[created_count]);
            }
            return false;
        }

        created_entries[created_count] = &parent[parent_index];
        created_pages[created_count] = next_physical;
        ++created_count;
        parent[parent_index] = (next_physical & PAGE_ADDRESS_MASK) |
                               PAGE_PRESENT | PAGE_WRITABLE;
        parent = next_table;
    }

    parent[0] = physical_page | PAGE_PRESENT | PAGE_WRITABLE |
                PAGE_WRITE_THROUGH | PAGE_CACHE_DISABLE;

    __asm__ volatile ("invlpg (%0)" : : "r"((uintptr_t)virtual_base) : "memory");
    *virtual_address = (volatile uint32_t *)(uintptr_t)virtual_base;
    return true;
}

/*
 * Create a process root with shared supervisor kernel mappings in the high
 * half and a private, initially empty lower canonical half for user mappings.
 */

static bool destroy_user_page_table(uint64_t table_physical,
                                     unsigned int level) {
    if (level == 0U) return false;
    volatile uint64_t *table =
        (volatile uint64_t *)memory_phys_to_virt(table_physical);
    if (table == NULL) return false;

    for (size_t i = 0; i < 512U; ++i) {
        const uint64_t entry = table[i];
        if ((entry & PAGE_PRESENT) == 0U) continue;
        if ((entry & PAGE_USER) == 0U) return false;

        if (level == 1U) {
            if (!memory_free_page(entry & PAGE_ADDRESS_MASK)) return false;
        } else {
            if ((entry & PAGE_LARGE) != 0U) return false;
            const uint64_t child = entry & PAGE_ADDRESS_MASK;
            if (!destroy_user_page_table(child, level - 1U) ||
                !memory_free_page(child)) return false;
        }
        table[i] = 0;
    }
    return true;
}

/*
 * Validate a complete user-owned subtree before teardown mutates it. This
 * two-pass design prevents a malformed later entry from leaving earlier
 * mappings already freed while the root still appears live.
 */
static bool validate_user_page_table(uint64_t table_physical,
                                     unsigned int level) {
    if (level == 0U) return false;
    if (!memory_page_is_allocated(table_physical)) return false;
    volatile uint64_t *table =
        (volatile uint64_t *)memory_phys_to_virt(table_physical);
    if (table == NULL) return false;

    for (size_t i = 0U; i < 512U; ++i) {
        const uint64_t entry = table[i];
        if ((entry & PAGE_PRESENT) == 0U) continue;
        if ((entry & PAGE_USER) == 0U) return false;
        const uint64_t child_or_leaf = entry & PAGE_ADDRESS_MASK;
        if (!memory_page_is_allocated(child_or_leaf)) return false;
        if (level > 1U) {
            if ((entry & PAGE_LARGE) != 0U ||
                !validate_user_page_table(child_or_leaf, level - 1U))
                return false;
        }
    }
    return true;
}

bool paging_destroy_user_address_space(uint64_t root_physical) {
    if (!memory_page_is_allocated(root_physical)) return false;

    uint64_t cr3, cr4;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile ("mov %%cr4, %0" : "=r"(cr4));
    if ((cr3 & PAGE_ADDRESS_MASK) == root_physical) return false;

    const unsigned int levels = (cr4 & CR4_LA57) != 0U ? 5U : 4U;
    volatile uint64_t *root =
        (volatile uint64_t *)memory_phys_to_virt(root_physical);
    if (root == NULL) return false;

    /*
     * User space owns the entire lower canonical half (top-level entries
     * 0..255). Walk every slot rather than assuming only slot zero is used:
     * this also makes teardown safe if the process ABI later grows beyond
     * the current 4 MiB ELF image window.
     */
    for (size_t index = 0U; index < 256U; ++index) {
        const uint64_t entry = root[index];
        if ((entry & PAGE_PRESENT) == 0U) continue;
        if ((entry & PAGE_USER) == 0U || (entry & PAGE_LARGE) != 0U ||
            !validate_user_page_table(entry & PAGE_ADDRESS_MASK,
                                      levels - 1U)) return false;
    }

    /* Only after every subtree is valid do we begin reclaiming its pages. */
    for (size_t index = 0U; index < 256U; ++index) {
        const uint64_t entry = root[index];
        if ((entry & PAGE_PRESENT) == 0U) continue;
        const uint64_t user_table = entry & PAGE_ADDRESS_MASK;
        if (!destroy_user_page_table(user_table, levels - 1U) ||
            !memory_free_page(user_table)) return false;
        root[index] = 0U;
    }
    return memory_free_page(root_physical);
}

bool paging_create_user_address_space(uint64_t *root_physical) {
    if (root_physical == NULL) return false;

    uint64_t cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    const uint64_t kernel_root_physical = cr3 & PAGE_ADDRESS_MASK;
    volatile uint64_t *kernel_root =
        (volatile uint64_t *)memory_phys_to_virt(kernel_root_physical);
    if (kernel_root == NULL) return false;

    volatile uint64_t *user_root;
    if (!allocate_table(root_physical, &user_root)) return false;

    /*
     * Share only the supervisor kernel half. The allocator zeroes the new
     * root, so leaving entries 0..255 untouched guarantees there are no
     * inherited identity maps, bootloader aliases, or supervisor-only lower
     * mappings in a process address space.
     */
    for (size_t i = 256U; i < 512U; ++i)
        user_root[i] = kernel_root[i] & ~PAGE_USER;
    return true;
}

/*
 * Exercise the same address-space lifecycle used by ELF processes during
 * boot. It verifies a clean lower half, shared kernel mappings, user-page
 * insertion, duplicate rejection, and complete reclamation on teardown.
 */
bool paging_user_address_space_selftest(void) {
    uint64_t cr3, cr4;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile ("mov %%cr4, %0" : "=r"(cr4));
    const unsigned int levels = (cr4 & CR4_LA57) != 0U ? 5U : 4U;
    const unsigned int user_bits = levels == 5U ? 56U : 47U;
    const uint64_t kernel_root_physical = cr3 & PAGE_ADDRESS_MASK;
    volatile uint64_t *kernel_root =
        (volatile uint64_t *)memory_phys_to_virt(kernel_root_physical);
    if (kernel_root == NULL) return false;

    uint64_t test_root_physical = 0U;
    if (!paging_create_user_address_space(&test_root_physical)) return false;
    volatile uint64_t *test_root =
        (volatile uint64_t *)memory_phys_to_virt(test_root_physical);
    if (test_root == NULL) {
        (void)paging_destroy_user_address_space(test_root_physical);
        return false;
    }

    bool valid = true;
    for (size_t i = 0U; i < 256U; ++i) {
        if (test_root[i] != 0U) valid = false;
    }
    for (size_t i = 256U; i < 512U; ++i) {
        if (test_root[i] != (kernel_root[i] & ~PAGE_USER) ||
            (test_root[i] & PAGE_USER) != 0U) valid = false;
    }

    uint64_t user_page = 0U;
    if (!memory_alloc_page(&user_page)) {
        (void)paging_destroy_user_address_space(test_root_physical);
        return false;
    }
    if (!paging_map_user_page(test_root_physical, 0x00400000ULL,
                              user_page, true, false)) {
        (void)memory_free_page(user_page);
        (void)paging_destroy_user_address_space(test_root_physical);
        return false;
    }
    if ((test_root[0] & (PAGE_PRESENT | PAGE_USER)) !=
        (PAGE_PRESENT | PAGE_USER)) valid = false;

    const uint64_t second_user_address =
        levels == 5U ? (1ULL << 48) : (1ULL << 39);
    uint64_t second_user_page = 0U;
    if (!memory_alloc_page(&second_user_page)) {
        (void)paging_destroy_user_address_space(test_root_physical);
        return false;
    }
    if (!paging_map_user_page(test_root_physical, second_user_address,
                              second_user_page, true, false)) {
        (void)memory_free_page(second_user_page);
        (void)paging_destroy_user_address_space(test_root_physical);
        return false;
    }
    if ((test_root[1] & (PAGE_PRESENT | PAGE_USER)) !=
        (PAGE_PRESENT | PAGE_USER)) valid = false;

    uint64_t duplicate_page = 0U;
    if (!memory_alloc_page(&duplicate_page)) {
        (void)paging_destroy_user_address_space(test_root_physical);
        return false;
    }
    if (paging_map_user_page(test_root_physical, 0x00400000ULL,
                             duplicate_page, true, false)) valid = false;
    if (!memory_free_page(duplicate_page)) valid = false;

    if (paging_map_user_page(test_root_physical, 1ULL << user_bits,
                             duplicate_page, true, false)) valid = false;
    if (paging_map_user_page(kernel_root_physical, 0x00800000ULL,
                             duplicate_page, true, false)) valid = false;

    /*
     * Put a deliberately malformed supervisor-only entry at the last user
     * root slot. Teardown must reject the entire tree before freeing any of
     * the valid lower-index mappings.
     */
    const uint64_t free_before_rejected_destroy = memory_free_bytes();
    test_root[255] = PAGE_PRESENT;
    const bool rejected_partial_destroy =
        !paging_destroy_user_address_space(test_root_physical) &&
        memory_free_bytes() == free_before_rejected_destroy;
    test_root[255] = 0U;
    if (!rejected_partial_destroy ||
        (test_root[0] & PAGE_PRESENT) == 0U ||
        (test_root[1] & PAGE_PRESENT) == 0U) valid = false;

    if (!paging_destroy_user_address_space(test_root_physical)) return false;
    return valid;
}

/* Map one previously-unmapped 4 KiB page into a user address space. */
bool paging_map_user_page(uint64_t root_physical, uint64_t virtual_address,
                          uint64_t page_physical, bool writable, bool executable) {
    if (!memory_page_is_allocated(root_physical) ||
        !memory_page_is_allocated(page_physical) ||
        (virtual_address & 0xFFFULL) != 0U ||
        (page_physical & ~PAGE_ADDRESS_MASK) != 0U) {
        return false;
    }

    uint64_t cr4;
    __asm__ volatile ("mov %%cr4, %0" : "=r"(cr4));
    const unsigned int levels = (cr4 & CR4_LA57) != 0U ? 5U : 4U;
    const unsigned int user_bits = levels == 5U ? 56U : 47U;
    if (virtual_address >= (1ULL << user_bits)) return false;

    uint64_t current_cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(current_cr3));
    if ((current_cr3 & PAGE_ADDRESS_MASK) == root_physical) return false;

    volatile uint64_t *table =
        (volatile uint64_t *)memory_phys_to_virt(root_physical);
    if (table == NULL) return false;

    volatile uint64_t *created_entries[4];
    uint64_t created_pages[4];
    size_t created_count = 0U;
    for (unsigned int level = levels; level > 1U; --level) {
        const unsigned int shift = 12U + 9U * (level - 1U);
        const unsigned int index =
            (unsigned int)((virtual_address >> shift) & 0x1FFU);
        const uint64_t entry = table[index];

        if ((entry & PAGE_PRESENT) == 0U) {
            uint64_t next_physical;
            volatile uint64_t *next_table;
            if (!allocate_table(&next_physical, &next_table)) {
                while (created_count != 0U) {
                    --created_count;
                    *created_entries[created_count] = 0U;
                    (void)memory_free_page(created_pages[created_count]);
                }
                return false;
            }
            created_entries[created_count] = &table[index];
            created_pages[created_count] = next_physical;
            ++created_count;
            table[index] = (next_physical & PAGE_ADDRESS_MASK) |
                           PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
            table = next_table;
        } else {
            if ((entry & PAGE_USER) == 0U || (entry & PAGE_LARGE) != 0U) {
                while (created_count != 0U) {
                    --created_count;
                    *created_entries[created_count] = 0U;
                    (void)memory_free_page(created_pages[created_count]);
                }
                return false;
            }
            table = (volatile uint64_t *)memory_phys_to_virt(
                entry & PAGE_ADDRESS_MASK);
            if (table == NULL) {
                while (created_count != 0U) {
                    --created_count;
                    *created_entries[created_count] = 0U;
                    (void)memory_free_page(created_pages[created_count]);
                }
                return false;
            }
        }
    }

    const unsigned int leaf_index =
        (unsigned int)((virtual_address >> 12) & 0x1FFU);
    if ((table[leaf_index] & PAGE_PRESENT) != 0U) {
        while (created_count != 0U) {
            --created_count;
            *created_entries[created_count] = 0U;
            (void)memory_free_page(created_pages[created_count]);
        }
        return false;
    }

    table[leaf_index] = (page_physical & PAGE_ADDRESS_MASK) |
                        PAGE_PRESENT | PAGE_USER |
                        (writable ? PAGE_WRITABLE : 0U) |
                        ((!executable && nx_enabled) ? PAGE_NO_EXECUTE : 0U);
    return true;
}

/*
 * Translate one user virtual address using the active CR3. Every paging level
 * must be present and user-accessible. The helper also understands 1 GiB and
 * 2 MiB leaves, so the copy routine does not assume every mapping is 4 KiB.
 */
static bool translate_user_address(uint64_t root_physical,
                                   unsigned int levels,
                                   uint64_t address,
                                   uint64_t *physical_address,
                                   uint64_t *mapping_size,
                                   bool require_writable) {
    if (physical_address == NULL || mapping_size == NULL) return false;

    volatile uint64_t *table =
        (volatile uint64_t *)memory_phys_to_virt(root_physical);
    if (table == NULL) return false;

    for (unsigned int level = levels; level > 0U; --level) {
        const unsigned int shift = 12U + 9U * (level - 1U);
        const uint64_t index = (address >> shift) & 0x1FFU;
        const uint64_t entry = table[index];

        if ((entry & (PAGE_PRESENT | PAGE_USER)) !=
            (PAGE_PRESENT | PAGE_USER) ||
            (require_writable && (entry & PAGE_WRITABLE) == 0U)) {
            return false;
        }

        if (level == 1U) {
            *mapping_size = 1ULL << 12;
            *physical_address = (entry & PAGE_ADDRESS_MASK) |
                                (address & (*mapping_size - 1ULL));
            return true;
        }

        if ((entry & PAGE_LARGE) != 0U) {
            if (level != 2U && level != 3U) return false;
            *mapping_size = 1ULL << shift;
            const uint64_t base_mask =
                PAGE_ADDRESS_MASK & ~(*mapping_size - 1ULL);
            *physical_address = (entry & base_mask) |
                                (address & (*mapping_size - 1ULL));
            return true;
        }

        table = (volatile uint64_t *)memory_phys_to_virt(
            entry & PAGE_ADDRESS_MASK);
        if (table == NULL) return false;
    }

    return false;
}

/*
 * Copy a bounded user range into kernel-owned memory. The range must remain
 * in the lower canonical user half and every traversed page must have the
 * U/S bit set at every level. The caller must separately impose an ABI limit.
 */
bool paging_copy_from_user(void *destination, uint64_t source, size_t length) {
    if (length == 0U) return true;
    if (destination == NULL) return false;

    const uint64_t byte_count = (uint64_t)length;
    if ((size_t)byte_count != length || source > UINT64_MAX - byte_count) {
        return false;
    }

    uint64_t cr3, cr4;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile ("mov %%cr4, %0" : "=r"(cr4));

    const unsigned int levels = (cr4 & CR4_LA57) != 0U ? 5U : 4U;
    const unsigned int user_address_bits = levels == 5U ? 56U : 47U;
    const uint64_t user_limit = 1ULL << user_address_bits;
    const uint64_t end = source + byte_count;
    if (source >= user_limit || end > user_limit) return false;

    const uint64_t root_physical = cr3 & PAGE_ADDRESS_MASK;
    uint8_t *output = (uint8_t *)destination;
    uint64_t current = source;
    uint64_t remaining = byte_count;

    while (remaining != 0U) {
        uint64_t physical;
        uint64_t mapping_size;
        if (!translate_user_address(root_physical, levels, current,
                                    &physical, &mapping_size, false)) {
            return false;
        }

        const uint64_t mapping_offset = current & (mapping_size - 1ULL);
        uint64_t chunk = mapping_size - mapping_offset;
        if (chunk > remaining) chunk = remaining;

        const volatile uint8_t *input =
            (const volatile uint8_t *)memory_phys_to_virt(physical);
        if (input == NULL) return false;

        for (uint64_t i = 0; i < chunk; ++i) output[i] = input[i];

        output += (size_t)chunk;
        current += chunk;
        remaining -= chunk;
    }

    return true;
}


/* Validate every page of a user destination, including its writable bit. */
bool paging_user_range_writable(uint64_t destination, size_t length) {
    if (length == 0U) return true;
    const uint64_t byte_count = (uint64_t)length;
    if ((size_t)byte_count != length ||
        destination > UINT64_MAX - byte_count) return false;

    uint64_t cr3, cr4;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile ("mov %%cr4, %0" : "=r"(cr4));
    const unsigned int levels = (cr4 & CR4_LA57) != 0U ? 5U : 4U;
    const unsigned int user_bits = levels == 5U ? 56U : 47U;
    const uint64_t user_limit = 1ULL << user_bits;
    const uint64_t end = destination + byte_count;
    if (destination >= user_limit || end > user_limit) return false;

    const uint64_t root_physical = cr3 & PAGE_ADDRESS_MASK;
    uint64_t current = destination;
    uint64_t remaining = byte_count;
    while (remaining != 0U) {
        uint64_t physical;
        uint64_t mapping_size;
        if (!translate_user_address(root_physical, levels, current,
                                    &physical, &mapping_size, true)) {
            return false;
        }
        (void)physical;
        const uint64_t offset = current & (mapping_size - 1ULL);
        uint64_t chunk = mapping_size - offset;
        if (chunk > remaining) chunk = remaining;
        current += chunk;
        remaining -= chunk;
    }
    return true;
}

/* Copy kernel-owned bytes into a validated, writable user range. */
bool paging_copy_to_user(uint64_t destination, const void *source,
                         size_t length) {
    if (length == 0U) return true;
    if (source == NULL ||
        !paging_user_range_writable(destination, length)) return false;

    uint64_t cr3, cr4;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile ("mov %%cr4, %0" : "=r"(cr4));
    const unsigned int levels = (cr4 & CR4_LA57) != 0U ? 5U : 4U;
    const uint64_t root_physical = cr3 & PAGE_ADDRESS_MASK;
    const uint8_t *input = (const uint8_t *)source;
    uint64_t current = destination;
    uint64_t remaining = (uint64_t)length;

    while (remaining != 0U) {
        uint64_t physical;
        uint64_t mapping_size;
        if (!translate_user_address(root_physical, levels, current,
                                    &physical, &mapping_size, true)) {
            return false;
        }
        const uint64_t offset = current & (mapping_size - 1ULL);
        uint64_t chunk = mapping_size - offset;
        if (chunk > remaining) chunk = remaining;

        volatile uint8_t *output =
            (volatile uint8_t *)memory_phys_to_virt(physical);
        if (output == NULL) return false;
        for (uint64_t i = 0; i < chunk; ++i) output[i] = input[i];

        input += (size_t)chunk;
        current += chunk;
        remaining -= chunk;
    }
    return true;
}
