#include "blockdev.h"
#include "gpt.h"

#include <stdint.h>

static struct nexus_block_device devices[NEXUS_BLOCKDEV_MAX_DEVICES];
static size_t registered_count;
static const struct nexus_block_device *boot_media_device;
static bool boot_media_detection_complete;
static bool mounted_devices[NEXUS_BLOCKDEV_MAX_DEVICES];
static const struct nexus_block_device *install_session_device;
static char install_session_id[NEXUS_BLOCKDEV_ID_MAX];
static char install_session_model[NEXUS_BLOCKDEV_MODEL_MAX];
static uint64_t install_session_block_count;
static uint32_t install_session_block_size;

void blockdev_init(void) {
    blockdev_install_session_end();
    registered_count = 0;
    boot_media_device = NULL;
    boot_media_detection_complete = false;
    for (size_t i = 0; i < NEXUS_BLOCKDEV_MAX_DEVICES; ++i) {
        devices[i] = (struct nexus_block_device){0};
        mounted_devices[i] = false;
    }
}

bool blockdev_register(const struct nexus_block_device *device) {
    if (device == NULL || device->name[0] == '\0' ||
        device->block_count == 0 || device->block_size == 0 ||
        device->max_blocks_per_request == 0 ||
        device->transport < NEXUS_BLOCKDEV_TRANSPORT_NVME ||
        device->transport > NEXUS_BLOCKDEV_TRANSPORT_NETWORK ||
        (((device->flags & NEXUS_BLOCKDEV_READ_ONLY) == 0) ==
         ((device->flags & NEXUS_BLOCKDEV_WRITABLE) == 0)) ||
        device->read == NULL ||
        ((device->flags & NEXUS_BLOCKDEV_WRITABLE) != 0 &&
         (device->write == NULL || device->flush == NULL)) ||
        ((device->flags & NEXUS_BLOCKDEV_READ_ONLY) != 0 &&
         (device->write != NULL || device->flush != NULL) &&
         (device->flags & NEXUS_BLOCKDEV_INSTALL_CAPABLE) == 0U) ||
        ((device->flags & NEXUS_BLOCKDEV_INSTALL_CAPABLE) != 0U &&
         ((device->flags & NEXUS_BLOCKDEV_READ_ONLY) == 0U ||
          device->write == NULL || device->flush == NULL)) ||
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
    mounted_devices[registered_count] = false;
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


static size_t blockdev_index(const struct nexus_block_device *device) {
    if (device == NULL) return NEXUS_BLOCKDEV_MAX_DEVICES;
    for (size_t i = 0U; i < registered_count; ++i)
        if (&devices[i] == device) return i;
    return NEXUS_BLOCKDEV_MAX_DEVICES;
}

static bool blockdev_set_boot_media(const struct nexus_block_device *device) {
    if (blockdev_index(device) == NEXUS_BLOCKDEV_MAX_DEVICES ||
        boot_media_detection_complete) return false;
    if (boot_media_device != NULL && boot_media_device != device) return false;
    boot_media_device = device;
    return true;
}

static bool blockdev_set_boot_media_detection_complete(void);

bool blockdev_set_boot_media_from_gpt_partition_guid(const uint8_t partition_guid[16]) {
    if (partition_guid == NULL || boot_media_detection_complete ||
        boot_media_device != NULL) return false;
    uint8_t nonzero = 0U;
    for (size_t i = 0U; i < 16U; ++i) nonzero |= partition_guid[i];
    if (nonzero == 0U) return false;

    const struct nexus_block_device *match = NULL;
    size_t match_count = 0U;
    for (size_t d = 0U; d < registered_count; ++d) {
        const struct nexus_block_device *candidate = &devices[d];
        if (!gpt_scan_device(candidate)) continue;
        for (size_t p = 0U; p < gpt_partition_count(); ++p) {
            const struct nexus_gpt_partition *partition = gpt_partition_at(p);
            if (partition == NULL) continue;
            bool equal = true;
            for (size_t b = 0U; b < 16U; ++b) {
                if (partition->unique_guid[b] != partition_guid[b]) {
                    equal = false;
                    break;
                }
            }
            if (equal && ++match_count > 1U) return false;
            if (equal) match = candidate;
        }
    }
    /* Missing, ambiguous, or corrupt metadata must never authorize writes. */
    if (match_count != 1U || match == NULL ||
        !blockdev_set_boot_media(match)) return false;
    if (!blockdev_set_boot_media_detection_complete()) {
        boot_media_device = NULL;
        return false;
    }
    return true;
}

bool blockdev_set_boot_media_from_gpt_disk_guid(const uint8_t disk_guid[16]) {
    if (disk_guid == NULL || boot_media_detection_complete || boot_media_device != NULL) return false;
    uint8_t nonzero = 0U; for (size_t i=0U;i<16U;++i) nonzero |= disk_guid[i];
    if (nonzero == 0U) return false;
    const struct nexus_block_device *match=NULL; size_t matches=0U;
    for(size_t d=0U;d<registered_count;++d) {
        if(!gpt_scan_device(&devices[d])) continue;
        const uint8_t *found=gpt_scanned_disk_guid(); bool equal=found!=NULL;
        for(size_t b=0U;b<16U&&equal;++b) if(found[b]!=disk_guid[b]) equal=false;
        if(equal){match=&devices[d];++matches;} if(matches>1U)return false;
    }
    if(matches!=1U||match==NULL||!blockdev_set_boot_media(match))return false;
    if(!blockdev_set_boot_media_detection_complete()){boot_media_device=NULL;return false;}
    return true;
}
bool blockdev_set_boot_media_from_mbr_disk_id(uint32_t disk_id) {
    if(disk_id==0U||boot_media_detection_complete||boot_media_device!=NULL)return false;
    const struct nexus_block_device *match=NULL; size_t matches=0U; uint8_t sector[4096];
    for(size_t d=0U;d<registered_count;++d){
        const struct nexus_block_device *candidate=&devices[d];
        if(candidate->block_size<512U||candidate->block_size>sizeof(sector)||
           !blockdev_read(candidate,0U,1U,sector))continue;
        if(sector[510]!=0x55U||sector[511]!=0xAAU)continue;
        uint32_t found=(uint32_t)sector[440]|((uint32_t)sector[441]<<8)|
          ((uint32_t)sector[442]<<16)|((uint32_t)sector[443]<<24);
        if(found==disk_id){match=candidate;++matches;} if(matches>1U)return false;
    }
    if(matches!=1U||match==NULL||!blockdev_set_boot_media(match))return false;
    if(!blockdev_set_boot_media_detection_complete()){boot_media_device=NULL;return false;}
    return true;
}
bool blockdev_set_boot_media_external(uint32_t source_kind) {
    if(boot_media_detection_complete||boot_media_device!=NULL)return false;
    uint32_t transport;
    if(source_kind==NEXUS_BOOT_SOURCE_OPTICAL)transport=NEXUS_BLOCKDEV_TRANSPORT_OPTICAL;
    else if(source_kind==NEXUS_BOOT_SOURCE_NETWORK)transport=NEXUS_BLOCKDEV_TRANSPORT_NETWORK;
    else return false;
    for(size_t i=0U;i<registered_count;++i)if(devices[i].transport==transport)return false;
    boot_media_detection_complete=true;
    return true;
}
static bool blockdev_set_boot_media_detection_complete(void) {
    if (boot_media_device == NULL || boot_media_detection_complete) return false;
    boot_media_detection_complete = true;
    return true;
}

bool blockdev_set_mounted(const struct nexus_block_device *device, bool mounted) {
    const size_t index = blockdev_index(device);
    if (index == NEXUS_BLOCKDEV_MAX_DEVICES) return false;
    mounted_devices[index] = mounted;
    if (mounted && install_session_device == device)
        blockdev_install_session_end();
    return true;
}

static bool blockdev_is_registered(const struct nexus_block_device *device) {
    if (device == NULL) return false;
    for (size_t i = 0; i < registered_count; ++i) {
        if (&devices[i] == device) return true;
    }
    return false;
}

static bool blockdev_string_equal(const char *left, size_t left_capacity,
                                  const char *right, size_t right_capacity) {
    if (left == NULL || right == NULL) return false;
    size_t i = 0;
    while (i < left_capacity && i < right_capacity &&
           left[i] != '\0' && right[i] != '\0') {
        if (left[i] != right[i]) return false;
        ++i;
    }
    return i < left_capacity && i < right_capacity &&
           left[i] == '\0' && right[i] == '\0';
}

bool blockdev_identity_is_unique(const struct nexus_block_device *device) {
    if (!blockdev_is_registered(device) ||
        device->stable_id[0] == '\0' ||
        !blockdev_string_equal(device->stable_id, NEXUS_BLOCKDEV_ID_MAX,
                               device->stable_id, NEXUS_BLOCKDEV_ID_MAX)) {
        return false;
    }

    /* A placeholder serial is not safe enough to authorize destructive work. */
    static const char unknown_id[] = "NVME-UNKNOWN";
    if (blockdev_string_equal(device->stable_id, NEXUS_BLOCKDEV_ID_MAX,
                              unknown_id, sizeof(unknown_id))) {
        return false;
    }

    for (size_t i = 0; i < registered_count; ++i) {
        if (&devices[i] == device) continue;
        if (blockdev_string_equal(device->stable_id, NEXUS_BLOCKDEV_ID_MAX,
                                  devices[i].stable_id, NEXUS_BLOCKDEV_ID_MAX)) {
            return false;
        }
    }
    return true;
}

bool blockdev_matches_identity(const struct nexus_block_device *device,
                               const char *stable_id, const char *model,
                               uint64_t block_count, uint32_t block_size) {
    if (!blockdev_identity_is_unique(device) || stable_id == NULL ||
        model == NULL || block_count == 0U || block_size == 0U ||
        device->block_count != block_count || device->block_size != block_size) {
        return false;
    }
    return blockdev_string_equal(device->stable_id, NEXUS_BLOCKDEV_ID_MAX,
                                 stable_id, NEXUS_BLOCKDEV_ID_MAX) &&
           blockdev_string_equal(device->model, NEXUS_BLOCKDEV_MODEL_MAX,
                                 model, NEXUS_BLOCKDEV_MODEL_MAX);
}

bool blockdev_install_write_supported(const struct nexus_block_device *device) {
    if (!blockdev_is_registered(device) ||
        (device->flags & NEXUS_BLOCKDEV_INSTALL_CAPABLE) == 0U ||
        (device->flags & NEXUS_BLOCKDEV_READ_ONLY) == 0U ||
        (device->flags & NEXUS_BLOCKDEV_WRITABLE) != 0U ||
        device->write == NULL || device->flush == NULL) return false;
    return true;
}

static bool blockdev_exact_text(const char *expected, const char *provided,
                                size_t capacity) {
    if (expected == NULL || provided == NULL) return false;
    size_t i = 0U;
    while (i < capacity && expected[i] != '\0' && provided[i] != '\0') {
        if (expected[i] != provided[i]) return false;
        ++i;
    }
    return i < capacity && expected[i] == '\0' && provided[i] == '\0';
}

void blockdev_install_session_end(void) {
    install_session_device = NULL;
    for (size_t i = 0U; i < NEXUS_BLOCKDEV_ID_MAX; ++i)
        install_session_id[i] = '\0';
    for (size_t i = 0U; i < NEXUS_BLOCKDEV_MODEL_MAX; ++i)
        install_session_model[i] = '\0';
    install_session_block_count = 0U;
    install_session_block_size = 0U;
}

bool blockdev_install_session_begin(const struct nexus_block_device *device,
                                    const char *typed_stable_id,
                                    const char *typed_erase_phrase) {
    blockdev_install_session_end();
    const size_t index = blockdev_index(device);
    if (!boot_media_detection_complete ||
        index == NEXUS_BLOCKDEV_MAX_DEVICES || device == boot_media_device ||
        mounted_devices[index] ||
        !blockdev_install_write_supported(device) ||
        !blockdev_identity_is_unique(device) ||
        !blockdev_exact_text(device->stable_id, typed_stable_id,
                             NEXUS_BLOCKDEV_ID_MAX) ||
        !blockdev_exact_text("ERASE", typed_erase_phrase, 8U)) return false;

    install_session_device = device;
    for (size_t i = 0U; i < NEXUS_BLOCKDEV_ID_MAX; ++i)
        install_session_id[i] = device->stable_id[i];
    for (size_t i = 0U; i < NEXUS_BLOCKDEV_MODEL_MAX; ++i)
        install_session_model[i] = device->model[i];
    install_session_block_count = device->block_count;
    install_session_block_size = device->block_size;
    return true;
}

static bool blockdev_install_session_valid(
    const struct nexus_block_device *device) {
    return device != NULL && device == install_session_device &&
        blockdev_matches_identity(device, install_session_id,
                                  install_session_model,
                                  install_session_block_count,
                                  install_session_block_size) &&
        blockdev_install_write_supported(device);
}

bool blockdev_install_write(const struct nexus_block_device *device,
                            uint64_t lba, uint32_t block_count,
                            const void *buffer) {
    if (!blockdev_install_session_valid(device) || buffer == NULL ||
        block_count == 0U || lba >= device->block_count ||
        (uint64_t)block_count > device->block_count - lba ||
        block_count > device->max_blocks_per_request) return false;
    return device->write(device->context, lba, block_count, buffer);
}

bool blockdev_install_flush(const struct nexus_block_device *device) {
    if (!blockdev_install_session_valid(device)) return false;
    return device->flush(device->context);
}
