#ifndef NEXUS_GPT_H
#define NEXUS_GPT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "blockdev.h"

#define NEXUS_GPT_MAX_PARTITIONS 16U

struct nexus_gpt_partition {
    uint8_t type_guid[16];
    uint8_t unique_guid[16];
    uint64_t first_lba;
    uint64_t last_lba;
    uint64_t attributes;
};

bool gpt_scan_device(const struct nexus_block_device *device);
size_t gpt_partition_count(void);
const struct nexus_gpt_partition *gpt_partition_at(size_t index);

#endif
