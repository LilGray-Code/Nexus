#include "usb_msc.h"

#include <stddef.h>
#include <stdint.h>

#define USB_MSC_CBW_BYTES 31U
#define USB_MSC_CSW_BYTES 13U
#define USB_MSC_CBW_SIGNATURE 0x43425355U
#define USB_MSC_CSW_SIGNATURE 0x53425355U

#define SCSI_TEST_UNIT_READY 0x00U
#define SCSI_REQUEST_SENSE 0x03U
#define SCSI_INQUIRY 0x12U
#define SCSI_READ_CAPACITY_10 0x25U
#define SCSI_READ_10 0x28U
#define SCSI_WRITE_10 0x2AU
#define SCSI_SYNCHRONIZE_CACHE_10 0x35U
#define SCSI_SERVICE_ACTION_IN_16 0x9EU
#define SCSI_READ_16 0x88U
#define SCSI_WRITE_16 0x8AU
#define SCSI_READ_CAPACITY_16 0x10U

enum scsi_data_direction {
    SCSI_DATA_NONE = 0,
    SCSI_DATA_IN,
    SCSI_DATA_OUT
};

static void zero_bytes(void *buffer, size_t length) {
    uint8_t *bytes = (uint8_t *)buffer;
    for (size_t i = 0U; i < length; ++i) bytes[i] = 0U;
}

static void copy_bytes(void *destination, const void *source, size_t length) {
    uint8_t *out = (uint8_t *)destination;
    const uint8_t *in = (const uint8_t *)source;
    for (size_t i = 0U; i < length; ++i) out[i] = in[i];
}

static uint32_t read_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

static uint32_t read_be32(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 24) |
           ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) |
           (uint32_t)bytes[3];
}

static uint64_t read_be64(const uint8_t *bytes) {
    uint64_t value = 0U;
    for (size_t i = 0U; i < 8U; ++i)
        value = (value << 8) | bytes[i];
    return value;
}

static void write_be32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)(value >> 24);
    bytes[1] = (uint8_t)(value >> 16);
    bytes[2] = (uint8_t)(value >> 8);
    bytes[3] = (uint8_t)value;
}

static void write_be64(uint8_t *bytes, uint64_t value) {
    for (size_t i = 0U; i < 8U; ++i)
        bytes[i] = (uint8_t)(value >> (56U - (uint32_t)i * 8U));
}

static uint16_t read_le16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8));
}

bool nexus_usb_msc_find_interface(
    const uint8_t *configuration, size_t length,
    struct nexus_usb_msc_interface *interface_info) {
    if (configuration == NULL || interface_info == NULL || length < 9U ||
        configuration[0] < 9U || configuration[1] != 2U)
        return false;

    const uint16_t total_length = read_le16(configuration + 2U);
    const uint8_t configuration_value = configuration[5];
    if (configuration_value == 0U || total_length < configuration[0] ||
        total_length > length) return false;

    struct nexus_usb_msc_interface candidate = {0};
    uint8_t declared_endpoints = 0U;
    uint8_t observed_endpoints = 0U;
    bool active = false;

    for (size_t offset = configuration[0]; offset < total_length;) {
        if (total_length - offset < 2U) return false;
        const uint8_t descriptor_length = configuration[offset];
        const uint8_t descriptor_type = configuration[offset + 1U];
        if (descriptor_length < 2U ||
            descriptor_length > total_length - offset) return false;

        if (descriptor_type == 4U) {
            if (descriptor_length < 9U) return false;
            if (active && observed_endpoints < declared_endpoints)
                return false;
            active = configuration[offset + 3U] == 0U &&
                     configuration[offset + 5U] == 0x08U &&
                     configuration[offset + 6U] == 0x06U &&
                     configuration[offset + 7U] == 0x50U;
            candidate = (struct nexus_usb_msc_interface){0};
            declared_endpoints = configuration[offset + 4U];
            observed_endpoints = 0U;
            if (active) {
                candidate.configuration_value = configuration_value;
                candidate.interface_number = configuration[offset + 2U];
            }
        } else if (descriptor_type == 5U && active) {
            if (descriptor_length < 7U) return false;
            ++observed_endpoints;
            const uint8_t address = configuration[offset + 2U];
            const uint8_t attributes = configuration[offset + 3U] & 0x03U;
            const uint16_t raw_packet = read_le16(configuration + offset + 4U);
            const uint16_t max_packet = raw_packet & 0x07FFU;
            if (attributes == 2U && (raw_packet & 0xF800U) == 0U &&
                max_packet >= 8U && max_packet <= 1024U) {
                if ((address & 0x80U) != 0U &&
                    candidate.bulk_in_endpoint == 0U) {
                    candidate.bulk_in_endpoint = address;
                    candidate.bulk_in_max_packet = max_packet;
                } else if ((address & 0x80U) == 0U &&
                           (address & 0x0FU) != 0U &&
                           candidate.bulk_out_endpoint == 0U) {
                    candidate.bulk_out_endpoint = address;
                    candidate.bulk_out_max_packet = max_packet;
                }
            }
        }
        offset += descriptor_length;

        if (active && offset < total_length &&
            configuration[offset + 1U] == 4U) {
            if (observed_endpoints < declared_endpoints) return false;
            if (candidate.bulk_in_endpoint != 0U &&
                candidate.bulk_out_endpoint != 0U) {
                *interface_info = candidate;
                return true;
            }
        }
    }

    if (active && observed_endpoints < declared_endpoints) return false;
    if (candidate.bulk_in_endpoint == 0U ||
        candidate.bulk_out_endpoint == 0U) return false;
    *interface_info = candidate;
    return true;
}

static bool valid_endpoint(uint8_t endpoint, bool input) {
    return (endpoint & 0x70U) == 0U &&
           (endpoint & 0x0FU) != 0U &&
           (((endpoint & 0x80U) != 0U) == input);
}

static bool recover_transport(struct nexus_usb_msc_device *device) {
    if (device == NULL || device->transport.reset_recovery == NULL ||
        !device->transport.reset_recovery(
            device->transport_context, device->interface_number,
            device->bulk_in_endpoint, device->bulk_out_endpoint)) {
        if (device != NULL) device->initialized = false;
        return false;
    }
    return true;
}

static uint32_t next_command_tag(struct nexus_usb_msc_device *device) {
    ++device->next_tag;
    if (device->next_tag == 0U) ++device->next_tag;
    return device->next_tag;
}

static bool execute_scsi(struct nexus_usb_msc_device *device,
                         const uint8_t *cdb, uint8_t cdb_length,
                         enum scsi_data_direction direction,
                         const void *out_data, void *in_data,
                         uint32_t transfer_length) {
    if (device == NULL || !device->initialized || cdb == NULL ||
        cdb_length == 0U || cdb_length > 16U ||
        transfer_length > NEXUS_USB_MSC_MAX_TRANSFER_BYTES ||
        (direction == SCSI_DATA_NONE && transfer_length != 0U) ||
        (direction == SCSI_DATA_IN &&
         (transfer_length == 0U || in_data == NULL)) ||
        (direction == SCSI_DATA_OUT &&
         (transfer_length == 0U || out_data == NULL)) ||
        (direction != SCSI_DATA_NONE && direction != SCSI_DATA_IN &&
         direction != SCSI_DATA_OUT)) return false;

    uint8_t cbw[USB_MSC_CBW_BYTES];
    zero_bytes(cbw, sizeof(cbw));
    const uint32_t tag = next_command_tag(device);
    cbw[0] = 'U'; cbw[1] = 'S'; cbw[2] = 'B'; cbw[3] = 'C';
    cbw[4] = (uint8_t)tag;
    cbw[5] = (uint8_t)(tag >> 8);
    cbw[6] = (uint8_t)(tag >> 16);
    cbw[7] = (uint8_t)(tag >> 24);
    cbw[8] = (uint8_t)transfer_length;
    cbw[9] = (uint8_t)(transfer_length >> 8);
    cbw[10] = (uint8_t)(transfer_length >> 16);
    cbw[11] = (uint8_t)(transfer_length >> 24);
    cbw[12] = direction == SCSI_DATA_IN ? 0x80U : 0U;
    cbw[13] = device->lun;
    cbw[14] = cdb_length;
    copy_bytes(cbw + 15U, cdb, cdb_length);

    size_t actual = 0U;
    if (!device->transport.bulk_out(device->transport_context,
                                    device->bulk_out_endpoint,
                                    cbw, sizeof(cbw), &actual) ||
        actual != sizeof(cbw)) {
        (void)recover_transport(device);
        return false;
    }

    if (transfer_length != 0U) {
        actual = 0U;
        const bool transferred = direction == SCSI_DATA_IN
            ? device->transport.bulk_in(
                device->transport_context, device->bulk_in_endpoint,
                in_data, transfer_length, &actual)
            : device->transport.bulk_out(
                device->transport_context, device->bulk_out_endpoint,
                out_data, transfer_length, &actual);
        if (!transferred || actual != transfer_length) {
            (void)recover_transport(device);
            return false;
        }
    }

    uint8_t csw[USB_MSC_CSW_BYTES];
    actual = 0U;
    if (!device->transport.bulk_in(device->transport_context,
                                   device->bulk_in_endpoint,
                                   csw, sizeof(csw), &actual) ||
        actual != sizeof(csw) ||
        read_le32(csw) != USB_MSC_CSW_SIGNATURE ||
        read_le32(csw + 4U) != tag ||
        read_le32(csw + 8U) != 0U || csw[12] > 2U) {
        (void)recover_transport(device);
        return false;
    }

    device->last_csw_status = csw[12];
    if (csw[12] == 2U) {
        (void)recover_transport(device);
        return false;
    }
    return csw[12] == 0U;
}

static void copy_ascii_field(char *destination, size_t capacity,
                             const uint8_t *source, size_t length) {
    if (destination == NULL || capacity == 0U) return;
    size_t output = length < capacity - 1U ? length : capacity - 1U;
    for (size_t i = 0U; i < output; ++i) {
        const uint8_t value = source[i];
        destination[i] = value >= 0x20U && value <= 0x7EU
            ? (char)value : '?';
    }
    while (output != 0U && destination[output - 1U] == ' ') --output;
    destination[output] = '\0';
}

bool nexus_usb_msc_init(struct nexus_usb_msc_device *device,
                        const struct nexus_usb_msc_transport *transport,
                        void *transport_context,
                        uint8_t interface_number,
                        uint8_t bulk_in_endpoint,
                        uint8_t bulk_out_endpoint,
                        uint8_t lun) {
    if (device == NULL || transport == NULL ||
        transport->bulk_in == NULL || transport->bulk_out == NULL ||
        transport->reset_recovery == NULL ||
        !valid_endpoint(bulk_in_endpoint, true) ||
        !valid_endpoint(bulk_out_endpoint, false) || lun > 15U)
        return false;

    *device = (struct nexus_usb_msc_device){0};
    device->transport = *transport;
    device->transport_context = transport_context;
    device->interface_number = interface_number;
    device->bulk_in_endpoint = bulk_in_endpoint;
    device->bulk_out_endpoint = bulk_out_endpoint;
    device->lun = lun;
    device->next_tag = 0U;
    device->initialized = true;

    uint8_t inquiry[36];
    uint8_t cdb[16] = {0U};
    cdb[0] = SCSI_INQUIRY;
    cdb[4] = (uint8_t)sizeof(inquiry);
    if (!execute_scsi(device, cdb, 6U, SCSI_DATA_IN,
                      NULL, inquiry, sizeof(inquiry)) ||
        (inquiry[0] & 0x1FU) != 0U) {
        device->initialized = false;
        return false;
    }
    copy_ascii_field(device->vendor, sizeof(device->vendor),
                     inquiry + 8U, 8U);
    copy_ascii_field(device->product, sizeof(device->product),
                     inquiry + 16U, 16U);
    copy_ascii_field(device->revision, sizeof(device->revision),
                     inquiry + 32U, 4U);

    zero_bytes(cdb, sizeof(cdb));
    cdb[0] = SCSI_TEST_UNIT_READY;
    if (!execute_scsi(device, cdb, 6U, SCSI_DATA_NONE,
                      NULL, NULL, 0U)) {
        device->initialized = false;
        return false;
    }

    uint8_t capacity[32];
    zero_bytes(cdb, sizeof(cdb));
    cdb[0] = SCSI_READ_CAPACITY_10;
    if (!execute_scsi(device, cdb, 10U, SCSI_DATA_IN,
                      NULL, capacity, 8U)) {
        device->initialized = false;
        return false;
    }

    const uint32_t last_lba_10 = read_be32(capacity);
    const uint32_t block_size = read_be32(capacity + 4U);
    uint64_t block_count = 0U;
    if (last_lba_10 == UINT32_MAX) {
        zero_bytes(cdb, sizeof(cdb));
        cdb[0] = SCSI_SERVICE_ACTION_IN_16;
        cdb[1] = SCSI_READ_CAPACITY_16;
        write_be32(cdb + 10U, sizeof(capacity));
        if (!execute_scsi(device, cdb, 16U, SCSI_DATA_IN,
                          NULL, capacity, sizeof(capacity))) {
            device->initialized = false;
            return false;
        }
        const uint64_t last_lba_16 = read_be64(capacity);
        if (last_lba_16 == UINT64_MAX) {
            device->initialized = false;
            return false;
        }
        block_count = last_lba_16 + 1U;
    } else {
        block_count = (uint64_t)last_lba_10 + 1U;
    }

    if (block_count == 0U ||
        (block_size != 512U && block_size != 4096U)) {
        device->initialized = false;
        return false;
    }
    device->block_count = block_count;
    device->block_size = block_size;
    device->max_blocks_per_request =
        NEXUS_USB_MSC_MAX_TRANSFER_BYTES / block_size;
    return device->max_blocks_per_request != 0U;
}

bool nexus_usb_msc_read_blocks(struct nexus_usb_msc_device *device,
                               uint64_t lba, uint32_t block_count,
                               void *buffer) {
    if (device == NULL || !device->initialized || buffer == NULL ||
        block_count == 0U || lba >= device->block_count ||
        (uint64_t)block_count > device->block_count - lba ||
        block_count > device->max_blocks_per_request) return false;

    uint8_t cdb[16] = {0U};
    uint8_t cdb_length;
    if (lba <= UINT32_MAX && block_count <= UINT16_MAX) {
        cdb[0] = SCSI_READ_10;
        write_be32(cdb + 2U, (uint32_t)lba);
        cdb[7] = (uint8_t)(block_count >> 8);
        cdb[8] = (uint8_t)block_count;
        cdb_length = 10U;
    } else {
        cdb[0] = SCSI_READ_16;
        write_be64(cdb + 2U, lba);
        write_be32(cdb + 10U, block_count);
        cdb_length = 16U;
    }
    return execute_scsi(device, cdb, cdb_length, SCSI_DATA_IN,
                        NULL, buffer, block_count * device->block_size);
}

bool nexus_usb_msc_write_blocks(struct nexus_usb_msc_device *device,
                                uint64_t lba, uint32_t block_count,
                                const void *buffer) {
    if (device == NULL || !device->initialized || buffer == NULL ||
        block_count == 0U || lba >= device->block_count ||
        (uint64_t)block_count > device->block_count - lba ||
        block_count > device->max_blocks_per_request) return false;

    uint8_t cdb[16] = {0U};
    uint8_t cdb_length;
    if (lba <= UINT32_MAX && block_count <= UINT16_MAX) {
        cdb[0] = SCSI_WRITE_10;
        write_be32(cdb + 2U, (uint32_t)lba);
        cdb[7] = (uint8_t)(block_count >> 8);
        cdb[8] = (uint8_t)block_count;
        cdb_length = 10U;
    } else {
        cdb[0] = SCSI_WRITE_16;
        write_be64(cdb + 2U, lba);
        write_be32(cdb + 10U, block_count);
        cdb_length = 16U;
    }
    return execute_scsi(device, cdb, cdb_length, SCSI_DATA_OUT,
                        buffer, NULL, block_count * device->block_size);
}

bool nexus_usb_msc_flush(struct nexus_usb_msc_device *device) {
    uint8_t cdb[10] = {0U};
    cdb[0] = SCSI_SYNCHRONIZE_CACHE_10;
    return execute_scsi(device, cdb, sizeof(cdb), SCSI_DATA_NONE,
                        NULL, NULL, 0U);
}

bool nexus_usb_msc_request_sense(struct nexus_usb_msc_device *device,
                                 struct nexus_usb_msc_sense *sense) {
    if (device == NULL || sense == NULL || !device->initialized) return false;
    uint8_t cdb[6] = {0U};
    uint8_t response[NEXUS_USB_MSC_SENSE_BYTES];
    cdb[0] = SCSI_REQUEST_SENSE;
    cdb[4] = sizeof(response);
    if (!execute_scsi(device, cdb, sizeof(cdb), SCSI_DATA_IN,
                      NULL, response, sizeof(response))) return false;
    sense->key = response[2] & 0x0FU;
    sense->additional_code = response[12];
    sense->additional_code_qualifier = response[13];
    return true;
}

bool nexus_usb_msc_ready(const struct nexus_usb_msc_device *device) {
    return device != NULL && device->initialized &&
           device->block_count != 0U && device->block_size != 0U &&
           device->max_blocks_per_request != 0U;
}
