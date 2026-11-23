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
        destination[i] = (char)source[i];
    }
    destination[length] = '\0';
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
        }
        cursor = end + 2U;
    }
    return false;
}
