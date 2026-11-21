#ifndef NEXUS_BIOS_INSTALL_H
#define NEXUS_BIOS_INSTALL_H

#include <stdbool.h>
#include <stdint.h>

struct nexus_bios_install_target {
    uint64_t block_count;
    uint32_t block_size;
    uint32_t max_blocks_per_request;
    void *context;
    bool (*read)(void *context,uint64_t lba,uint32_t blocks,void *buffer);
    bool (*write)(void *context,uint64_t lba,uint32_t blocks,const void *buffer);
    bool (*flush)(void *context);
};

/* Install a Limine BIOS image (first 512 bytes are MBR boot code, remainder
 * is stage 2) into LBA 0 and the selected GPT BIOS Boot partition. The caller
 * must authenticate the partition type and destructive-write authorization. */
bool nexus_bios_install_apply(const struct nexus_bios_install_target *target,
                              uint64_t bios_partition_first_lba,
                              uint64_t bios_partition_block_count,
                              const void *limine_bios_image,
                              uint32_t limine_bios_image_size);

#endif
