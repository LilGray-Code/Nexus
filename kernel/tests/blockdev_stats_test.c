#include "blockdev.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint8_t media[32U * 512U];
static bool fail_read;
static bool fail_write;
static bool fail_flush;

static bool memory_read(void *context, uint64_t lba, uint32_t count,
                        void *buffer) {
    (void)context;
    if (fail_read || lba >= 32U || count > 32U - lba) return false;
    memcpy(buffer, media + (size_t)lba * 512U, (size_t)count * 512U);
    return true;
}

static bool memory_write(void *context, uint64_t lba, uint32_t count,
                         const void *buffer) {
    (void)context;
    if (fail_write || lba >= 32U || count > 32U - lba) return false;
    memcpy(media + (size_t)lba * 512U, buffer, (size_t)count * 512U);
    return true;
}

static bool memory_flush(void *context) {
    (void)context;
    return !fail_flush;
}

int main(void) {
    memset(media, 0, sizeof(media));
    fail_read = fail_write = fail_flush = false;
    blockdev_init();

    const struct nexus_block_device candidate = {
        .name = "test0",
        .stable_id = "MEMORY-TEST-DISK",
        .model = "Nexus Memory Test Disk",
        .block_count = 32U,
        .block_size = 512U,
        .max_blocks_per_request = 8U,
        .flags = NEXUS_BLOCKDEV_WRITABLE,
        .transport = NEXUS_BLOCKDEV_TRANSPORT_AHCI,
        .context = media,
        .read = memory_read,
        .write = memory_write,
        .flush = memory_flush
    };
    assert(blockdev_register(&candidate));
    const struct nexus_block_device *disk = blockdev_at(0U);
    assert(disk != NULL);

    uint8_t sector[1024];
    memset(sector, 0xA5, sizeof(sector));
    assert(blockdev_write(disk, 2U, 2U, sector));
    assert(blockdev_flush(disk));
    uint8_t readback[1024] = {0};
    assert(blockdev_read(disk, 2U, 2U, readback));
    assert(memcmp(sector, readback, sizeof(sector)) == 0);

    fail_read = true;
    assert(!blockdev_read(disk, 0U, 1U, readback));
    fail_read = false;
    fail_write = true;
    assert(!blockdev_write(disk, 0U, 1U, sector));
    fail_write = false;
    fail_flush = true;
    assert(!blockdev_flush(disk));
    fail_flush = false;

    struct nexus_blockdev_stats stats;
    assert(blockdev_get_stats(disk, &stats));
    assert(stats.read_requests == 2U && stats.read_blocks == 2U);
    assert(stats.read_errors == 1U);
    assert(stats.write_requests == 2U && stats.write_blocks == 2U);
    assert(stats.write_errors == 1U);
    assert(stats.flush_requests == 2U && stats.flush_errors == 1U);

    struct nexus_block_device foreign = candidate;
    assert(!blockdev_get_stats(&foreign, &stats));
    assert(!blockdev_get_stats(disk, NULL));
    puts("NEXUS BLOCK DEVICE I/O TELEMETRY TESTS PASSED");
    return 0;
}
