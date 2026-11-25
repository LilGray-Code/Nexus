#include "usb_msc.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

struct mock_usb_storage {
    uint32_t tag;
    uint8_t opcode;
    uint32_t transfer_length;
    bool data_in;
    bool data_pending;
    bool write_protected;
    bool saw_write;
    unsigned commands;
    uint8_t inquiry[36];
};

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

static void put_be32(uint8_t *output, uint32_t value) {
    output[0] = (uint8_t)(value >> 24);
    output[1] = (uint8_t)(value >> 16);
    output[2] = (uint8_t)(value >> 8);
    output[3] = (uint8_t)value;
}

static bool mock_bulk_out(void *context, const void *buffer,
                          size_t length, size_t *transferred) {
    struct mock_usb_storage *mock = context;
    assert(mock != NULL && buffer != NULL && transferred != NULL);
    const uint8_t *bytes = buffer;
    if (length == 31U && get_le32(bytes) == 0x43425355U) {
        mock->tag = get_le32(bytes + 4U);
        mock->transfer_length = get_le32(bytes + 8U);
        mock->data_in = (bytes[12] & 0x80U) != 0U;
        mock->opcode = bytes[15U];
        mock->data_pending = mock->transfer_length != 0U;
        ++mock->commands;
    } else {
        assert(mock->data_pending && !mock->data_in);
        assert(length == mock->transfer_length);
        mock->saw_write = true;
        mock->data_pending = false;
    }
    *transferred = length;
    return true;
}

static bool mock_bulk_in(void *context, void *buffer,
                         size_t length, size_t *transferred) {
    struct mock_usb_storage *mock = context;
    assert(mock != NULL && buffer != NULL && transferred != NULL);
    uint8_t *bytes = buffer;
    if (mock->data_pending) {
        assert(mock->data_in && length == mock->transfer_length);
        for (size_t i = 0U; i < length; ++i) bytes[i] = 0U;
        if (mock->opcode == 0x12U) {
            assert(length == sizeof(mock->inquiry));
            memcpy(bytes, mock->inquiry, length);
        } else if (mock->opcode == 0x25U) {
            assert(length == 8U);
            put_be32(bytes, 2047U);
            put_be32(bytes + 4U, 512U);
        } else if (mock->opcode == 0x1AU) {
            assert(length == 4U);
            bytes[2] = mock->write_protected ? 0x80U : 0U;
        } else if (mock->opcode == 0x28U || mock->opcode == 0x88U) {
            for (size_t i = 0U; i < length; ++i)
                bytes[i] = (uint8_t)(0x5AU ^ (uint8_t)i);
        } else {
            assert(!"unexpected USB MSC data-in command");
        }
        mock->data_pending = false;
        *transferred = length;
        return true;
    }

    assert(length == 13U);
    memset(bytes, 0, length);
    put_le32(bytes, 0x53425355U);
    put_le32(bytes + 4U, mock->tag);
    *transferred = length;
    return true;
}

static bool mock_reset(void *context) {
    struct mock_usb_storage *mock = context;
    assert(mock != NULL);
    mock->data_pending = false;
    return true;
}

static void init_mock(struct mock_usb_storage *mock, bool write_protected) {
    *mock = (struct mock_usb_storage){0};
    mock->write_protected = write_protected;
    mock->inquiry[0] = 0U;
    memcpy(mock->inquiry + 8U, "NEXUSUSB", 8U);
    memcpy(mock->inquiry + 16U, "TEST DISK       ", 16U);
}

static struct nexus_usb_msc_transport make_transport(
    struct mock_usb_storage *mock) {
    struct nexus_usb_msc_transport transport = {
        .context = mock,
        .bulk_in = mock_bulk_in,
        .bulk_out = mock_bulk_out,
        .reset = mock_reset
    };
    memcpy(transport.device_name, "USB0", 5U);
    memcpy(transport.stable_id, "USB-MSC-PORT-02", 16U);
    return transport;
}

int main(void) {
    struct mock_usb_storage mock;
    init_mock(&mock, false);
    struct nexus_usb_msc_transport transport = make_transport(&mock);

    blockdev_init();
    assert(nexus_usb_msc_attach(&transport));
    assert(nexus_usb_msc_ready());
    assert(blockdev_count() == 1U);
    const struct nexus_block_device *device = blockdev_at(0U);
    assert(device != NULL);
    assert(device->transport == NEXUS_BLOCKDEV_TRANSPORT_USB);
    assert(device->block_count == 2048U);
    assert(device->block_size == 512U);
    assert(device->max_blocks_per_request == 8U);
    assert(blockdev_install_write_supported(device));
    assert(strcmp(device->stable_id, "USB-MSC-PORT-02") == 0);
    assert(strcmp(device->model, "NEXUSUSB TEST DISK") == 0);

    uint8_t readback[1024] = {0U};
    assert(blockdev_read(device, 3U, 2U, readback));
    for (size_t i = 0U; i < sizeof(readback); ++i)
        assert(readback[i] == (uint8_t)(0x5AU ^ (uint8_t)i));

    assert(blockdev_set_boot_media_external(NEXUS_BOOT_SOURCE_OPTICAL));
    assert(blockdev_install_session_begin(
        device, "USB-MSC-PORT-02", "ERASE"));
    uint8_t write_data[512];
    memset(write_data, 0xC3, sizeof(write_data));
    assert(blockdev_install_write(device, 7U, 1U, write_data));
    assert(mock.saw_write);
    assert(blockdev_install_flush(device));
    blockdev_install_session_end();

    return 0;
}
