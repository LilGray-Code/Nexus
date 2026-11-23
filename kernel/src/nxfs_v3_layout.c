#include "nxfs_v3_layout.h"

#include <stddef.h>
#include <stdint.h>

static bool add_u64(uint64_t left, uint64_t right, uint64_t *out) {
    if (out == NULL || left > UINT64_MAX - right) return false;
    *out = left + right;
    return true;
}

static bool mul_u64(uint64_t left, uint64_t right, uint64_t *out) {
    if (out == NULL || (right != 0U && left > UINT64_MAX / right))
        return false;
    *out = left * right;
    return true;
}

static bool ceil_div_u64(uint64_t value, uint64_t divisor,
                         uint64_t *out) {
    uint64_t rounded;
    if (divisor == 0U || out == NULL ||
        !add_u64(value, divisor - 1U, &rounded)) return false;
    *out = rounded / divisor;
    return true;
}

bool nexus_nxfs_v3_layout_create(uint64_t partition_blocks,
                                uint32_t block_size,
                                struct nexus_nxfs_v3_layout *out) {
    uint64_t inode_bytes;
    uint64_t inode_blocks;
    uint64_t bitmap_bits_per_block;
    uint64_t fixed_blocks;
    uint64_t bitmap_blocks = 1U;
    uint64_t data_blocks = 0U;
    uint64_t next_bitmap_blocks;
    uint64_t bitmap_start;
    uint64_t inode_start;
    uint64_t data_start;
    uint64_t end;

    if (out == NULL) return false;
    *out = (struct nexus_nxfs_v3_layout){0};

    if ((block_size != 512U && block_size != 4096U) ||
        !mul_u64(NEXUS_NXFS_V3_MAX_INODES,
                 NEXUS_NXFS_V3_INODE_BYTES, &inode_bytes) ||
        !ceil_div_u64(inode_bytes, block_size, &inode_blocks) ||
        !mul_u64(block_size, 8U, &bitmap_bits_per_block) ||
        !add_u64(2U, NEXUS_NXFS_V3_JOURNAL_BLOCKS, &fixed_blocks) ||
        !add_u64(fixed_blocks, inode_blocks, &fixed_blocks) ||
        partition_blocks <= fixed_blocks) return false;

    /*
     * Bitmap size depends on the data blocks it describes. Recompute until
     * stable; because each bitmap block describes block_size*8 data blocks,
     * this converges monotonically in at most a handful of iterations.
     */
    for (unsigned int pass = 0U; pass < 8U; ++pass) {
        uint64_t metadata_blocks;
        if (!add_u64(fixed_blocks, bitmap_blocks, &metadata_blocks) ||
            partition_blocks <= metadata_blocks) return false;
        data_blocks = partition_blocks - metadata_blocks;
        if (!ceil_div_u64(data_blocks, bitmap_bits_per_block,
                          &next_bitmap_blocks)) return false;
        if (next_bitmap_blocks == bitmap_blocks) break;
        bitmap_blocks = next_bitmap_blocks;
        if (pass == 7U) return false;
    }

    if (data_blocks < NEXUS_NXFS_V3_MIN_DATA_BLOCKS ||
        !add_u64(2U, NEXUS_NXFS_V3_JOURNAL_BLOCKS, &bitmap_start) ||
        !add_u64(bitmap_start, bitmap_blocks, &inode_start) ||
        !add_u64(inode_start, inode_blocks, &data_start) ||
        !add_u64(data_start, data_blocks, &end) ||
        end != partition_blocks) return false;

    *out = (struct nexus_nxfs_v3_layout){
        .block_size = block_size,
        .partition_blocks = partition_blocks,
        .journal_start = 2U,
        .journal_blocks = NEXUS_NXFS_V3_JOURNAL_BLOCKS,
        .bitmap_start = bitmap_start,
        .bitmap_blocks = bitmap_blocks,
        .inode_start = inode_start,
        .inode_blocks = inode_blocks,
        .data_start = data_start,
        .data_blocks = data_blocks,
        .max_inodes = NEXUS_NXFS_V3_MAX_INODES
    };
    return true;
}
