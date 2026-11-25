#include "usb_msc.h"

#include <stddef.h>
#include <stdint.h>

#define USB_MSC_CBW_SIGNATURE 0x43425355U
#define USB_MSC_CSW_SIGNATURE 0x53425355U
#define USB_MSC_CBW_BYTES 31U
#define USB_MSC_CSW_BYTES 13U
#define USB_MSC_MAX_TRANSFER 4096U

struct nexus_usb_msc_state {
    bool ready;
    bool writable;
    uint32_t next_tag;
    uint64_t block_count;
    uint32_t block_size;
    struct nexus_usb_msc_transport transport;
    struct nexus_block_device block_device;
};

static struct nexus_usb_msc_state usb_msc;

static void put_le32(uint8_t *output, uint32_t value) {
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8);
    output[2] = (uint8_t)(value >> 16);
    output[3] = (uint8_t)(value >> 24);
}

static uint32_t get_le32(const uint8_t *input) {
    return (uint32_t)input[0] | ((uint32_t)input[1] << 8) |
           ((uint32_t)input[2] << 16) | ((uint32_t)input[3] << 24);
}

static uint32_t get_be32(const uint8_t *input) {
    return ((uint32_t)input[0] << 24) | ((uint32_t)input[1] << 16) |
           ((uint32_t)input[2] << 8) | (uint32_t)input[3];
}

static uint64_t get_be64(const uint8_t *input) {
    return ((uint64_t)get_be32(input) << 32) | get_be32(input + 4U);
}

static void put_be32(uint8_t *output, uint32_t value) {
    output[0] = (uint8_t)(value >> 24);
    output[1] = (uint8_t)(value >> 16);
    output[2] = (uint8_t)(value >> 8);
    output[3] = (uint8_t)value;
}

static void put_be64(uint8_t *output, uint64_t value) {
    put_be32(output, (uint32_t)(value >> 32));
    put_be32(output + 4U, (uint32_t)value);
}

static bool copy_text(char *destination, size_t capacity,
                      const char *source) {
    if (destination == NULL || capacity == 0U || source == NULL)
        return false;
    size_t i = 0U;
    while (i + 1U < capacity && source[i] != '\0') {
        destination[i] = source[i];
        ++i;
    }
    destination[i] = '\0';
    return source[i] == '\0';
}

static void copy_scsi_text(char *destination, size_t capacity,
                           const uint8_t *source, size_t source_length) {
    if (destination == NULL || capacity == 0U || source == NULL) return;
    size_t length = source_length < capacity - 1U
        ? source_length : capacity - 1U;
    for (size_t i = 0U; i < length; ++i) {
        const uint8_t character = source[i];
        destination[i] = character >= 0x20U && character <= 0x7EU
            ? (char)character : ' ';
    }
    while (length > 0U && destination[length - 1U] == ' ') --length;
    destination[length] = '\0';
}

static bool transport_exact_in(size_t expected, void *buffer) {
    size_t transferred = 0U;
    return usb_msc.transport.bulk_in != NULL &&
        usb_msc.transport.bulk_in(usb_msc.transport.context,
                                  buffer, expected, &transferred) &&
        transferred == expected;
}

static bool transport_exact_out(size_t expected, const void *buffer) {
    size_t transferred = 0U;
    return usb_msc.transport.bulk_out != NULL &&
        usb_msc.transport.bulk_out(usb_msc.transport.context,
                                   buffer, expected, &transferred) &&
        transferred == expected;
}

/* USB Mass Storage Bulk-Only Transport. One command is in flight at a time,
 * which matches Nexus's single-command, synchronous xHCI storage path.
 */
static bool usb_msc_command(const uint8_t *cdb, uint8_t cdb_length,
                            bool data_in, void *data, uint32_t data_length) {
    if (!usb_msc.ready && usb_msc.transport.bulk_in == NULL) return false;
    if (cdb == NULL || cdb_length == 0U || cdb_length > 16U ||
        (data_length != 0U && data == NULL) ||
        data_length > USB_MSC_MAX_TRANSFER) return false;

    uint8_t cbw[USB_MSC_CBW_BYTES] = {0U};
    const uint32_t tag = ++usb_msc.next_tag != 0U
        ? usb_msc.next_tag : ++usb_msc.next_tag;
    put_le32(cbw, USB_MSC_CBW_SIGNATURE);
    put_le32(cbw + 4U, tag);
    put_le32(cbw + 8U, data_length);
    cbw[12] = data_in ? 0x80U : 0U;
    cbw[13] = 0U;
    cbw[14] = cdb_length;
    for (size_t i = 0U; i < cdb_length; ++i) cbw[15U + i] = cdb[i];

    bool ok = transport_exact_out(sizeof(cbw), cbw);
    if (ok && data_length != 0U) {
        ok = data_in
            ? transport_exact_in(data_length, data)
            : transport_exact_out(data_length, data);
    }

    uint8_t csw[USB_MSC_CSW_BYTES] = {0U};
    if (ok) ok = transport_exact_in(sizeof(csw), csw);
    if (ok) {
        ok = get_le32(csw) == USB_MSC_CSW_SIGNATURE &&
             get_le32(csw + 4U) == tag &&
             get_le32(csw + 8U) == 0U && csw[12] == 0U;
    }

    if (!ok && usb_msc.transport.reset != NULL)
        (void)usb_msc.transport.reset(usb_msc.transport.context);
    return ok;
}

static bool usb_msc_test_unit_ready(void) {
    const uint8_t cdb[6] = {0x00U, 0U, 0U, 0U, 0U, 0U};
    return usb_msc_command(cdb, sizeof(cdb), false, NULL, 0U);
}

static bool usb_msc_inquiry(char model[NEXUS_BLOCKDEV_MODEL_MAX]) {
    uint8_t cdb[6] = {0x12U, 0U, 0U, 0U, 36U, 0U};
    uint8_t inquiry[36] = {0U};
    if (!usb_msc_command(cdb, sizeof(cdb), true,
                         inquiry, sizeof(inquiry))) return false;
    if ((inquiry[0] & 0x1FU) != 0U && (inquiry[0] & 0x1FU) != 0x00U)
        return false;
    char vendor[9];
    char product[17];
    copy_scsi_text(vendor, sizeof(vendor), inquiry + 8U, 8U);
    copy_scsi_text(product, sizeof(product), inquiry + 16U, 16U);
    size_t length = 0U;
    for (size_t i = 0U; vendor[i] != '\0' &&
         length + 1U < NEXUS_BLOCKDEV_MODEL_MAX; ++i)
        model[length++] = vendor[i];
    if (length != 0U && product[0] != '\0' &&
        length + 1U < NEXUS_BLOCKDEV_MODEL_MAX)
        model[length++] = ' ';
    for (size_t i = 0U; product[i] != '\0' &&
         length + 1U < NEXUS_BLOCKDEV_MODEL_MAX; ++i)
        model[length++] = product[i];
    model[length] = '\0';
    if (length == 0U)
        (void)copy_text(model, NEXUS_BLOCKDEV_MODEL_MAX, "USB SCSI Storage");
    return true;
}

static bool usb_msc_read_capacity(uint64_t *blocks, uint32_t *block_size) {
    if (blocks == NULL || block_size == NULL) return false;
    uint8_t cdb10[10] = {0x25U};
    uint8_t capacity10[8] = {0U};
    if (!usb_msc_command(cdb10, sizeof(cdb10), true,
                         capacity10, sizeof(capacity10))) return false;

    const uint32_t last_lba = get_be32(capacity10);
    const uint32_t sector_size = get_be32(capacity10 + 4U);
    if (sector_size == 0U || sector_size > USB_MSC_MAX_TRANSFER ||
        (sector_size & (sector_size - 1U)) != 0U) return false;

    uint64_t last = last_lba;
    if (last_lba == UINT32_MAX) {
        uint8_t cdb16[16] = {0x9EU, 0x10U};
        cdb16[10] = 0U; cdb16[11] = 0U; cdb16[12] = 0U;
        cdb16[13] = 32U;
        uint8_t capacity16[32] = {0U};
        if (!usb_msc_command(cdb16, sizeof(cdb16), true,
                             capacity16, sizeof(capacity16))) return false;
        last = get_be64(capacity16);
        const uint32_t extended_sector_size = get_be32(capacity16 + 8U);
        if (extended_sector_size != sector_size) return false;
    }
    if (last == UINT64_MAX) return false;
    *blocks = last + 1U;
    *block_size = sector_size;
    return *blocks > 1U;
}

/* MODE SENSE(6) is required before enabling the destructive installer path.
 * If a device does not report write protection reliably, Nexus treats it as
 * read-only rather than guessing that erasing it is safe.
 */
static bool usb_msc_check_write_protect(bool *write_protected) {
    if (write_protected == NULL) return false;
    uint8_t cdb[6] = {0x1AU, 0x08U, 0x3FU, 0U, 4U, 0U};
    uint8_t response[4] = {0U};
    if (!usb_msc_command(cdb, sizeof(cdb), true,
                         response, sizeof(response))) return false;
    *write_protected = (response[2] & 0x80U) != 0U;
    return true;
}

static bool usb_msc_rw_command(bool write, uint64_t lba,
                               uint32_t blocks, void *buffer) {
    if (buffer == NULL || blocks == 0U || usb_msc.block_size == 0U ||
        lba >= usb_msc.block_count ||
        (uint64_t)blocks > usb_msc.block_count - lba ||
        blocks > USB_MSC_MAX_TRANSFER / usb_msc.block_size)
        return false;

    uint8_t cdb[16] = {0U};
    uint8_t cdb_length;
    if (lba <= UINT32_MAX && blocks <= UINT16_MAX) {
        cdb[0] = write ? 0x2AU : 0x28U;
        put_be32(cdb + 2U, (uint32_t)lba);
        cdb[7] = (uint8_t)(blocks >> 8);
        cdb[8] = (uint8_t)blocks;
        cdb_length = 10U;
    } else {
        cdb[0] = write ? 0x8AU : 0x88U;
        put_be64(cdb + 2U, lba);
        put_be32(cdb + 10U, blocks);
        cdb_length = 16U;
    }
    const uint32_t bytes = blocks * usb_msc.block_size;
    return usb_msc_command(cdb, cdb_length, !write, buffer, bytes);
}

static bool usb_msc_block_read(void *context, uint64_t lba,
                               uint32_t blocks, void *buffer) {
    if (context != &usb_msc || !usb_msc.ready) return false;
    return usb_msc_rw_command(false, lba, blocks, buffer);
}

static bool usb_msc_block_write(void *context, uint64_t lba,
                                uint32_t blocks, const void *buffer) {
    if (context != &usb_msc || !usb_msc.ready || !usb_msc.writable)
        return false;
    return usb_msc_rw_command(true, lba, blocks, (void *)buffer);
}

static bool usb_msc_block_flush(void *context) {
    if (context != &usb_msc || !usb_msc.ready || !usb_msc.writable)
        return false;
    const uint8_t cdb[10] = {0x35U};
    return usb_msc_command(cdb, sizeof(cdb), false, NULL, 0U);
}

bool nexus_usb_msc_attach(const struct nexus_usb_msc_transport *transport) {
    if (transport == NULL || transport->context == NULL ||
        transport->bulk_in == NULL || transport->bulk_out == NULL ||
        transport->device_name[0] == '\0' ||
        transport->stable_id[0] == '\0' || usb_msc.ready)
        return false;

    usb_msc = (struct nexus_usb_msc_state){0};
    usb_msc.transport = *transport;
    if (!usb_msc_test_unit_ready()) goto failed;

    char model[NEXUS_BLOCKDEV_MODEL_MAX] = {0};
    if (!usb_msc_inquiry(model) ||
        !usb_msc_read_capacity(&usb_msc.block_count, &usb_msc.block_size))
        goto failed;

    bool write_protected = true;
    const bool mode_sense_valid = usb_msc_check_write_protect(&write_protected);
    usb_msc.writable = mode_sense_valid && !write_protected;

    const uint32_t max_blocks =
        USB_MSC_MAX_TRANSFER / usb_msc.block_size;
    if (max_blocks == 0U) goto failed;

    usb_msc.block_device = (struct nexus_block_device){0};
    if (!copy_text(usb_msc.block_device.name,
                   sizeof(usb_msc.block_device.name),
                   transport->device_name) ||
        !copy_text(usb_msc.block_device.stable_id,
                   sizeof(usb_msc.block_device.stable_id),
                   transport->stable_id) ||
        !copy_text(usb_msc.block_device.model,
                   sizeof(usb_msc.block_device.model), model))
        goto failed;
    usb_msc.block_device.block_count = usb_msc.block_count;
    usb_msc.block_device.block_size = usb_msc.block_size;
    usb_msc.block_device.max_blocks_per_request = max_blocks;
    usb_msc.block_device.transport = NEXUS_BLOCKDEV_TRANSPORT_USB;
    usb_msc.block_device.flags = NEXUS_BLOCKDEV_READ_ONLY |
        (usb_msc.writable ? NEXUS_BLOCKDEV_INSTALL_CAPABLE : 0U);
    usb_msc.block_device.context = &usb_msc;
    usb_msc.block_device.read = usb_msc_block_read;
    if (usb_msc.writable) {
        usb_msc.block_device.write = usb_msc_block_write;
        usb_msc.block_device.flush = usb_msc_block_flush;
    }

    if (!blockdev_register(&usb_msc.block_device)) goto failed;
    usb_msc.ready = true;
    return true;

failed:
    if (usb_msc.transport.reset != NULL)
        (void)usb_msc.transport.reset(usb_msc.transport.context);
    usb_msc = (struct nexus_usb_msc_state){0};
    return false;
}

bool nexus_usb_msc_ready(void) {
    return usb_msc.ready;
}

const struct nexus_block_device *nexus_usb_msc_block_device(void) {
    return usb_msc.ready ? &usb_msc.block_device : NULL;
}
