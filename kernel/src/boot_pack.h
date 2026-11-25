#ifndef NEXUS_BOOT_PACK_H
#define NEXUS_BOOT_PACK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NEXUS_BOOT_PACK_MAX_FILES 32U
#define NEXUS_BOOT_PACK_NAME_MAX 32U
#define NEXUS_BOOT_PACK_MAX_BYTES (2U * 1024U * 1024U)

struct nexus_boot_pack_entry {
    char name[NEXUS_BOOT_PACK_NAME_MAX];
    const uint8_t *data;
    uint32_t size;
    uint32_t crc32;
};

/* Validate a complete NXSPACK1 image before publishing any of its files. */
bool nexus_boot_pack_init(const void *image, size_t image_size);
bool nexus_boot_pack_ready(void);
size_t nexus_boot_pack_count(void);
const struct nexus_boot_pack_entry *nexus_boot_pack_entry_at(size_t index);
const struct nexus_boot_pack_entry *nexus_boot_pack_find(const char *name);
bool nexus_boot_pack_read(const struct nexus_boot_pack_entry *entry,
                          uint32_t offset, void *buffer,
                          size_t requested, size_t *bytes_read);

#endif
