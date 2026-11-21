#include "blockdev.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static bool test_read(void *context, uint64_t lba, uint32_t count, void *buffer) {
    (void)context; (void)lba; (void)count; (void)buffer;
    return true;
}

static struct nexus_block_device make_device(const char *name,
                                              const char *stable_id) {
    struct nexus_block_device device = {0};
    (void)strncpy(device.name, name, sizeof(device.name) - 1U);
    (void)strncpy(device.stable_id, stable_id, sizeof(device.stable_id) - 1U);
    (void)strncpy(device.model, "Nexus test disk", sizeof(device.model) - 1U);
    device.block_count = 1024U * 1024U;
    device.block_size = 512U;
    device.max_blocks_per_request = 8U;
    device.flags = NEXUS_BLOCKDEV_READ_ONLY;
    device.read = test_read;
    return device;
}

int main(void) {
    blockdev_init();
    struct nexus_block_device first = make_device("disk0", "NVME-SERIAL-001");
    assert(blockdev_register(&first));
    const struct nexus_block_device *registered = blockdev_at(0U);
    assert(registered != NULL);
    assert(blockdev_identity_is_unique(registered));
    assert(blockdev_matches_identity(registered, "NVME-SERIAL-001",
                                     "Nexus test disk", 1024U * 1024U, 512U));
    assert(!blockdev_matches_identity(registered, "NVME-SERIAL-002",
                                      "Nexus test disk", 1024U * 1024U, 512U));
    assert(!blockdev_matches_identity(registered, "NVME-SERIAL-001",
                                      "Nexus test disk", 1024U * 1024U, 4096U));

    struct nexus_block_device duplicate =
        make_device("disk1", "NVME-SERIAL-001");
    assert(blockdev_register(&duplicate));
    assert(!blockdev_identity_is_unique(registered));
    assert(!blockdev_matches_identity(registered, "NVME-SERIAL-001",
                                      "Nexus test disk", 1024U * 1024U, 512U));

    blockdev_init();
    struct nexus_block_device placeholder = make_device("disk0", "NVME-UNKNOWN");
    assert(blockdev_register(&placeholder));
    assert(!blockdev_identity_is_unique(blockdev_at(0U)));
    return 0;
}
