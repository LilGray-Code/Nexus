#include "nxfs_v3_layout.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static void verify_layout(uint64_t blocks, uint32_t block_size) {
    struct nexus_nxfs_v3_layout layout;
    assert(nexus_nxfs_v3_layout_create(blocks, block_size, &layout));
    assert(layout.block_size == block_size);
    assert(layout.partition_blocks == blocks);
    assert(layout.journal_start == 2U);
    assert(layout.journal_blocks == NEXUS_NXFS_V3_JOURNAL_BLOCKS);
    assert(layout.bitmap_start == layout.journal_start + layout.journal_blocks);
    assert(layout.inode_start == layout.bitmap_start + layout.bitmap_blocks);
    assert(layout.data_start == layout.inode_start + layout.inode_blocks);
    assert(layout.data_start + layout.data_blocks == blocks);
    assert(layout.max_inodes == NEXUS_NXFS_V3_MAX_INODES);
    assert(layout.data_blocks >= NEXUS_NXFS_V3_MIN_DATA_BLOCKS);
    assert(layout.bitmap_blocks * (uint64_t)block_size * 8U >=
           layout.data_blocks);
    assert((layout.bitmap_blocks - 1U) * (uint64_t)block_size * 8U <
           layout.data_blocks);
}

static void test_small_and_large_volumes(void) {
    verify_layout(1024ULL * 1024ULL, 512U);
    verify_layout(64ULL * 1024ULL * 1024ULL * 1024ULL / 512ULL, 512U);
    verify_layout(4ULL * 1024ULL * 1024ULL * 1024ULL * 1024ULL / 512ULL,
                  512U);
    verify_layout(4ULL * 1024ULL * 1024ULL * 1024ULL * 1024ULL / 4096ULL,
                  4096U);
}

static void test_capacity_scales_without_fixed_data_ceiling(void) {
    struct nexus_nxfs_v3_layout small, large;
    assert(nexus_nxfs_v3_layout_create(64ULL * 1024ULL * 1024ULL / 512ULL,
                                       512U, &small));
    assert(nexus_nxfs_v3_layout_create(4ULL * 1024ULL * 1024ULL * 1024ULL *
                                       1024ULL / 512ULL, 512U, &large));
    assert(large.data_blocks > small.data_blocks * 20U);
    assert(large.data_blocks > UINT32_MAX);
}

static void test_rejects_invalid_and_too_small_inputs(void) {
    struct nexus_nxfs_v3_layout layout;
    assert(!nexus_nxfs_v3_layout_create(0U, 512U, &layout));
    assert(!nexus_nxfs_v3_layout_create(100000U, 768U, &layout));
    assert(!nexus_nxfs_v3_layout_create(100000U, 1024U, &layout));
    assert(!nexus_nxfs_v3_layout_create(100000U, 512U, NULL));
    assert(!nexus_nxfs_v3_layout_create(1000U, 512U, &layout));
    assert(layout.partition_blocks == 0U);
}

int main(void) {
    test_small_and_large_volumes();
    test_capacity_scales_without_fixed_data_ceiling();
    test_rejects_invalid_and_too_small_inputs();
    puts("NEXUS NXFS V3 LAYOUT TESTS PASSED");
    return 0;
}
