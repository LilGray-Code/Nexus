#ifndef NEXUS_ELF64_H
#define NEXUS_ELF64_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NEXUS_ELF64_MAX_LOAD_SEGMENTS 16U

#define NEXUS_ELF64_SEGMENT_READ  0x04U
#define NEXUS_ELF64_SEGMENT_WRITE 0x02U
#define NEXUS_ELF64_SEGMENT_EXEC  0x01U

struct nexus_elf64_segment {
    uint64_t file_offset;
    uint64_t virtual_address;
    uint64_t file_size;
    uint64_t memory_size;
    uint64_t alignment;
    uint32_t flags;
};

struct nexus_elf64_image {
    uint64_t entry_point;
    uint16_t segment_count;
    struct nexus_elf64_segment segments[NEXUS_ELF64_MAX_LOAD_SEGMENTS];
};

/* Validate a bounded little-endian x86-64 ET_EXEC image without loading it. */
bool nexus_elf64_parse(const void *file, size_t file_size,
                       struct nexus_elf64_image *image);

/*
 * Load a validated static image into a private lower-half address space.
 * The returned stack is a single zeroed page. The bootstrap allocator is
 * monotonic, so failed loads do not yet reclaim partially allocated pages.
 */
bool nexus_elf64_load(const void *file, size_t file_size,
                      uint64_t *root_physical, uint64_t *entry_point,
                      uint64_t *stack_pointer);

/* Pure in-memory parser tests run during the kernel boot self-tests. */
bool nexus_elf64_selftest(void);

#endif
