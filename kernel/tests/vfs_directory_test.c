#include "blockdev.h"
#include "fat32.h"
#include "nxfs.h"
#include "vfs.h"
#include "boot_pack.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define TEST_BLOCK_SIZE 512U
#define TEST_BLOCKS_PER_FILE (NEXUS_NXFS_MAX_FILE_BYTES / TEST_BLOCK_SIZE)
#define TEST_PARTITION_BLOCKS \
    (67U + NEXUS_NXFS_MAX_FILES * 2U * TEST_BLOCKS_PER_FILE + 16U)
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
        if (block != NULL)
            memcpy(out + (size_t)n * TEST_BLOCK_SIZE,
                   block->data, TEST_BLOCK_SIZE);
        else
            memset(out + (size_t)n * TEST_BLOCK_SIZE, 0, TEST_BLOCK_SIZE);
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
bool blockdev_set_mounted(const struct nexus_block_device *device,
                          bool mounted) {
    (void)mounted;
    return device != NULL;
}
bool blockdev_nexus_data_write_enable(const struct nexus_block_device *device,
                                      uint64_t first, uint64_t last) {
    return device != NULL && first <= last;
}
void blockdev_nexus_data_write_disable(const struct nexus_block_device *device) {
    (void)device;
}
bool blockdev_nexus_data_write_is_enabled(
    const struct nexus_block_device *device, uint64_t first, uint64_t last) {
    (void)device; (void)first; (void)last;
    return false;
}
bool blockdev_install_write_supported(
    const struct nexus_block_device *device) {
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

/* A deterministic read-only FAT32 volume lets the same VFS test prove that
 * directory navigation does not accidentally grant write access to FAT32. */
static struct nexus_fat32_entry fat_entries[3];
static size_t fat_entry_count;

static void fat_set_entry(size_t slot, const char *name, uint8_t attributes,
                          uint32_t cluster, uint32_t size) {
    assert(slot < 3U);
    memset(&fat_entries[slot], 0, sizeof(fat_entries[slot]));
    size_t n = 0U;
    while (name[n] != '\0' && n < sizeof(fat_entries[slot].name) - 1U) {
        fat_entries[slot].name[n] = name[n];
        ++n;
    }
    fat_entries[slot].attributes = attributes;
    fat_entries[slot].first_cluster = cluster;
    fat_entries[slot].size = size;
}

static void fat_set_root(void) {
    fat_entry_count = 2U;
    fat_set_entry(0U, "BOOT", 0x10U, 3U, 0U);
    fat_set_entry(1U, "ROOT.TXT", 0x20U, 8U, 12U);
}

static bool fat32_mock_read_directory(uint32_t cluster) {
    if (cluster == 2U) {
        fat_set_root();
        return true;
    }
    if (cluster == 3U) {
        fat_entry_count = 2U;
        fat_set_entry(0U, "MANUALS", 0x10U, 4U, 0U);
        fat_set_entry(1U, "INFO.TXT", 0x20U, 6U, 14U);
        return true;
    }
    if (cluster == 4U) {
        fat_entry_count = 1U;
        fat_set_entry(0U, "GUIDE.TXT", 0x20U, 7U, 16U);
        return true;
    }
    return false;
}

bool fat32_mount(const struct nexus_block_device *device,
                 const struct nexus_gpt_partition *partition) {
    if (device == NULL || partition == NULL) return false;
    fat_set_root();
    return true;
}
uint32_t fat32_root_cluster(void) {
    return 2U;
}
bool fat32_read_directory(uint32_t first_cluster) {
    return fat32_mock_read_directory(first_cluster);
}
size_t fat32_root_entry_count(void) {
    return fat_entry_count;
}
const struct nexus_fat32_entry *fat32_root_entry_at(size_t index) {
    return index < fat_entry_count ? &fat_entries[index] : NULL;
}
bool fat32_read_file(const struct nexus_fat32_entry *entry,
                     uint32_t offset, uint32_t length, void *buffer) {
    if (entry == NULL || (length != 0U && buffer == NULL) ||
        offset > entry->size || length > entry->size - offset)
        return false;
    const char *payload = NULL;
    if (entry->first_cluster == 6U) payload = "READ ONLY INFO";
    if (entry->first_cluster == 7U) payload = "Nested FAT guide";
    if (entry->first_cluster == 8U) payload = "ROOT CONTENT";
    if (payload == NULL || strlen(payload) != entry->size) return false;
    memcpy(buffer, payload + offset, length);
    return true;
}

static bool find_entry(const char *name, struct nexus_vfs_dirent *found) {
    for (size_t i = 0U; i < vfs_root_entry_count(); ++i) {
        struct nexus_vfs_dirent entry;
        if (vfs_root_entry(i, &entry) &&
            strcmp(entry.name, name) == 0) {
            if (found != NULL) *found = entry;
            return true;
        }
    }
    return false;
}

static void read_current_file(const char *name, const char *expected) {
    struct nexus_vfs_file file;
    assert(vfs_open_readonly(name, &file));
    uint32_t size = 0U;
    assert(vfs_file_size(&file, &size));
    assert(size == strlen(expected));
    char readback[64] = {0};
    size_t bytes = 0U;
    assert(vfs_read(&file, readback, size, &bytes));
    assert(bytes == size);
    assert(memcmp(readback, expected, size) == 0);
    vfs_close(&file);
}

static void read_current_bytes(const char *name, const uint8_t *expected,
                               size_t expected_size) {
    struct nexus_vfs_file file;
    assert(vfs_open_readonly(name, &file));
    uint32_t size = 0U;
    assert(vfs_file_size(&file, &size));
    assert(size == expected_size);
    uint8_t buffer[NEXUS_VFS_READ_CHUNK];
    size_t offset = 0U;
    while (offset < expected_size) {
        size_t bytes = 0U;
        const size_t requested = expected_size - offset <
            sizeof(buffer) ? expected_size - offset : sizeof(buffer);
        assert(vfs_read(&file, buffer, requested, &bytes));
        assert(bytes == requested);
        assert(memcmp(buffer, expected + offset, bytes) == 0);
        offset += bytes;
    }
    vfs_close(&file);
}


static uint32_t pack_test_crc32(const uint8_t *data, size_t length) {
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t i = 0U; i < length; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0U; bit < 8U; ++bit)
            crc = (crc >> 1) ^ ((crc & 1U) ? 0xEDB88320U : 0U);
    }
    return ~crc;
}
static void pack_test_write_le32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}
static void make_boot_pack(uint8_t image[128], uint32_t *image_size) {
    static const uint8_t payload[] = "immutable bundled system file";
    const uint32_t header = 32U + 48U;
    const uint32_t total = header + (uint32_t)(sizeof(payload) - 1U);
    memset(image, 0, 128U);
    const uint8_t magic[8] = {'N','X','S','P','A','C','K','1'};
    memcpy(image, magic, sizeof(magic));
    pack_test_write_le32(image + 8U, 1U);
    pack_test_write_le32(image + 12U, header);
    pack_test_write_le32(image + 16U, total);
    pack_test_write_le32(image + 20U, 1U);
    uint8_t *record = image + 32U;
    memcpy(record, "BUNDLED.TXT", sizeof("BUNDLED.TXT") - 1U);
    pack_test_write_le32(record + 32U, header);
    pack_test_write_le32(record + 36U, (uint32_t)(sizeof(payload) - 1U));
    pack_test_write_le32(record + 40U,
                         pack_test_crc32(payload, sizeof(payload) - 1U));
    pack_test_write_le32(record + 44U, 0U);
    pack_test_write_le32(image + 24U, pack_test_crc32(record, 48U));
    memcpy(image + header, payload, sizeof(payload) - 1U);
    pack_test_write_le32(image + 28U,
                         pack_test_crc32(payload, sizeof(payload) - 1U));
    *image_size = total;
}

int main(void) {
    struct nexus_block_device device = {0};
    memcpy(device.name, "VFS-TEST", sizeof("VFS-TEST"));
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
    const uint8_t nxfs_guid[16] = {
        0x46U,0x53U,0x58U,0x4EU,0x01U,0x00U,0x58U,0x4EU,
        0x9AU,0x11U,0x4EU,0x58U,0x55U,0x53U,0x46U,0x53U
    };
    memcpy(partition.type_guid, nxfs_guid, sizeof(nxfs_guid));

    assert(nxfs_format(&device, &partition));
    uint8_t system_pack[128];
    uint32_t system_pack_size = 0U;
    make_boot_pack(system_pack, &system_pack_size);
    assert(nexus_boot_pack_init(system_pack, system_pack_size));
    assert(vfs_mount_root_nxfs(&device, &partition));
    assert(vfs_current_directory_writable());
    assert(vfs_root_entry_count() == 1U);
    struct nexus_vfs_dirent bundled_entry;
    assert(find_entry("BUNDLED.TXT", &bundled_entry));
    assert(bundled_entry.type == NEXUS_VFS_FILE);
    assert(!vfs_file_writable("BUNDLED.TXT"));
    read_current_file("BUNDLED.TXT", "immutable bundled system file");
    assert(!vfs_remove_file("BUNDLED.TXT"));
    assert(vfs_create_directory("DOCUMENTS"));
    assert(find_entry("DOCUMENTS", NULL));
    assert(vfs_enter_directory("DOCUMENTS"));
    assert(!find_entry("BUNDLED.TXT", NULL));
    struct nexus_vfs_file hidden_system_file;
    assert(!vfs_open_readonly("BUNDLED.TXT", &hidden_system_file));

    char path[96];
    assert(vfs_current_path(path, sizeof(path)));
    assert(strcmp(path, "/DOCUMENTS") == 0);
    assert(vfs_create_directory("PROJECTS"));
    assert(vfs_enter_directory("PROJECTS"));
    assert(vfs_current_path(path, sizeof(path)));
    assert(strcmp(path, "/DOCUMENTS/PROJECTS") == 0);
    assert(vfs_change_directory("/DOCUMENTS"));
    assert(vfs_current_path(path, sizeof(path)));
    assert(strcmp(path, "/DOCUMENTS") == 0);
    assert(vfs_change_directory("/DOCUMENTS/PROJECTS"));
    assert(vfs_current_path(path, sizeof(path)));
    assert(strcmp(path, "/DOCUMENTS/PROJECTS") == 0);
    /* A failed path change is transactional and leaves the working directory. */
    assert(!vfs_change_directory("/DOCUMENTS/MISSING"));
    assert(vfs_current_path(path, sizeof(path)));
    assert(strcmp(path, "/DOCUMENTS/PROJECTS") == 0);

    /* Root-scoped preferences never follow the current nested directory. */
    static const uint8_t preferences_a[] = {
        'N', 'X', 'P', '1', 1U, 3U, 0xA7U ^ 'N' ^ 'X' ^ 'P' ^ '1' ^ 1U ^ 3U
    };
    assert(vfs_write_root_file("NEXUS.CFG", preferences_a,
                               sizeof(preferences_a)));
    char root_config[32] = {0};
    struct nexus_vfs_file root_file;
    assert(vfs_open_root_readonly("NEXUS.CFG", &root_file));
    size_t root_bytes = 0U;
    assert(vfs_read(&root_file, root_config, sizeof(preferences_a), &root_bytes));
    assert(root_bytes == sizeof(preferences_a));
    assert(memcmp(root_config, preferences_a, sizeof(preferences_a)) == 0);
    vfs_close(&root_file);
    assert(vfs_current_path(path, sizeof(path)));
    assert(strcmp(path, "/DOCUMENTS/PROJECTS") == 0);

    static const char payload[] = "Persistent nested VFS file";
    assert(vfs_write_file("NOTE.TXT", payload, sizeof(payload) - 1U));
    struct nexus_vfs_dirent note;
    assert(vfs_root_entry_count() == 1U);
    assert(vfs_root_entry(0U, &note));
    assert(note.type == NEXUS_VFS_FILE);
    assert(strcmp(note.name, "NOTE.TXT") == 0);
    assert(vfs_file_writable("NOTE.TXT"));
    read_current_file("NOTE.TXT", payload);
    assert(!vfs_remove_directory("PROJECTS"));

    assert(vfs_copy_file("NOTE.TXT", "DOCUMENTS/PROJECTS/COPY.TXT"));
    assert(vfs_root_entry_count() == 2U);
    read_current_file("COPY.TXT", payload);
    assert(vfs_copy_file_absolute("/DOCUMENTS/PROJECTS/NOTE.TXT",
                                  "/DOCUMENTS/PROJECTS/ABSOLUTE.TXT"));
    read_current_file("ABSOLUTE.TXT", payload);
    assert(vfs_move_file_absolute("/DOCUMENTS/PROJECTS/ABSOLUTE.TXT",
                                  "/DOCUMENTS/PROJECTS/RENAMED.TXT"));
    read_current_file("RENAMED.TXT", payload);
    assert(!vfs_copy_file("NOTE.TXT", "DOCUMENTS/PROJECTS/COPY.TXT"));
    assert(!vfs_move_file("COPY.TXT", "DOCUMENTS/PROJECTS/NOTE.TXT"));
    assert(vfs_root_entry_count() == 2U);
    assert(vfs_move_file("COPY.TXT", "DOCUMENTS/MOVED.TXT"));
    struct nexus_vfs_file copy_probe;
    assert(!vfs_open_readonly("COPY.TXT", &copy_probe));

    assert(vfs_parent_directory());
    assert(vfs_current_path(path, sizeof(path)));
    assert(strcmp(path, "/DOCUMENTS") == 0);
    read_current_file("MOVED.TXT", payload);
    assert(vfs_rename_file("MOVED.TXT", "ARCHIVE.TXT"));
    struct nexus_vfs_file moved_probe;
    assert(!vfs_open_readonly("MOVED.TXT", &moved_probe));
    read_current_file("ARCHIVE.TXT", payload);

    static uint8_t large_payload[8193];
    for (size_t i = 0U; i < sizeof(large_payload); ++i)
        large_payload[i] = (uint8_t)((i * 37U + 11U) & 0xFFU);
    assert(vfs_write_file("BIG.BIN", large_payload, sizeof(large_payload)));
    assert(vfs_copy_file("BIG.BIN", "BIGCOPY.BIN"));
    read_current_bytes("BIGCOPY.BIN", large_payload, sizeof(large_payload));
    assert(!vfs_move_file("BIGCOPY.BIN", "BIG.BIN"));

    assert(!vfs_remove_directory("PROJECTS"));
    read_current_file("ARCHIVE.TXT", payload);
    read_current_bytes("BIGCOPY.BIN", large_payload, sizeof(large_payload));
    assert(vfs_parent_directory());
    assert(!vfs_remove_directory("DOCUMENTS"));

    /* Reboot-equivalent remount: VFS path resets but nested metadata persists. */
    nxfs_unmount();
    assert(vfs_mount_root_nxfs(&device, &partition));
    assert(find_entry("BUNDLED.TXT", &bundled_entry));
    read_current_file("BUNDLED.TXT", "immutable bundled system file");
    assert(vfs_open_root_readonly("NEXUS.CFG", &root_file));
    memset(root_config, 0, sizeof(root_config));
    root_bytes = 0U;
    assert(vfs_read(&root_file, root_config, sizeof(preferences_a), &root_bytes));
    assert(root_bytes == sizeof(preferences_a));
    assert(memcmp(root_config, preferences_a, sizeof(preferences_a)) == 0);
    vfs_close(&root_file);
    assert(find_entry("NEXUS.CFG", &note));
    assert(note.type == NEXUS_VFS_FILE);
    assert(find_entry("DOCUMENTS", &note));
    assert(note.type == NEXUS_VFS_DIRECTORY);
    assert(vfs_enter_directory("DOCUMENTS"));
    read_current_file("ARCHIVE.TXT", payload);
    read_current_bytes("BIGCOPY.BIN", large_payload, sizeof(large_payload));
    assert(vfs_remove_file("ARCHIVE.TXT"));
    assert(vfs_remove_file("BIG.BIN"));
    assert(vfs_remove_file("BIGCOPY.BIN"));
    assert(vfs_enter_directory("PROJECTS"));
    read_current_file("NOTE.TXT", payload);
    assert(vfs_remove_file("NOTE.TXT"));
    assert(vfs_parent_directory());
    assert(vfs_remove_directory("PROJECTS"));
    assert(vfs_parent_directory());
    assert(vfs_remove_directory("DOCUMENTS"));
    assert(!find_entry("DOCUMENTS", NULL));

    /* Combine NXFS user data with a mounted read-only FAT32 volume. */
    struct nexus_gpt_partition fat_partition = partition;
    assert(vfs_mount_root_fat32(&device, &fat_partition));
    assert(!vfs_file_writable("ROOT.TXT"));
    assert(find_entry("BOOT", &note));
    assert(note.type == NEXUS_VFS_DIRECTORY && note.first_cluster == 3U);
    assert(vfs_enter_directory("BOOT"));
    assert(vfs_current_path(path, sizeof(path)));
    assert(strcmp(path, "/BOOT") == 0);
    assert(!vfs_current_directory_writable());
    assert(!vfs_write_file("NEW.TXT", payload, sizeof(payload) - 1U));
    assert(!vfs_create_directory("UNSAFE"));
    assert(!vfs_remove_file("INFO.TXT"));
    assert(!vfs_copy_file("INFO.TXT", "BOOT/COPY.TXT"));
    assert(!vfs_move_file("INFO.TXT", "BOOT/MOVED.TXT"));
    read_current_file("INFO.TXT", "READ ONLY INFO");

    assert(vfs_enter_directory("MANUALS"));
    assert(vfs_current_path(path, sizeof(path)));
    assert(strcmp(path, "/BOOT/MANUALS") == 0);
    read_current_file("GUIDE.TXT", "Nested FAT guide");
    /* Root reads remain available while browsing a nested read-only FAT folder. */
    assert(vfs_open_root_readonly("ROOT.TXT", &root_file));
    memset(root_config, 0, sizeof(root_config));
    root_bytes = 0U;
    assert(vfs_read(&root_file, root_config, 12U, &root_bytes));
    assert(root_bytes == 12U);
    assert(memcmp(root_config, "ROOT CONTENT", 12U) == 0);
    vfs_close(&root_file);
    assert(vfs_current_path(path, sizeof(path)));
    assert(strcmp(path, "/BOOT/MANUALS") == 0);
    assert(vfs_write_root_file("NEXUS.CFG", preferences_a,
                               sizeof(preferences_a)));
    assert(vfs_open_root_readonly("NEXUS.CFG", &root_file));
    memset(root_config, 0, sizeof(root_config));
    root_bytes = 0U;
    assert(vfs_read(&root_file, root_config, sizeof(preferences_a), &root_bytes));
    assert(root_bytes == sizeof(preferences_a));
    assert(memcmp(root_config, preferences_a, sizeof(preferences_a)) == 0);
    vfs_close(&root_file);
    assert(vfs_parent_directory());
    assert(vfs_parent_directory());
    assert(vfs_current_path(path, sizeof(path)));
    assert(strcmp(path, "/") == 0);
    assert(!vfs_remove_directory("BOOT"));
    return 0;
}
