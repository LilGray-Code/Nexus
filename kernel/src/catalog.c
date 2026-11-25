#include "catalog.h"

#include <stddef.h>
#include <stdint.h>

#define CATALOG_MAGIC "NEXUS-CATALOG/1"

static bool catalog_line(const uint8_t *bytes, size_t length, size_t *offset,
                         size_t *start, size_t *line_length) {
    if (bytes == NULL || offset == NULL || start == NULL ||
        line_length == NULL || *offset >= length) return false;
    const size_t begin = *offset;
    size_t end = begin;
    while (end < length && bytes[end] != '\n') {
        if (bytes[end] == '\r' || bytes[end] == 0U ||
            end - begin >= NEXUS_CATALOG_URL_MAX + 128U) return false;
        ++end;
    }
    if (end >= length) return false;
    *start = begin;
    *line_length = end - begin;
    *offset = end + 1U;
    return true;
}

static bool line_value(const uint8_t *bytes, size_t start, size_t length,
                       const char *prefix, char *output, size_t capacity) {
    size_t prefix_length = 0U;
    while (prefix[prefix_length] != '\0') ++prefix_length;
    if (length <= prefix_length ||
        capacity <= length - prefix_length) return false;
    for (size_t i = 0U; i < prefix_length; ++i)
        if (bytes[start + i] != (uint8_t)prefix[i]) return false;
    for (size_t i = prefix_length; i < length; ++i) {
        const uint8_t c = bytes[start + i];
        if (c == 0U || c == '\r' || c == '\n') return false;
        output[i - prefix_length] = (char)c;
    }
    output[length - prefix_length] = '\0';
    return true;
}

static bool catalog_decimal(const char *text, uint32_t maximum,
                            uint32_t *value) {
    if (text == NULL || value == NULL || text[0] == '\0' ||
        (text[0] == '0' && text[1] != '\0')) return false;
    uint32_t parsed = 0U;
    for (size_t i = 0U; text[i] != '\0'; ++i) {
        if (text[i] < '0' || text[i] > '9') return false;
        const uint32_t digit = (uint32_t)(text[i] - '0');
        if (parsed > (maximum - digit) / 10U) return false;
        parsed = parsed * 10U + digit;
    }
    if (parsed == 0U) return false;
    *value = parsed;
    return true;
}

static bool catalog_id_valid(const char *id) {
    if (id == NULL || id[0] < 'A' || id[0] > 'Z') return false;
    size_t length = 0U;
    char previous = '\0';
    while (id[length] != '\0') {
        const char c = id[length];
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '-') || (c == '-' && previous == '-')) return false;
        previous = c;
        if (++length > NEXUS_PACKAGE_ID_MAX) return false;
    }
    return length != 0U && previous != '-';
}

static bool catalog_version_valid(const char *version) {
    if (version == NULL) return false;
    size_t length = 0U, component_start = 0U;
    uint32_t component = 0U;
    unsigned count = 0U;
    while (version[length] != '\0') {
        const char c = version[length];
        if (c == '.') {
            if (length == component_start) return false;
            ++count;
            component_start = length + 1U;
            component = 0U;
        } else {
            if (c < '0' || c > '9') return false;
            if (length == component_start && c == '0' &&
                version[length + 1U] != '.' && version[length + 1U] != '\0')
                return false;
            component = component * 10U + (uint32_t)(c - '0');
            if (component > 255U) return false;
        }
        if (++length > NEXUS_PACKAGE_VERSION_MAX) return false;
    }
    return length != component_start && count == 2U;
}

static bool catalog_key_valid(const char *key) {
    if (key == NULL || key[0] == '\0') return false;
    size_t length = 0U;
    while (key[length] != '\0') {
        const char c = key[length];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
        if (++length > NEXUS_PACKAGE_KEY_ID_MAX) return false;
    }
    return length != 0U;
}

static bool catalog_url_valid(const char *url) {
    static const char scheme[] = "https://";
    if (url == NULL) return false;
    for (size_t i = 0U; i < sizeof(scheme) - 1U; ++i)
        if (url[i] != scheme[i]) return false;

    size_t host_start = sizeof(scheme) - 1U;
    size_t host_end = host_start;
    while (url[host_end] != '\0' && url[host_end] != '/' &&
           url[host_end] != '?' && url[host_end] != '#') {
        const char c = url[host_end];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '.')) return false;
        ++host_end;
        if (host_end - host_start > 100U) return false;
    }
    if (host_end == host_start || url[host_end - 1U] == '.') return false;
    if (url[host_start] == '.' || url[host_start] == '-') return false;
    for (size_t i = host_start; i < host_end; ++i) {
        if (url[i] == '.' &&
            (i == host_start || url[i - 1U] == '.' || url[i - 1U] == '-'))
            return false;
        if (url[i] == '-' &&
            (i == host_start || url[i - 1U] == '.' || url[i - 1U] == '-'))
            return false;
    }
    if (url[host_end - 1U] == '-') return false;

    size_t length = host_end;
    while (url[length] != '\0') {
        const unsigned char c = (unsigned char)url[length];
        if (c <= 0x20U || c >= 0x7FU || c == '\\' || c == '|') return false;
        ++length;
        if (length > NEXUS_CATALOG_URL_MAX) return false;
    }
    return length > host_start;
}

static int compare_ids(const char *left, const char *right) {
    size_t i = 0U;
    while (left[i] != '\0' && right[i] != '\0') {
        if (left[i] < right[i]) return -1;
        if (left[i] > right[i]) return 1;
        ++i;
    }
    if (left[i] == right[i]) return 0;
    return left[i] == '\0' ? -1 : 1;
}

static int catalog_hex_nibble(uint8_t c) {
    if (c >= '0' && c <= '9') return (int)(c - '0');
    if (c >= 'a' && c <= 'f') return (int)(c - 'a') + 10;
    return -1;
}

static bool catalog_decode_hex(const uint8_t *text, size_t length,
                               uint8_t *output, size_t output_length) {
    if (text == NULL || output == NULL || length != output_length * 2U)
        return false;
    for (size_t i = 0U; i < output_length; ++i) {
        const int high = catalog_hex_nibble(text[i * 2U]);
        const int low = catalog_hex_nibble(text[i * 2U + 1U]);
        if (high < 0 || low < 0) return false;
        output[i] = (uint8_t)((high << 4) | low);
    }
    return true;
}

static bool parse_app_entry(const uint8_t *bytes, size_t start,
                            size_t line_length,
                            struct nexus_catalog_entry *entry) {
    static const char prefix[] = "APP=";
    const size_t prefix_length = sizeof(prefix) - 1U;
    if (bytes == NULL || entry == NULL || line_length <= prefix_length)
        return false;
    for (size_t i = 0U; i < prefix_length; ++i)
        if (bytes[start + i] != (uint8_t)prefix[i]) return false;

    const size_t value_start = start + prefix_length;
    size_t separators[3], count = 0U;
    for (size_t i = value_start; i < start + line_length; ++i) {
        if (bytes[i] == '|') {
            if (count >= 3U) return false;
            separators[count++] = i;
        }
    }
    if (count != 3U) return false;
    const size_t end = start + line_length;
    if (!line_value(bytes, value_start, separators[0] - value_start,
                    "", entry->id, sizeof(entry->id)) ||
        !catalog_id_valid(entry->id) ||
        !line_value(bytes, separators[0] + 1U,
                    separators[1] - separators[0] - 1U,
                    "", entry->version, sizeof(entry->version)) ||
        !catalog_version_valid(entry->version) ||
        !line_value(bytes, separators[1] + 1U,
                    separators[2] - separators[1] - 1U,
                    "", entry->url, sizeof(entry->url)) ||
        !catalog_url_valid(entry->url) ||
        !catalog_decode_hex(bytes + separators[2] + 1U,
                    end - separators[2] - 1U,
                    entry->package_sha256,
                    sizeof(entry->package_sha256))) return false;
    return true;
}

bool nexus_catalog_parse_verify(const void *catalog_bytes,
                                size_t catalog_length,
                                nexus_package_signature_verifier verifier,
                                void *verifier_context,
                                struct nexus_catalog *catalog) {
    if (catalog_bytes == NULL || catalog == NULL || verifier == NULL ||
        catalog_length == 0U || catalog_length > NEXUS_CATALOG_BYTES_MAX)
        return false;
    const uint8_t *bytes = (const uint8_t *)catalog_bytes;
    struct nexus_catalog parsed = {0};
    size_t offset = 0U, start = 0U, line_length = 0U;

    if (!catalog_line(bytes, catalog_length, &offset, &start, &line_length) ||
        line_length != sizeof(CATALOG_MAGIC) - 1U)
        return false;
    for (size_t i = 0U; i < line_length; ++i)
        if (bytes[start + i] != (uint8_t)CATALOG_MAGIC[i]) return false;

    char number[16];
    if (!catalog_line(bytes, catalog_length, &offset, &start, &line_length) ||
        !line_value(bytes, start, line_length, "GENERATION=",
                    number, sizeof(number)) ||
        !catalog_decimal(number, UINT32_MAX, &parsed.generation)) return false;

    if (!catalog_line(bytes, catalog_length, &offset, &start, &line_length) ||
        !line_value(bytes, start, line_length, "COUNT=",
                    number, sizeof(number)) ||
        !catalog_decimal(number, NEXUS_CATALOG_MAX_ENTRIES,
                         &parsed.entry_count)) return false;

    if (!catalog_line(bytes, catalog_length, &offset, &start, &line_length) ||
        !line_value(bytes, start, line_length, "KEY=", parsed.key_id,
                    sizeof(parsed.key_id)) ||
        !catalog_key_valid(parsed.key_id)) return false;

    for (uint32_t i = 0U; i < parsed.entry_count; ++i) {
        if (!catalog_line(bytes, catalog_length, &offset, &start,
                          &line_length) ||
            !parse_app_entry(bytes, start, line_length, &parsed.entries[i]))
            return false;
        if (i != 0U &&
            compare_ids(parsed.entries[i - 1U].id,
                        parsed.entries[i].id) >= 0) return false;
    }

    const size_t signed_message_size = offset;
    uint8_t signature[NEXUS_PACKAGE_SIGNATURE_BYTES];
    if (!catalog_line(bytes, catalog_length, &offset, &start, &line_length) ||
        line_length != sizeof("SIGNATURE=") - 1U +
                       NEXUS_PACKAGE_SIGNATURE_BYTES * 2U)
        return false;
    for (size_t i = 0U; i < sizeof("SIGNATURE=") - 1U; ++i)
        if (bytes[start + i] != (uint8_t)"SIGNATURE="[i]) return false;
    if (!catalog_decode_hex(bytes + start + sizeof("SIGNATURE=") - 1U,
                line_length - (sizeof("SIGNATURE=") - 1U),
                signature, sizeof(signature)) || offset != catalog_length)
        return false;

    if (!verifier(parsed.key_id, bytes, signed_message_size,
                  signature, verifier_context)) return false;
    *catalog = parsed;
    return true;
}
