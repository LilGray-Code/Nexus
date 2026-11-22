#ifndef NEXUS_ELF64_DYNAMIC_H
#define NEXUS_ELF64_DYNAMIC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NEXUS_ELF64_DYNAMIC_MAX_NEEDED 16U

struct nexus_elf64_dynamic_info {
    bool position_independent;
    bool has_interpreter;
    bool has_rela;
    uint32_t needed_count;
    uint64_t rela_address;
    uint64_t rela_size;
    uint64_t rela_entry_size;
};

/* Validate dynamic-link metadata in an x86-64 ET_DYN image. This inspection
 * stage does not relocate or execute the image.
 */
bool nexus_elf64_dynamic_parse(const void *file, size_t file_size,
                               struct nexus_elf64_dynamic_info *info);

#endif
