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

/* Apply only symbol-free R_X86_64_RELATIVE relocations. image represents the
 * contiguous virtual range [virtual_base, virtual_base + image_size).
 * Every relocation target must lie in a writable PT_LOAD segment.
 */
bool nexus_elf64_dynamic_apply_relative(const void *file, size_t file_size,
                                        uint64_t virtual_base,
                                        uint8_t *image, size_t image_size,
                                        uint64_t load_bias);

#endif
