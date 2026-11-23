#ifndef NEXUS_NXFS_V3_LAYOUT_H
#define NEXUS_NXFS_V3_LAYOUT_H

#include <stdbool.h>
#include <stdint.h>

/* NXFS v3 layout prototype. This planner does not format or write a disk. */
/* Keep it separate from the v2 formatter until v3 codecs, journal replay,
 * migration fixtures, and fault-injection tests are complete. */
#define NEXUS_NXFS_V3_VERSION 3U
#define NEXUS_NXFS_V3_JOURNAL_BLOCKS 256U
#define NEXUS_NXFS_V3_MAX_INODES 16384U
#define NEXUS_NXFS_V3_INODE_BYTES 128U
#define NEXUS_NXFS_V3_MIN_DATA_BLOCKS 8U

struct nexus_nxfs_v3_layout {
    uint32_t block_size;
    uint64_t partition_blocks;
    uint64_t journal_start;
    uint64_t journal_blocks;
    uint64_t bitmap_start;
    uint64_t bitmap_blocks;
    uint64_t inode_start;
    uint64_t inode_blocks;
    uint64_t data_start;
    uint64_t data_blocks;
    uint32_t max_inodes;
};

/*
 * Compute a checked, partition-relative NXFS v3 metadata/data layout.
 * Superblocks occupy blocks 0 and 1. The planner reserves a fixed journal and
 * inode table, sizes the allocation bitmap from the remaining data region,
 * and assigns every remaining block to data. No disk I/O is performed.
 */
bool nexus_nxfs_v3_layout_create(uint64_t partition_blocks,
                                uint32_t block_size,
                                struct nexus_nxfs_v3_layout *out);

#endif
