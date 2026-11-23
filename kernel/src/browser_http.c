#include "browser_http.h"

#include <stddef.h>
#include <stdint.h>

static bool equal_ascii_ci(const uint8_t *left, size_t length,
                           const char *right) {
    size_t i = 0U;
    while (i < length && right[i] != '\0') {
        uint8_t a = left[i];
        uint8_t b = (uint8_t)right[i];
        if (a >= 'A' && a <= 'Z') a = (uint8_t)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (uint8_t)(b - 'A' + 'a');
        if (a != b) return false;
        ++i;
    }
    return i == length && right[i] == '\0';
}

static bool copy_header_value(char *destination, size_t capacity,
                              const uint8_t *source, size_t length) {
    while (length > 0U && (*source == ' ' || *source == '\t')) {
        ++source;
        --length;
    }
    while (length > 0U &&
           (source[length - 1U] == ' ' || source[length - 1U] == '\t')) {
        --length;
    }
    if (length >= capacity) return false;
    for (size_t i = 0U; i < length; ++i) {
        if (source[i] < 0x20U && source[i] != '\t') return false;
        if (source[i] > 0x7EU) return false;
        destination[i] = (char)source[i];
    }
    destination[length] = '\0';
    return true;
}

static bool contains_comma_token_ci(const char *value, size_t length,
                                      const char *wanted) {
    size_t cursor = 0U;
    while (cursor < length) {
        while (cursor < length &&
               (value[cursor] == ',' || value[cursor] == ' ' ||
                value[cursor] == 9U)) ++cursor;
        const size_t start = cursor;
        while (cursor < length && value[cursor] != ',' &&
               value[cursor] != ' ' && value[cursor] != 9U) ++cursor;
        if (cursor > start &&
            equal_ascii_ci((const uint8_t *)value + start,
                           cursor - start, wanted)) return true;
    }
    return false;
}

static bool parse_content_length(const uint8_t *source, size_t length,
                                 struct nexus_browser_http_response *parsed) {
    while (length > 0U && (*source == ' ' || *source == '\t')) {
        ++source;
        --length;
    }
    while (length > 0U &&
           (source[length - 1U] == ' ' || source[length - 1U] == '\t')) {
        --length;
    }
    if (length == 0U) return false;

    uint32_t value = 0U;
    for (size_t i = 0U; i < length; ++i) {
        if (source[i] < '0' || source[i] > '9') return false;
        const uint32_t digit = (uint32_t)(source[i] - '0');
        if (value > (UINT32_MAX - digit) / 10U) return false;
        value = value * 10U + digit;
    }
    if (parsed->has_content_length && parsed->content_length != value)
        return false;
    parsed->content_length = value;
    parsed->has_content_length = true;
    return true;
}

/* Extract the simple filename= parameter. RFC 5987 filename* is intentionally
 * left to the caller's fallback name because Nexus stores ASCII names only. */
static bool parse_content_disposition(const uint8_t *source, size_t length,
                                      char *filename, size_t capacity) {
    size_t cursor = 0U;
    while (cursor < length) {
        while (cursor < length &&
               (source[cursor] == ';' || source[cursor] == ' ' ||
                source[cursor] == '\t')) ++cursor;
        const size_t key_start = cursor;
        while (cursor < length && source[cursor] != '=' &&
               source[cursor] != ';') ++cursor;
        size_t key_end = cursor;
        while (key_end > key_start &&
               (source[key_end - 1U] == ' ' ||
                source[key_end - 1U] == '\t')) --key_end;
        if (cursor >= length || source[cursor] != '=') {
            if (cursor < length) ++cursor;
            continue;
        }
        ++cursor;
        while (cursor < length &&
               (source[cursor] == ' ' || source[cursor] == '\t')) ++cursor;

        const bool quoted = cursor < length && source[cursor] == '"';
        if (quoted) ++cursor;
        const size_t value_start = cursor;
        while (cursor < length) {
            if (quoted) {
                if (source[cursor] == '"') break;
                if (source[cursor] == '\\' && cursor + 1U < length)
                    cursor += 2U;
                else
                    ++cursor;
            } else if (source[cursor] == ';') {
                break;
            } else {
                ++cursor;
            }
        }
        const size_t value_end = cursor;
        if (quoted && cursor < length && source[cursor] == '"') ++cursor;
        while (cursor < length && source[cursor] != ';') ++cursor;

        if (equal_ascii_ci(source + key_start, key_end - key_start,
                           "filename")) {
            size_t used = 0U;
            for (size_t i = value_start; i < value_end; ++i) {
                uint8_t c = source[i];
                if (c == '\\' && quoted && i + 1U < value_end) c = source[++i];
                if (c < 0x20U || c > 0x7EU || used + 1U >= capacity) {
                    filename[0] = '\0';
                    return true;
                }
                filename[used++] = (char)c;
            }
            filename[used] = '\0';
            return true;
        }
    }
    return true;
}

bool nexus_browser_http_parse(const uint8_t *response, size_t response_length,
                              struct nexus_browser_http_response *parsed) {
    if (response == NULL || parsed == NULL || response_length < 12U ||
        response_length > 65536U) return false;
    *parsed = (struct nexus_browser_http_response){0};

    size_t line_end = 0U;
    while (line_end + 1U < response_length &&
           !(response[line_end] == '\r' && response[line_end + 1U] == '\n')) {
        if (response[line_end] < 0x20U || response[line_end] > 0x7EU)
            return false;
        ++line_end;
    }
    if (line_end + 1U >= response_length || line_end < 12U ||
        response[0] != 'H' || response[1] != 'T' ||
        response[2] != 'T' || response[3] != 'P' ||
        response[4] != '/' || response[5] != '1' ||
        response[6] != '.' ||
        (response[7] != '0' && response[7] != '1') ||
        response[8] != ' ' ||
        response[9] < '1' || response[9] > '5' ||
        response[10] < '0' || response[10] > '9' ||
        response[11] < '0' || response[11] > '9' ||
        (line_end > 12U && response[12] != ' ')) return false;

    parsed->status_code = (uint16_t)(
        (uint16_t)(response[9] - '0') * 100U +
        (uint16_t)(response[10] - '0') * 10U +
        (uint16_t)(response[11] - '0'));

    size_t cursor = line_end + 2U;
    while (cursor < response_length) {
        if (cursor > NEXUS_BROWSER_HTTP_HEADER_MAX) return false;
        if (cursor + 1U < response_length &&
            response[cursor] == '\r' && response[cursor + 1U] == '\n') {
            parsed->body_offset = cursor + 2U;
            return true;
        }

        size_t end = cursor;
        while (end + 1U < response_length &&
               !(response[end] == '\r' && response[end + 1U] == '\n')) {
            if (response[end] < 0x20U && response[end] != '\t') return false;
            if (response[end] > 0x7EU) return false;
            ++end;
        }
        if (end + 1U >= response_length || end == cursor) return false;

        size_t colon = cursor;
        while (colon < end && response[colon] != ':') ++colon;
        if (colon == cursor || colon == end) return false;
        const uint8_t *value = response + colon + 1U;
        const size_t value_length = end - colon - 1U;
        if (equal_ascii_ci(response + cursor, colon - cursor, "location")) {
            if (!copy_header_value(parsed->location,
                                   sizeof(parsed->location),
                                   value, value_length)) return false;
        } else if (equal_ascii_ci(response + cursor, colon - cursor,
                                  "content-type")) {
            if (!copy_header_value(parsed->content_type,
                                   sizeof(parsed->content_type),
                                   value, value_length)) return false;
        } else if (equal_ascii_ci(response + cursor, colon - cursor,
                                  "content-length")) {
            if (!parse_content_length(value, value_length, parsed))
                return false;
        } else if (equal_ascii_ci(response + cursor, colon - cursor,
                                  "transfer-encoding")) {
            char encoding[32];
            if (!copy_header_value(encoding, sizeof(encoding),
                                   value, value_length)) return false;
            size_t encoding_length = 0U;
            while (encoding_length < sizeof(encoding) &&
                   encoding[encoding_length] != '\0') ++encoding_length;
            parsed->chunked_transfer =
                contains_comma_token_ci(encoding, encoding_length, "chunked");
        } else if (equal_ascii_ci(response + cursor, colon - cursor,
                                  "content-disposition")) {
            if (!parse_content_disposition(value, value_length,
                                           parsed->filename,
                                           sizeof(parsed->filename)))
                return false;
        }
        cursor = end + 2U;
    }
    return false;
}
