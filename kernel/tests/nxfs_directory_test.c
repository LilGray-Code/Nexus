#include "blockdev.h"
#include "nxfs.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define TEST_BLOCK_SIZE 512U
#define TEST_BLOCKS_PER_FILE (NEXUS_NXFS_MAX_FILE_BYTES / TEST_BLOCK_SIZE)
#define TEST_PARTITION_BLOCKS (67U + NEXUS_NXFS_MAX_FILES * 2U * TEST_BLOCKS_PER_FILE + 16U)
#define TEST_SPARSE_BLOCKS 256U

struct sparse_block {
    bool used;
    uint64_t lba;
    uint8_t data[TEST_BLOCK_SIZE];
};

struct mock_disk {
    struct sparse_block blocks[TEST_SPARSE_BLOCKS];
};

static struct mock_disk disk;

static uint32_t test_crc32(const uint8_t *data, size_t length) {
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t i = 0U; i < length; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0U; bit < 8U; ++bit)
            crc = (crc >> 1) ^
                ((crc & 1U) != 0U ? 0xEDB88320U : 0U);
    }
    return ~crc;
}

static void test_write_le32(uint8_t *data, uint32_t value) {
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)(value >> 16);
    data[3] = (uint8_t)(value >> 24);
}

/* Convert an empty test volume to the original on-disk 1 MiB geometry. */
static void force_legacy_superblocks(void) {
    for (uint64_t lba = 0U; lba < 2U; ++lba) {
        struct sparse_block *block = find_block(lba, false);
        assert(block != NULL);
        test_write_le32(block->data + 20U, NEXUS_NXFS_LEGACY_FILE_BYTES);
        test_write_le32(block->data + 40U,
            NEXUS_NXFS_LEGACY_FILE_BYTES / TEST_BLOCK_SIZE);
        test_write_le32(block->data + 60U,
                        test_crc32(block->data, 60U));
    }
}

static struct sparse_block *find_block(uint64_t lba, bool create) {
    struct sparse_block *free_slot = NULL;
    for (size_t i = 0U; i < TEST_SPARSE_BLOCKS; ++i) {
        if (disk.blocks[i].used && disk.blocks[i].lba == lba)
            return &disk.blocks[i];
        if (!disk.blocks[i].used && free_slot == NULL)
            free_slot = &disk.blocks[i];
    }
    if (!create || free_slot == NULL) return NULL;
    free_slot->used = true;
    free_slot->lba = lba;
    memset(free_slot->data, 0, sizeof(free_slot->data));
    return free_slot;
}

static bool mock_read(void *context, uint64_t lba, uint32_t count,
                      void *buffer) {
    struct mock_disk *store = context;
    if (store == NULL || buffer == NULL || lba > TEST_PARTITION_BLOCKS ||
        count > TEST_PARTITION_BLOCKS - lba) return false;
    uint8_t *out = buffer;
    for (uint32_t n = 0U; n < count; ++n) {
        struct sparse_block *block = find_block(lba + n, false);
        if (block != NULL) memcpy(out + (size_t)n * TEST_BLOCK_SIZE,
                                  block->data, TEST_BLOCK_SIZE);
        else memset(out + (size_t)n * TEST_BLOCK_SIZE, 0, TEST_BLOCK_SIZE);
    }
    return true;
}

static bool mock_write(void *context, uint64_t lba, uint32_t count,
                       const void *buffer) {
    struct mock_disk *store = context;
    if (store == NULL || buffer == NULL || lba > TEST_PARTITION_BLOCKS ||
        count > TEST_PARTITION_BLOCKS - lba) return false;
    const uint8_t *in = buffer;
    for (uint32_t n = 0U; n < count; ++n) {
        struct sparse_block *block = find_block(lba + n, true);
        if (block == NULL) return false;
        memcpy(block->data, in + (size_t)n * TEST_BLOCK_SIZE,
               TEST_BLOCK_SIZE);
    }
    return true;
}

static bool mock_flush(void *context) {
    return context == &disk;
}

bool blockdev_read(const struct nexus_block_device *device, uint64_t lba,
                   uint32_t count, void *buffer) {
    return device != NULL && device->read != NULL &&
           device->read(device->context, lba, count, buffer);
}

bool blockdev_write(const struct nexus_block_device *device, uint64_t lba,
                    uint32_t count, const void *buffer) {
    return device != NULL && device->write != NULL &&
           device->write(device->context, lba, count, buffer);
}

bool blockdev_flush(const struct nexus_block_device *device) {
    return device != NULL && device->flush != NULL &&
           device->flush(device->context);
}

bool blockdev_nexus_data_write_enable(const struct nexus_block_device *device,
                                      uint64_t first, uint64_t last) {
    return device != NULL && first <= last;
}
void blockdev_nexus_data_write_disable(const struct nexus_block_device *device) {
    (void)device;
}
bool blockdev_nexus_data_write_is_enabled(const struct nexus_block_device *device,
                                          uint64_t first, uint64_t last) {
    (void)device; (void)first; (void)last;
    return false;
}
bool blockdev_install_write_supported(const struct nexus_block_device *device) {
    (void)device;
    return false;
}
bool blockdev_install_session_begin(const struct nexus_block_device *device,
                                    const char *id, const char *phrase) {
    (void)device; (void)id; (void)phrase;
    return false;
}
bool blockdev_install_write(const struct nexus_block_device *device,
                            uint64_t lba, uint32_t count,
                            const void *buffer) {
    (void)device; (void)lba; (void)count; (void)buffer;
    return false;
}
bool blockdev_install_flush(const struct nexus_block_device *device) {
    (void)device;
    return false;
}
void blockdev_install_session_end(void) {}

int main(void) {
    struct nexus_block_device device = {0};
    memcpy(device.name, "NXFS-TEST", sizeof("NXFS-TEST"));
    device.block_count = TEST_PARTITION_BLOCKS;
    device.block_size = TEST_BLOCK_SIZE;
    device.flags = NEXUS_BLOCKDEV_WRITABLE;
    device.context = &disk;
    device.read = mock_read;
    device.write = mock_write;
    device.flush = mock_flush;

    struct nexus_gpt_partition partition = {0};
    partition.first_lba = 0U;
    partition.last_lba = TEST_PARTITION_BLOCKS - 1U;
    const uint8_t guid[16] = {
        0x46U,0x53U,0x58U,0x4EU,0x01U,0x00U,0x58U,0x4EU,
        0x9AU,0x11U,0x4EU,0x58U,0x55U,0x53U,0x46U,0x53U
    };
    memcpy(partition.type_guid, guid, sizeof(guid));

    assert(nxfs_format(&device, &partition));
    assert(nxfs_is_mounted() && nxfs_is_writable());
    assert(nxfs_get_max_file_bytes() == NEXUS_NXFS_MAX_FILE_BYTES);

    /* Existing 1 MiB filesystems must keep their original slot geometry. */
    force_legacy_superblocks();
    assert(nxfs_mount(&device, &partition));
    assert(nxfs_get_max_file_bytes() == NEXUS_NXFS_LEGACY_FILE_BYTES);
    assert(!nxfs_write_file("TOO.BIG", NULL,
                           (size_t)NEXUS_NXFS_LEGACY_FILE_BYTES + 1U));

    /* A Data partition too small for 4 MiB slots still formats as legacy. */
    struct nexus_gpt_partition smaller_partition = partition;
    smaller_partition.last_lba =
        67U + (uint64_t)NEXUS_NXFS_MAX_FILES * 2U *
        (NEXUS_NXFS_LEGACY_FILE_BYTES / TEST_BLOCK_SIZE) + 15U;
    assert(nxfs_format(&device, &smaller_partition));
    assert(nxfs_get_max_file_bytes() == NEXUS_NXFS_LEGACY_FILE_BYTES);

    /* Restore a large test partition and verify the new 4 MiB format. */
    assert(nxfs_format(&device, &partition));
    assert(nxfs_get_max_file_bytes() == NEXUS_NXFS_MAX_FILE_BYTES);
    assert(!nxfs_create_directory("MISSING/CHILD"));
    assert(!nxfs_create_directory("DOCUMENTS//INVALID"));
    assert(!nxfs_create_directory("DOCUMENTS/"));
    assert(nxfs_create_directory("Documents"));
    assert(nxfs_create_directory("Documents/Projects"));
    assert(nxfs_is_directory("documents"));
    assert(nxfs_is_directory("DOCUMENTS/PROJECTS"));
    assert(!nxfs_is_directory("DOCUMENTS/PROJECTS/NOTE.TXT"));

    static const char payload[] = "Folder data survives remount";
    assert(nxfs_write_file("Documents/Projects/NOTE.TXT",
                           payload, sizeof(payload) - 1U));
    assert(!nxfs_write_file("Documents/Projects", payload,
                            sizeof(payload) - 1U));
    assert(!nxfs_remove_file("Documents"));
    assert(!nxfs_remove_directory("Documents"));
    assert(!nxfs_remove_directory("Documents/Projects/NOTE.TXT"));

    nxfs_unmount();
    assert(nxfs_mount(&device, &partition));
    assert(nxfs_is_directory("DOCUMENTS"));
    assert(nxfs_is_directory("DOCUMENTS/PROJECTS"));
    const struct nexus_nxfs_entry *file = NULL;
    for (size_t i = 0U; i < nxfs_entry_count(); ++i) {
        const struct nexus_nxfs_entry *entry = nxfs_entry_at(i);
        if (entry != NULL && strcmp(entry->name,
                                    "DOCUMENTS/PROJECTS/NOTE.TXT") == 0)
            file = entry;
    }
    assert(file != NULL && !file->directory);
    uint8_t readback[sizeof(payload)];
    assert(nxfs_read_file(file, 0U, sizeof(payload) - 1U, readback));
    assert(memcmp(readback, payload, sizeof(payload) - 1U) == 0);
    assert(nxfs_remove_file("DOCUMENTS/PROJECTS/NOTE.TXT"));
    assert(nxfs_remove_directory("DOCUMENTS/PROJECTS"));
    assert(nxfs_remove_directory("DOCUMENTS"));
    assert(!nxfs_is_directory("DOCUMENTS"));
    return 0;
}
