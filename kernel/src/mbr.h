#ifndef NEXUS_MBR_H
#define NEXUS_MBR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "blockdev.h"

#define NEXUS_MBR_MAX_PARTITIONS 36U
#define NEXUS_MBR_MAX_EBR_HOPS 32U

struct nexus_mbr_partition {
    uint8_t type;
    bool bootable;
    bool logical;
    uint64_t first_lba;
    uint64_t block_count;
};

/* Scans classic MBR and bounded EBR chains; protective GPT MBR is reported
 * separately so callers can prefer the CRC-validated GPT parser.
 */
bool mbr_scan_device(const struct nexus_block_device *device);
bool mbr_has_protective_gpt(void);
uint32_t mbr_disk_signature(void);
size_t mbr_partition_count(void);
const struct nexus_mbr_partition *mbr_partition_at(size_t index);
bool mbr_type_is_extended(uint8_t type);
bool mbr_type_is_fat32(uint8_t type);

#endif
