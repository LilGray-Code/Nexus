#include "blockdev.h"
#include "gpt.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TEST_DISK_BLOCKS 32768U
#define DATA_FIRST 12288U
#define DATA_LAST 32766U
#define SYSTEM_FIRST 4096U
#define SYSTEM_LAST 11263U

static struct nexus_gpt_partition partitions[3];
static const struct nexus_block_device *gpt_device;
static uint32_t write_calls;
static uint32_t flush_calls;

static const uint8_t nxfs_guid[16] = {
    0x46U,0x53U,0x58U,0x4EU,0x01U,0x00U,0x58U,0x4EU,
    0x9AU,0x11U,0x4EU,0x58U,0x55U,0x53U,0x46U,0x53U
};
static const uint8_t esp_guid[16] = {
    0x28U,0x73U,0x2AU,0xC1U,0x1FU,0xF8U,0xD2U,0x11U,
    0xBAU,0x4BU,0x00U,0xA0U,0xC9U,0x3EU,0xC9U,0x3BU
};
static const uint8_t boot_partition_id[16] = {
    0x11U,0x22U,0x33U,0x44U,0x55U,0x66U,0x77U,0x88U,
    0x99U,0xAAU,0xBBU,0xCCU,0xDDU,0xEEU,0xF0U,0x01U
};

static bool mock_read(void *context, uint64_t lba, uint32_t count,
                      void *buffer) {
    (void)context; (void)lba; (void)count;
    memset(buffer, 0, 512U * count);
    return true;
}
static bool mock_write(void *context, uint64_t lba, uint32_t count,
                       const void *buffer) {
    (void)context; (void)lba; (void)count; (void)buffer;
    ++write_calls;
    return true;
}
static bool mock_flush(void *context) {
    (void)context;
    ++flush_calls;
    return true;
}

bool gpt_scan_device(const struct nexus_block_device *device) {
    return device != NULL && device == gpt_device;
}
const uint8_t *gpt_scanned_disk_guid(void) {
    static const uint8_t disk_guid[16] = {1U};
    return disk_guid;
}
size_t gpt_partition_count(void) { return 3U; }
const struct nexus_gpt_partition *gpt_partition_at(size_t index) {
    return index < 3U ? &partitions[index] : NULL;
}

static void setup_partitions(void) {
    memset(partitions, 0, sizeof(partitions));
    memcpy(partitions[0].type_guid, esp_guid, 16U);
    memcpy(partitions[0].unique_guid, boot_partition_id, 16U);
    partitions[0].first_lba = 2048U;
    partitions[0].last_lba = 3071U;
    memcpy(partitions[1].type_guid, nxfs_guid, 16U);
    partitions[1].first_lba = SYSTEM_FIRST;
    partitions[1].last_lba = SYSTEM_LAST;
    memcpy(partitions[2].type_guid, nxfs_guid, 16U);
    partitions[2].first_lba = DATA_FIRST;
    partitions[2].last_lba = DATA_LAST;
}

static struct nexus_block_device make_device(const char *name,
                                              uintptr_t context_value) {
    struct nexus_block_device device = {0};
    (void)snprintf(device.name, sizeof(device.name), "%s", name);
    (void)snprintf(device.stable_id, sizeof(device.stable_id), "NVME-TEST-%s", name);
    (void)snprintf(device.model, sizeof(device.model), "Nexus test NVMe");
    device.block_count = TEST_DISK_BLOCKS;
    device.block_size = 512U;
    device.max_blocks_per_request = 8U;
    device.flags = NEXUS_BLOCKDEV_READ_ONLY | NEXUS_BLOCKDEV_INSTALL_CAPABLE;
    device.transport = NEXUS_BLOCKDEV_TRANSPORT_NVME;
    device.context = (void *)context_value;
    device.read = mock_read;
    device.write = mock_write;
    device.flush = mock_flush;
    return device;
}

int main(void) {
    setup_partitions();
    blockdev_init();

    int context_a = 1, context_b = 2;
    struct nexus_block_device input_a = make_device("nvme0n1", (uintptr_t)&context_a);
    assert(blockdev_register(&input_a));
    const struct nexus_block_device *disk_a = blockdev_at(0U);
    assert(disk_a != NULL);
    gpt_device = disk_a;

    uint8_t data[512] = {0};
    assert(!blockdev_write(disk_a, DATA_FIRST, 1U, data));
    assert(!blockdev_flush(disk_a));
    assert(!blockdev_nexus_data_write_enable(disk_a, SYSTEM_FIRST, SYSTEM_LAST));
    assert(!blockdev_nexus_data_write_enable(disk_a, DATA_FIRST, DATA_LAST));

    assert(blockdev_set_boot_media_from_gpt_partition_guid(boot_partition_id));
    assert(blockdev_boot_media_detection_complete());
    assert(blockdev_is_boot_media(disk_a));
    assert(!blockdev_write(disk_a, DATA_FIRST, 1U, data));
    assert(blockdev_nexus_data_write_enable(disk_a, DATA_FIRST, DATA_LAST));
    assert(blockdev_nexus_data_write_is_enabled(disk_a, DATA_FIRST, DATA_LAST));

    assert(blockdev_write(disk_a, DATA_FIRST, 1U, data));
    assert(!blockdev_write(disk_a, SYSTEM_FIRST, 1U, data));
    assert(!blockdev_write(disk_a, DATA_LAST, 2U, data));
    assert(blockdev_flush(disk_a));
    assert(write_calls == 1U);
    assert(flush_calls == 1U);

    blockdev_nexus_data_write_disable(disk_a);
    assert(!blockdev_nexus_data_write_is_enabled(disk_a, DATA_FIRST, DATA_LAST));
    assert(!blockdev_write(disk_a, DATA_FIRST, 1U, data));
    assert(!blockdev_flush(disk_a));

    struct nexus_block_device input_b = make_device("nvme1n1", (uintptr_t)&context_b);
    assert(blockdev_register(&input_b));
    const struct nexus_block_device *disk_b = blockdev_at(1U);
    assert(disk_b != NULL);
    gpt_device = disk_b;
    assert(!blockdev_nexus_data_write_enable(disk_b, DATA_FIRST, DATA_LAST));

    puts("PASS: ordinary writes require identified boot disk and exact Nexus data partition");
    return 0;
}
