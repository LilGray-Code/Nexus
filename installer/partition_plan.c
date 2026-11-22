#include "partition_plan.h"

#define GPT_ENTRY_COUNT 128U
#define GPT_ENTRY_BYTES 128U
#define GPT_HEADER_BYTES 92U
#define GPT_MIN_BLOCK_SIZE 512U
#define GPT_MAX_BLOCK_SIZE 4096U

static bool add_u64(uint64_t a, uint64_t b, uint64_t *out) {
    if (out == NULL || a > UINT64_MAX - b) return false;
    *out = a + b;
    return true;
}

static bool mul_u64(uint64_t a, uint64_t b, uint64_t *out) {
    if (out == NULL || (b != 0U && a > UINT64_MAX / b)) return false;
    *out = a * b;
    return true;
}

static bool round_up_u64(uint64_t value, uint64_t alignment,
                         uint64_t *out) {
    uint64_t remainder;
    if (alignment == 0U || out == NULL) return false;
    remainder = value % alignment;
    return remainder == 0U ? (*out = value, true)
                           : add_u64(value, alignment - remainder, out);
}

static bool mib_to_blocks(uint32_t mib, uint32_t block_size,
                          uint64_t *blocks) {
    uint64_t bytes;
    if (!mul_u64((uint64_t)mib, 1024U * 1024U, &bytes) ||
        bytes % block_size != 0U) return false;
    *blocks = bytes / block_size;
    return true;
}

static bool append_partition(struct nexus_partition_plan *plan,
                             enum nexus_partition_kind kind,
                             uint64_t first, uint64_t blocks,
                             uint64_t alignment, uint64_t *next) {
    uint64_t last;
    uint64_t after;
    if (plan == NULL || next == NULL || blocks == 0U ||
        plan->partition_count >= NEXUS_PLAN_MAX_PARTITIONS ||
        first % alignment != 0U ||
        !add_u64(first, blocks - 1U, &last) ||
        !add_u64(last, 1U, &after)) return false;
    plan->partitions[plan->partition_count++] =
        (struct nexus_planned_partition){kind, first, last};
    *next = after;
    return true;
}

bool nexus_partition_plan_create(uint64_t disk_block_count,
                                 uint32_t logical_block_size,
                                 enum nexus_firmware_mode firmware,
                                 struct nexus_partition_plan *out) {
    uint64_t entry_bytes;
    uint64_t entry_blocks;
    uint64_t alignment;
    uint64_t first;
    uint64_t last;
    uint64_t blocks;
    uint64_t next;
    uint64_t minimum_system;
    uint64_t recovery_blocks = 0U;
    uint64_t esp_blocks = 0U;
    uint64_t bios_blocks = 0U;

    if (out == NULL) return false;
    *out = (struct nexus_partition_plan){0};
    if (logical_block_size < GPT_MIN_BLOCK_SIZE ||
        logical_block_size > GPT_MAX_BLOCK_SIZE ||
        (logical_block_size & (logical_block_size - 1U)) != 0U ||
        NEXUS_PLAN_ALIGNMENT_BYTES % logical_block_size != 0U ||
        (firmware != NEXUS_FIRMWARE_UEFI &&
         firmware != NEXUS_FIRMWARE_LEGACY_BIOS &&
         firmware != NEXUS_FIRMWARE_DUAL) ||
        (firmware != NEXUS_FIRMWARE_UEFI && logical_block_size != 512U) ||
        disk_block_count < 128U) return false;

    alignment = NEXUS_PLAN_ALIGNMENT_BYTES / logical_block_size;
    if (!mul_u64(GPT_ENTRY_COUNT, GPT_ENTRY_BYTES, &entry_bytes))
        return false;
    entry_blocks = (entry_bytes + logical_block_size - 1U) /
                   logical_block_size;
    if (!add_u64(2U, entry_blocks, &first) ||
        !round_up_u64(first, alignment, &first) ||
        disk_block_count <= entry_blocks + 1U) return false;
    last = disk_block_count - entry_blocks - 2U;
    if (first > last) return false;

    out->logical_block_size = logical_block_size;
    out->disk_block_count = disk_block_count;
    out->first_usable_lba = first;
    out->last_usable_lba = last;

    if (firmware == NEXUS_FIRMWARE_UEFI || firmware == NEXUS_FIRMWARE_DUAL) {
        if (!mib_to_blocks(NEXUS_PLAN_ESP_MIB, logical_block_size,
                           &esp_blocks)) return false;
        if (!append_partition(out, NEXUS_PARTITION_ESP, first, esp_blocks,
                              alignment, &next)) goto fail;
        if (!round_up_u64(next, alignment, &first)) goto fail;
    }
    if (firmware == NEXUS_FIRMWARE_LEGACY_BIOS ||
        firmware == NEXUS_FIRMWARE_DUAL) {
        if (!mib_to_blocks(NEXUS_PLAN_BIOS_BOOT_MIB, logical_block_size,
                           &bios_blocks) ||
            !append_partition(out, NEXUS_PARTITION_BIOS_BOOT, first,
                              bios_blocks, alignment, &next) ||
            !round_up_u64(next, alignment, &first)) goto fail;
    }

    if (!mib_to_blocks(NEXUS_PLAN_SYSTEM_MIB, logical_block_size,
                       &minimum_system) ||
        !append_partition(out, NEXUS_PARTITION_SYSTEM, first,
                          minimum_system, alignment, &next) ||
        !round_up_u64(next, alignment, &first) ||
        !mib_to_blocks(NEXUS_PLAN_RECOVERY_MIB, logical_block_size,
                       &recovery_blocks) ||
        !append_partition(out, NEXUS_PARTITION_RECOVERY, first,
                          recovery_blocks, alignment, &next) ||
        !round_up_u64(next, alignment, &first)) goto fail;

    if (first > last) goto fail;
    blocks = last - first + 1U;
    if (blocks < alignment) goto fail;
    if (!append_partition(out, NEXUS_PARTITION_DATA, first, blocks,
                          alignment, &next) || next - 1U > last) goto fail;

    return true;

fail:
    *out = (struct nexus_partition_plan){0};
    return false;
}
