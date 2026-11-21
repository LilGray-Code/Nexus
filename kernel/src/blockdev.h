#ifndef NEXUS_BLOCKDEV_H
#define NEXUS_BLOCKDEV_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NEXUS_BLOCKDEV_MAX_DEVICES 8U
#define NEXUS_BLOCKDEV_NAME_MAX 16U
#define NEXUS_BLOCKDEV_READ_ONLY 0x00000001U

typedef bool (*nexus_blockdev_read_fn)(void *context,
                                       uint64_t lba,
                                       uint32_t block_count,
                                       void *buffer);

struct nexus_block_device {
    char name[NEXUS_BLOCKDEV_NAME_MAX];
    uint64_t block_count;
    uint32_t block_size;
    uint32_t max_blocks_per_request;
    uint32_t flags;
    void *context;
    nexus_blockdev_read_fn read;
};

void blockdev_init(void);
bool blockdev_register(const struct nexus_block_device *device);
size_t blockdev_count(void);
const struct nexus_block_device *blockdev_at(size_t index);
bool blockdev_read(const struct nexus_block_device *device,
                   uint64_t lba, uint32_t block_count, void *buffer);

#endif
