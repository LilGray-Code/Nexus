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
    if (!memory_alloc_page(physical)) return false;

    volatile uint64_t *table =
        (volatile uint64_t *)memory_phys_to_virt(*physical);
    if (table == NULL) return false;

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
    for (unsigned int level = levels; level > 1U; --level) {
        uint64_t next_physical;
        volatile uint64_t *next_table;
        if (!allocate_table(&next_physical, &next_table)) return false;

        parent[(level == levels) ? root_index : 0] =
            (next_physical & PAGE_ADDRESS_MASK) |
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
 * Clone the active kernel page-table root and remove the low canonical slot.
 * Kernel mappings in the high half remain shared, while user mappings are
 * created beneath a private lower-half tree.
 */
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

    for (size_t i = 0; i < 512U; ++i) user_root[i] = kernel_root[i];
    user_root[0] = 0;
    return true;
}

/* Map one previously-unmapped 4 KiB page into a user address space. */
bool paging_map_user_page(uint64_t root_physical, uint64_t virtual_address,
                          uint64_t page_physical, bool writable, bool executable) {
    if ((virtual_address & 0xFFFULL) != 0U ||
        (page_physical & 0xFFFULL) != 0U) {
        return false;
    }

    uint64_t cr4;
    __asm__ volatile ("mov %%cr4, %0" : "=r"(cr4));
    const unsigned int levels = (cr4 & CR4_LA57) != 0U ? 5U : 4U;
    const unsigned int user_bits = levels == 5U ? 56U : 47U;
    if (virtual_address >= (1ULL << user_bits)) return false;

    volatile uint64_t *table =
        (volatile uint64_t *)memory_phys_to_virt(root_physical);
    if (table == NULL) return false;

    for (unsigned int level = levels; level > 1U; --level) {
        const unsigned int shift = 12U + 9U * (level - 1U);
        const unsigned int index =
            (unsigned int)((virtual_address >> shift) & 0x1FFU);
        uint64_t entry = table[index];

        if ((entry & PAGE_PRESENT) == 0U) {
            uint64_t next_physical;
            volatile uint64_t *next_table;
            if (!allocate_table(&next_physical, &next_table)) return false;
            entry = (next_physical & PAGE_ADDRESS_MASK) |
                    PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
            table[index] = entry;
            table = next_table;
        } else {
            if ((entry & PAGE_USER) == 0U || (entry & PAGE_LARGE) != 0U) {
                return false;
            }
            table = (volatile uint64_t *)memory_phys_to_virt(
                entry & PAGE_ADDRESS_MASK);
            if (table == NULL) return false;
        }
    }

    const unsigned int leaf_index =
        (unsigned int)((virtual_address >> 12) & 0x1FFU);
    if ((table[leaf_index] & PAGE_PRESENT) != 0U) return false;

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
                                   uint64_t *mapping_size) {
    if (physical_address == NULL || mapping_size == NULL) return false;

    volatile uint64_t *table =
        (volatile uint64_t *)memory_phys_to_virt(root_physical);
    if (table == NULL) return false;

    for (unsigned int level = levels; level > 0U; --level) {
        const unsigned int shift = 12U + 9U * (level - 1U);
        const uint64_t index = (address >> shift) & 0x1FFU;
        const uint64_t entry = table[index];

        if ((entry & (PAGE_PRESENT | PAGE_USER)) !=
            (PAGE_PRESENT | PAGE_USER)) {
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
                                    &physical, &mapping_size)) {
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
