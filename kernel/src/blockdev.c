#include "blockdev.h"

#include <stdint.h>

static struct nexus_block_device devices[NEXUS_BLOCKDEV_MAX_DEVICES];
static size_t registered_count;

void blockdev_init(void) {
    registered_count = 0;
    for (size_t i = 0; i < NEXUS_BLOCKDEV_MAX_DEVICES; ++i) {
        devices[i] = (struct nexus_block_device){0};
    }
}

bool blockdev_register(const struct nexus_block_device *device) {
    if (device == NULL || device->name[0] == '\0' ||
        device->block_count == 0 || device->block_size == 0 ||
        device->max_blocks_per_request == 0 ||
        (((device->flags & NEXUS_BLOCKDEV_READ_ONLY) == 0) ==
         ((device->flags & NEXUS_BLOCKDEV_WRITABLE) == 0)) ||
        device->read == NULL ||
        ((device->flags & NEXUS_BLOCKDEV_WRITABLE) != 0 &&
         (device->write == NULL || device->flush == NULL)) ||
        ((device->flags & NEXUS_BLOCKDEV_READ_ONLY) != 0 &&
         (device->write != NULL || device->flush != NULL)) ||
        registered_count >= NEXUS_BLOCKDEV_MAX_DEVICES) {
        return false;
    }

    for (size_t i = 0; i < registered_count; ++i) {
        if (devices[i].context == device->context &&
            devices[i].read == device->read) {
            return false;
        }
    }

    size_t name_length = 0;
    while (name_length < NEXUS_BLOCKDEV_NAME_MAX &&
           device->name[name_length] != '\0') {
        ++name_length;
    }
    if (name_length == 0 || name_length >= NEXUS_BLOCKDEV_NAME_MAX) {
        return false;
    }

    devices[registered_count] = *device;
    ++registered_count;
    return true;
}

size_t blockdev_count(void) {
    return registered_count;
}

const struct nexus_block_device *blockdev_at(size_t index) {
    if (index >= registered_count) return NULL;
    return &devices[index];
}

bool blockdev_read(const struct nexus_block_device *device,
                   uint64_t lba, uint32_t block_count, void *buffer) {
    if (device == NULL || buffer == NULL || block_count == 0 ||
        device->read == NULL ||
        (device->flags & (NEXUS_BLOCKDEV_READ_ONLY |
                          NEXUS_BLOCKDEV_WRITABLE)) == 0 ||
        lba >= device->block_count ||
        (uint64_t)block_count > device->block_count - lba ||
        block_count > device->max_blocks_per_request) {
        return false;
    }

    /* Only registry-owned handles may reach a driver callback. */
    bool registered = false;
    for (size_t i = 0; i < registered_count; ++i) {
        if (&devices[i] == device) {
            registered = true;
            break;
        }
    }
    if (!registered) return false;

    return device->read(device->context, lba, block_count, buffer);
}


bool blockdev_write(const struct nexus_block_device *device,
                    uint64_t lba, uint32_t block_count, const void *buffer) {
    if (device == NULL || buffer == NULL || block_count == 0U ||
        device->write == NULL ||
        (device->flags & NEXUS_BLOCKDEV_WRITABLE) == 0U ||
        (device->flags & NEXUS_BLOCKDEV_READ_ONLY) != 0U ||
        lba >= device->block_count ||
        (uint64_t)block_count > device->block_count - lba ||
        block_count > device->max_blocks_per_request) return false;

    for (size_t i = 0U; i < registered_count; ++i) {
        if (&devices[i] == device)
            return device->write(device->context, lba, block_count, buffer);
    }
    return false;
}

bool blockdev_flush(const struct nexus_block_device *device) {
    if (device == NULL || device->flush == NULL ||
        (device->flags & NEXUS_BLOCKDEV_WRITABLE) == 0U ||
        (device->flags & NEXUS_BLOCKDEV_READ_ONLY) != 0U) return false;

    for (size_t i = 0U; i < registered_count; ++i) {
        if (&devices[i] == device) return device->flush(device->context);
    }
    return false;
}
