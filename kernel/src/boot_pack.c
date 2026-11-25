#include "boot_pack.h"

#include <stddef.h>
#include <stdint.h>

#define NXSP_HEADER_BYTES 32U
#define NXSP_RECORD_BYTES 48U
#define NXSP_VERSION 1U

static const uint8_t nxsp_magic[8] = {'N','X','S','P','A','C','K','1'};
static struct nexus_boot_pack_entry entries[NEXUS_BOOT_PACK_MAX_FILES];
static size_t entry_count;
static bool pack_ready;

static uint32_t read_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint32_t crc32_bytes(const uint8_t *data, size_t length) {
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t i = 0U; i < length; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0U; bit < 8U; ++bit)
            crc = (crc >> 1) ^ ((crc & 1U) ? 0xEDB88320U : 0U);
    }
    return ~crc;
}
static char upper_ascii(char c) {
    return c >= 'a' && c <= 'z' ? (char)(c - 'a' + 'A') : c;
}
static bool names_equal(const char *a, const char *b) {
    if (a == NULL || b == NULL) return false;
    size_t i = 0U;
    while (i < NEXUS_BOOT_PACK_NAME_MAX) {
        const char x = upper_ascii(a[i]);
        const char y = upper_ascii(b[i]);
        if (x != y) return false;
        if (x == '\0') return true;
        ++i;
    }
    return false;
}
static bool valid_name(const uint8_t name[NEXUS_BOOT_PACK_NAME_MAX]) {
    size_t length = 0U;
    while (length < NEXUS_BOOT_PACK_NAME_MAX && name[length] != 0U) {
        const uint8_t c = name[length];
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '.' || c == '_' || c == '-')) return false;
        ++length;
    }
    if (length == 0U || length >= NEXUS_BOOT_PACK_NAME_MAX) return false;
    for (size_t i = length; i < NEXUS_BOOT_PACK_NAME_MAX; ++i)
        if (name[i] != 0U) return false;
    return true;
}
static bool pointer_is_entry(const struct nexus_boot_pack_entry *entry) {
    if (entry == NULL) return false;
    for (size_t i = 0U; i < entry_count; ++i)
        if (entry == &entries[i]) return true;
    return false;
}

bool nexus_boot_pack_init(const void *image, size_t image_size) {
    pack_ready = false;
    entry_count = 0U;
    for (size_t i = 0U; i < NEXUS_BOOT_PACK_MAX_FILES; ++i)
        entries[i] = (struct nexus_boot_pack_entry){0};

    if (image == NULL || image_size < NXSP_HEADER_BYTES ||
        image_size > NEXUS_BOOT_PACK_MAX_BYTES || image_size > UINT32_MAX)
        return false;
    const uint8_t *bytes = (const uint8_t *)image;
    for (size_t i = 0U; i < sizeof(nxsp_magic); ++i)
        if (bytes[i] != nxsp_magic[i]) return false;

    const uint32_t version = read_le32(bytes + 8U);
    const uint32_t header_size = read_le32(bytes + 12U);
    const uint32_t total_size = read_le32(bytes + 16U);
    const uint32_t count = read_le32(bytes + 20U);
    const uint32_t table_crc = read_le32(bytes + 24U);
    const uint32_t payload_crc = read_le32(bytes + 28U);

    if (version != NXSP_VERSION || total_size != image_size ||
        count == 0U || count > NEXUS_BOOT_PACK_MAX_FILES ||
        header_size != NXSP_HEADER_BYTES + count * NXSP_RECORD_BYTES ||
        header_size > total_size) return false;

    const size_t table_bytes = (size_t)count * NXSP_RECORD_BYTES;
    if (crc32_bytes(bytes + NXSP_HEADER_BYTES, table_bytes) != table_crc ||
        crc32_bytes(bytes + header_size, image_size - header_size) !=
            payload_crc) return false;

    struct nexus_boot_pack_entry parsed[NEXUS_BOOT_PACK_MAX_FILES] = {0};
    uint32_t offsets[NEXUS_BOOT_PACK_MAX_FILES] = {0};
    uint32_t sizes[NEXUS_BOOT_PACK_MAX_FILES] = {0};

    for (uint32_t i = 0U; i < count; ++i) {
        const uint8_t *record =
            bytes + NXSP_HEADER_BYTES + (size_t)i * NXSP_RECORD_BYTES;
        if (!valid_name(record)) return false;
        for (size_t n = 0U; n < NEXUS_BOOT_PACK_NAME_MAX; ++n)
            parsed[i].name[n] = (char)record[n];

        const uint32_t offset = read_le32(record + 32U);
        const uint32_t size = read_le32(record + 36U);
        const uint32_t crc = read_le32(record + 40U);
        const uint32_t flags = read_le32(record + 44U);
        if (flags != 0U || offset < header_size || offset > total_size ||
            size > total_size - offset) return false;
        if (crc32_bytes(bytes + offset, size) != crc) return false;

        parsed[i].data = bytes + offset;
        parsed[i].size = size;
        parsed[i].crc32 = crc;
        offsets[i] = offset;
        sizes[i] = size;

        for (uint32_t j = 0U; j < i; ++j) {
            if (names_equal(parsed[i].name, parsed[j].name)) return false;
            const uint64_t a0 = offsets[i];
            const uint64_t a1 = a0 + sizes[i];
            const uint64_t b0 = offsets[j];
            const uint64_t b1 = b0 + sizes[j];
            if (sizes[i] != 0U && sizes[j] != 0U &&
                a0 < b1 && b0 < a1) return false;
        }
    }

    for (uint32_t i = 0U; i < count; ++i) entries[i] = parsed[i];
    entry_count = count;
    pack_ready = true;
    return true;
}

bool nexus_boot_pack_ready(void) { return pack_ready; }
size_t nexus_boot_pack_count(void) { return pack_ready ? entry_count : 0U; }

const struct nexus_boot_pack_entry *nexus_boot_pack_entry_at(size_t index) {
    return pack_ready && index < entry_count ? &entries[index] : NULL;
}

const struct nexus_boot_pack_entry *nexus_boot_pack_find(const char *name) {
    if (!pack_ready || name == NULL) return NULL;
    for (size_t i = 0U; i < entry_count; ++i)
        if (names_equal(entries[i].name, name)) return &entries[i];
    return NULL;
}

bool nexus_boot_pack_read(const struct nexus_boot_pack_entry *entry,
                          uint32_t offset, void *buffer,
                          size_t requested, size_t *bytes_read) {
    if (bytes_read != NULL) *bytes_read = 0U;
    if (!pack_ready || !pointer_is_entry(entry) || bytes_read == NULL ||
        (requested != 0U && buffer == NULL) || offset > entry->size)
        return false;
    size_t amount = requested;
    const uint32_t remaining = entry->size - offset;
    if (amount > remaining) amount = remaining;
    if (amount > 4096U) amount = 4096U;
    const uint8_t *source = entry->data + offset;
    uint8_t *destination = (uint8_t *)buffer;
    for (size_t i = 0U; i < amount; ++i) destination[i] = source[i];
    *bytes_read = amount;
    return true;
}
