#include "blockdev.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static unsigned int write_calls;
static unsigned int flush_calls;

static bool test_read(void *context, uint64_t lba, uint32_t count, void *buffer) {
    (void)context; (void)lba; (void)count; (void)buffer;
    return true;
}

static bool test_read_second(void *context, uint64_t lba, uint32_t count,
                              void *buffer) {
    (void)context; (void)lba; (void)count; (void)buffer;
    return true;
}

static bool test_write(void *context, uint64_t lba, uint32_t count,
                       const void *buffer) {
    (void)context; (void)lba; (void)count; (void)buffer;
    ++write_calls;
    return true;
}

static bool test_flush(void *context) {
    (void)context;
    ++flush_calls;
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
    duplicate.read = test_read_second;
    assert(blockdev_register(&duplicate));
    assert(!blockdev_identity_is_unique(registered));
    assert(!blockdev_matches_identity(registered, "NVME-SERIAL-001",
                                      "Nexus test disk", 1024U * 1024U, 512U));

    blockdev_init();
    struct nexus_block_device placeholder = make_device("disk0", "NVME-UNKNOWN");
    assert(blockdev_register(&placeholder));
    assert(!blockdev_identity_is_unique(blockdev_at(0U)));

    blockdev_init();
    struct nexus_block_device install_disk =
        make_device("disk0", "NVME-SERIAL-003");
    install_disk.flags = NEXUS_BLOCKDEV_READ_ONLY |
                         NEXUS_BLOCKDEV_INSTALL_CAPABLE;
    install_disk.write = test_write;
    install_disk.flush = test_flush;
    assert(blockdev_register(&install_disk));
    const struct nexus_block_device *install_registered = blockdev_at(0U);
    assert(install_registered != NULL);
    assert(blockdev_install_write_supported(install_registered));
    /* Installer-capable is not the same as generally writable. */
    uint8_t sector[512] = {0};
    assert(!blockdev_write(install_registered, 0U, 1U, sector));
    assert(!blockdev_flush(install_registered));
    assert(!blockdev_install_write(install_registered, 0U, 1U, sector));
    /* Boot provenance is unknown by default: fail closed. */
    assert(!blockdev_install_session_begin(install_registered,
        "NVME-SERIAL-003", "ERASE"));
    assert(!blockdev_set_boot_media_detection_complete());
    assert(blockdev_set_boot_media(install_registered));
    assert(blockdev_set_boot_media_detection_complete());
    assert(!blockdev_install_session_begin(install_registered,
        "NVME-SERIAL-003", "ERASE"));
    assert(!blockdev_set_boot_media_detection_complete());
    blockdev_init();
    install_disk = make_device("disk0", "NVME-SERIAL-003");
    install_disk.flags = NEXUS_BLOCKDEV_READ_ONLY |
                         NEXUS_BLOCKDEV_INSTALL_CAPABLE;
    install_disk.write = test_write;
    install_disk.flush = test_flush;
    assert(blockdev_register(&install_disk));
    install_registered = blockdev_at(0U);
    struct nexus_block_device boot_disk = make_device("boot", "NVME-BOOT-001");
    boot_disk.read = test_read_second;
    assert(blockdev_register(&boot_disk));
    const struct nexus_block_device *boot_registered = blockdev_at(1U);
    assert(blockdev_set_boot_media(boot_registered));
    assert(blockdev_set_boot_media_detection_complete());
    assert(!blockdev_install_session_begin(boot_registered,
        "NVME-BOOT-001", "ERASE"));
    assert(blockdev_set_mounted(install_registered, true));
    assert(!blockdev_install_session_begin(install_registered,
        "NVME-SERIAL-003", "ERASE"));
    assert(blockdev_set_mounted(install_registered, false));
    assert(!blockdev_install_session_begin(install_registered,
        "WRONG-SERIAL", "ERASE"));
    assert(!blockdev_install_session_begin(install_registered,
        "NVME-SERIAL-003", "NO"));
    assert(blockdev_install_session_begin(install_registered,
        "NVME-SERIAL-003", "ERASE"));
    assert(blockdev_install_write(install_registered, 10U, 1U, sector));
    assert(blockdev_install_flush(install_registered));
    /* A volume becoming mounted revokes an already-open destructive session. */
    assert(blockdev_set_mounted(install_registered, true));
    assert(!blockdev_install_write(install_registered, 10U, 1U, sector));
    assert(!blockdev_install_flush(install_registered));
    assert(blockdev_set_mounted(install_registered, false));
    assert(blockdev_install_session_begin(install_registered,
        "NVME-SERIAL-003", "ERASE"));
    /* Reject requests that cross device bounds or exceed the DMA request cap. */
    assert(!blockdev_install_write(install_registered,
        install_registered->block_count - 1U, 2U, sector));
    assert(!blockdev_install_write(install_registered, 10U, 9U, sector));
    assert(!blockdev_install_write(install_registered,
        install_registered->block_count, 1U, sector));
    assert(write_calls == 1U && flush_calls == 1U);
    blockdev_install_session_end();
    assert(!blockdev_install_write(install_registered, 10U, 1U, sector));
    assert(write_calls == 1U);
    return 0;
}
