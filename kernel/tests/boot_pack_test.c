#include "boot_pack.h"

#include <assert.h>
#include <stdint.h>
#include <stddef.h>

#define HEADER_BYTES 32U
#define RECORD_BYTES 48U

static uint32_t crc32_bytes(const uint8_t *data, size_t length) {
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t i = 0U; i < length; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0U; bit < 8U; ++bit)
            crc = (crc >> 1) ^ ((crc & 1U) ? 0xEDB88320U : 0U);
    }
    return ~crc;
}
static void write_le32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}
static void make_pack(uint8_t image[128]) {
    static const uint8_t payload[] = "hello from NXSPACK1";
    const uint32_t header_size = HEADER_BYTES + RECORD_BYTES;
    const uint32_t total_size = header_size + sizeof(payload) - 1U;
    for (size_t i = 0U; i < 128U; ++i) image[i] = 0U;
    const uint8_t magic[8] = {'N','X','S','P','A','C','K','1'};
    for (size_t i = 0U; i < 8U; ++i) image[i] = magic[i];
    write_le32(image + 8U, 1U);
    write_le32(image + 12U, header_size);
    write_le32(image + 16U, total_size);
    write_le32(image + 20U, 1U);
    uint8_t *record = image + HEADER_BYTES;
    const char name[] = "HELLO.TXT";
    for (size_t i = 0U; i < sizeof(name) - 1U; ++i)
        record[i] = (uint8_t)name[i];
    write_le32(record + 32U, header_size);
    write_le32(record + 36U, (uint32_t)(sizeof(payload) - 1U));
    write_le32(record + 40U, crc32_bytes(payload, sizeof(payload) - 1U));
    write_le32(record + 44U, 0U);
    write_le32(image + 24U, crc32_bytes(record, RECORD_BYTES));
    for (size_t i = 0U; i < sizeof(payload) - 1U; ++i)
        image[header_size + i] = payload[i];
    write_le32(image + 28U,
               crc32_bytes(image + header_size, sizeof(payload) - 1U));
}
int main(void) {
    uint8_t image[128];
    make_pack(image);
    const uint32_t image_size = HEADER_BYTES + RECORD_BYTES +
                                (uint32_t)(sizeof("hello from NXSPACK1") - 1U);
    assert(nexus_boot_pack_init(image, image_size));
    assert(nexus_boot_pack_ready());
    assert(nexus_boot_pack_count() == 1U);
    const struct nexus_boot_pack_entry *entry =
        nexus_boot_pack_find("hello.txt");
    assert(entry != NULL && entry->size == sizeof("hello from NXSPACK1") - 1U);
    assert(nexus_boot_pack_find("../HELLO.TXT") == NULL);
    char output[32] = {0};
    size_t read_count = 0U;
    assert(nexus_boot_pack_read(entry, 0U, output, sizeof(output), &read_count));
    assert(read_count == entry->size);
    assert(output[0] == 'h' && output[read_count - 1U] == '1');
    assert(nexus_boot_pack_read(entry, entry->size, output, sizeof(output),
                                &read_count));
    assert(read_count == 0U);
    assert(!nexus_boot_pack_read(entry, entry->size + 1U, output,
                                 sizeof(output), &read_count));
    image[HEADER_BYTES + RECORD_BYTES] ^= 1U;
    assert(!nexus_boot_pack_init(image, image_size));
    assert(!nexus_boot_pack_ready());
    assert(nexus_boot_pack_count() == 0U);
    return 0;
}
