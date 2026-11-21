#ifndef NEXUS_BLOCKDEV_H
#define NEXUS_BLOCKDEV_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NEXUS_BLOCKDEV_MAX_DEVICES 8U
#define NEXUS_BLOCKDEV_NAME_MAX 16U
#define NEXUS_BLOCKDEV_ID_MAX 64U
#define NEXUS_BLOCKDEV_MODEL_MAX 64U
#define NEXUS_BLOCKDEV_READ_ONLY 0x00000001U
#define NEXUS_BLOCKDEV_WRITABLE 0x00000002U
/* Driver can issue writes, but ordinary filesystem writes remain disabled. */
#define NEXUS_BLOCKDEV_INSTALL_CAPABLE 0x00000004U

typedef bool (*nexus_blockdev_read_fn)(void *context,
                                       uint64_t lba,
                                       uint32_t block_count,
                                       void *buffer);
typedef bool (*nexus_blockdev_write_fn)(void *context,
                                        uint64_t lba,
                                        uint32_t block_count,
                                        const void *buffer);
typedef bool (*nexus_blockdev_flush_fn)(void *context);

struct nexus_block_device {
    char name[NEXUS_BLOCKDEV_NAME_MAX];
    /* Stable hardware identity and model are shown during destructive setup. */
    char stable_id[NEXUS_BLOCKDEV_ID_MAX];
    char model[NEXUS_BLOCKDEV_MODEL_MAX];
    uint64_t block_count;
    uint32_t block_size;
    uint32_t max_blocks_per_request;
    uint32_t flags;
    void *context;
    nexus_blockdev_read_fn read;
    nexus_blockdev_write_fn write;
    nexus_blockdev_flush_fn flush;
};

void blockdev_init(void);
bool blockdev_register(const struct nexus_block_device *device);
size_t blockdev_count(void);
const struct nexus_block_device *blockdev_at(size_t index);
bool blockdev_read(const struct nexus_block_device *device,
                   uint64_t lba, uint32_t block_count, void *buffer);
bool blockdev_write(const struct nexus_block_device *device,
                    uint64_t lba, uint32_t block_count, const void *buffer);
bool blockdev_flush(const struct nexus_block_device *device);
/* Reports hardware support only; this does not authorize or perform writes. */
bool blockdev_install_write_supported(const struct nexus_block_device *device);
/* Installer gate: only a registered, non-placeholder, unique identity is eligible. */
bool blockdev_identity_is_unique(const struct nexus_block_device *device);
/* Revalidate the exact identity and geometry captured when the target was chosen. */
bool blockdev_matches_identity(const struct nexus_block_device *device,
                                const char *stable_id, const char *model,
                                uint64_t block_count, uint32_t block_size);

#endif
