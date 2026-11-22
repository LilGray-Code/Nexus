#ifndef NEXUS_INSTALLER_PARTITION_PLAN_H
#define NEXUS_INSTALLER_PARTITION_PLAN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NEXUS_PLAN_MAX_PARTITIONS 5U
#define NEXUS_PLAN_ESP_MIB 512U
#define NEXUS_PLAN_BIOS_BOOT_MIB 2U
#define NEXUS_PLAN_SYSTEM_MIB 8192U
#define NEXUS_PLAN_RECOVERY_MIB 16U
#define NEXUS_PLAN_ALIGNMENT_BYTES (1024U * 1024U)

enum nexus_firmware_mode {
    NEXUS_FIRMWARE_UEFI = 1,
    NEXUS_FIRMWARE_LEGACY_BIOS = 2,
    NEXUS_FIRMWARE_DUAL = 3
};

enum nexus_partition_kind {
    NEXUS_PARTITION_ESP = 1,
    NEXUS_PARTITION_BIOS_BOOT = 2,
    NEXUS_PARTITION_SYSTEM = 3,
    NEXUS_PARTITION_DATA = 4,
  NEXUS_PARTITION_RECOVERY = 5
};

struct nexus_planned_partition {
    enum nexus_partition_kind kind;
    uint64_t first_lba;
    uint64_t last_lba;
};

struct nexus_partition_plan {
    uint32_t logical_block_size;
    uint64_t disk_block_count;
    uint64_t first_usable_lba;
    uint64_t last_usable_lba;
    size_t partition_count;
    struct nexus_planned_partition partitions[NEXUS_PLAN_MAX_PARTITIONS];
};

/*
 * Pure, read-only planning function. It never performs I/O and never writes
 * partition metadata. Data receives all remaining aligned usable space.
 */
bool nexus_partition_plan_create(uint64_t disk_block_count,
                                 uint32_t logical_block_size,
                                 enum nexus_firmware_mode firmware,
                                 struct nexus_partition_plan *out);

#endif
