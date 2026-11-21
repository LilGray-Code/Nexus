#include "gpt_install.h"

#include <stddef.h>
#include <stdint.h>

/* Installer is a single serialized kernel operation. Keep the large GPT
 * workspace out of the limited kernel stack. */
static struct nexus_gpt_image_buffers install_image_workspace;
static uint8_t install_verify_workspace[NEXUS_GPT_IMAGE_ENTRY_BYTES];
static bool install_transaction_busy;

static bool bytes_equal(const uint8_t *a, const uint8_t *b, size_t length) {
    for (size_t i = 0U; i < length; ++i)
        if (a[i] != b[i]) return false;
    return true;
}

static enum nexus_gpt_install_result write_and_verify(
    const struct nexus_gpt_install_target *target,
    uint64_t lba, uint32_t blocks, const uint8_t *expected,
    size_t bytes) {
    uint8_t *verify = install_verify_workspace;
    const size_t capacity = (size_t)blocks * target->block_size;
    const uint32_t max_blocks = target->max_blocks_per_request != 0U
        ? target->max_blocks_per_request : blocks;
    if (bytes > capacity || capacity > sizeof(install_verify_workspace) ||
        max_blocks == 0U) return NEXUS_GPT_INSTALL_INVALID;

    /* Respect the controller's transfer limit. A 512-byte-sector GPT entry
     * array is 32 sectors and exceeds this NVMe driver's eight-sector DMA cap. */
    uint32_t completed = 0U;
    while (completed < blocks) {
        uint32_t chunk = blocks - completed;
        if (chunk > max_blocks) chunk = max_blocks;
        const size_t offset = (size_t)completed * target->block_size;
        if (!target->write(target->context, lba + completed, chunk,
                           expected + offset))
            return NEXUS_GPT_INSTALL_IO_ERROR;
        completed += chunk;
    }
    if (!target->flush(target->context))
        return NEXUS_GPT_INSTALL_IO_ERROR;

    completed = 0U;
    while (completed < blocks) {
        uint32_t chunk = blocks - completed;
        if (chunk > max_blocks) chunk = max_blocks;
        const size_t offset = (size_t)completed * target->block_size;
        const size_t chunk_bytes = (size_t)chunk * target->block_size;
        for (size_t i = 0U; i < chunk_bytes; ++i) verify[i] = 0U;
        if (!target->read(target->context, lba + completed, chunk, verify) ||
            !bytes_equal(verify, expected + offset, chunk_bytes))
            return NEXUS_GPT_INSTALL_VERIFY_ERROR;
        completed += chunk;
    }
    return NEXUS_GPT_INSTALL_OK;
}

static enum nexus_gpt_install_result nexus_gpt_install_apply_locked(
    const struct nexus_gpt_install_target *target,
    const struct nexus_partition_plan *plan,
    const uint8_t disk_guid[16],
    const uint8_t *partition_guids,
    bool first_confirmation,
    bool second_confirmation) {
    struct nexus_gpt_image_buffers *image = &install_image_workspace;
    uint64_t entry_blocks;
    uint64_t backup_entries_lba;
    const uint64_t backup_header_lba =
        target != NULL && target->block_count > 0U
            ? target->block_count - 1U : 0U;
    uint32_t block_size;
    enum nexus_gpt_install_result result;

    if (target == NULL || plan == NULL || disk_guid == NULL ||
        partition_guids == NULL || target->read == NULL ||
        target->write == NULL || target->flush == NULL ||
        target->block_count != plan->disk_block_count ||
        target->block_size != plan->logical_block_size ||
        (target->block_size != 512U && target->block_size != 4096U))
        return NEXUS_GPT_INSTALL_INVALID;
    if (!first_confirmation || !second_confirmation)
        return NEXUS_GPT_INSTALL_NOT_CONFIRMED;
    if (!target->identity_revalidated || !target->not_live_media ||
        !target->not_mounted)
        return NEXUS_GPT_INSTALL_UNSAFE_TARGET;
    if (!nexus_gpt_image_build(plan, disk_guid, partition_guids, image))
        return NEXUS_GPT_INSTALL_INVALID;

    block_size = target->block_size;
    entry_blocks = (NEXUS_GPT_IMAGE_ENTRY_BYTES + block_size - 1U) /
                   block_size;
    if (target->block_count <= entry_blocks + 2U)
        return NEXUS_GPT_INSTALL_INVALID;
    backup_entries_lba = backup_header_lba - entry_blocks;

    /* Preserve a valid backup GPT before replacing the primary metadata. */
    result = write_and_verify(target, backup_entries_lba,
                              (uint32_t)entry_blocks, image->backup_entries,
                              NEXUS_GPT_IMAGE_ENTRY_BYTES);
    if (result != NEXUS_GPT_INSTALL_OK) return result;
    result = write_and_verify(target, backup_header_lba, 1U,
                              image->backup_header, block_size);
    if (result != NEXUS_GPT_INSTALL_OK) return result;
    result = write_and_verify(target, 2U, (uint32_t)entry_blocks,
                              image->primary_entries,
                              NEXUS_GPT_IMAGE_ENTRY_BYTES);
    if (result != NEXUS_GPT_INSTALL_OK) return result;
    result = write_and_verify(target, 1U, 1U,
                              image->primary_header, block_size);
    if (result != NEXUS_GPT_INSTALL_OK) return result;
    result = write_and_verify(target, 0U, 1U,
                              image->protective_mbr, block_size);
    return result;
}

/* The large serialization and verification buffers are shared static storage.
 * Serialize access so a second installer request cannot corrupt an active write. */
enum nexus_gpt_install_result nexus_gpt_install_apply(
    const struct nexus_gpt_install_target *target,
    const struct nexus_partition_plan *plan,
    const uint8_t disk_guid[16],
    const uint8_t *partition_guids,
    bool first_confirmation,
    bool second_confirmation) {
    if (install_transaction_busy) return NEXUS_GPT_INSTALL_UNSAFE_TARGET;
    install_transaction_busy = true;
    const enum nexus_gpt_install_result result = nexus_gpt_install_apply_locked(
        target, plan, disk_guid, partition_guids,
        first_confirmation, second_confirmation);
    install_transaction_busy = false;
    return result;
}
