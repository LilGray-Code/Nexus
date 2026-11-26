#include "http.h"

#include <stdint.h>

static bool bytes_equal_ci(const uint8_t *value, size_t length,
                           const char *expected) {
    size_t i = 0U;
    while (expected[i] != '\0') {
        if (i >= length) return false;
        uint8_t ch = value[i];
        if (ch >= 'A' && ch <= 'Z') ch = (uint8_t)(ch - 'A' + 'a');
        uint8_t want = (uint8_t)expected[i];
        if (want >= 'A' && want <= 'Z')
            want = (uint8_t)(want - 'A' + 'a');
        if (ch != want) return false;
        ++i;
    }
    return i == length;
}

static bool find_crlf(const uint8_t *data, size_t length, size_t start,
                      size_t *line_end) {
    if (data == NULL || line_end == NULL || start > length) return false;
    for (size_t i = start; i + 1U < length; ++i) {
        if (data[i] == '\r' && data[i + 1U] == '\n') {
            *line_end = i;
            return true;
        }
    }
    return false;
}

static bool parse_content_length(const uint8_t *value, size_t length,
                                 size_t *result) {
    if (value == NULL || result == NULL || length == 0U) return false;
    size_t parsed = 0U;
    for (size_t i = 0U; i < length; ++i) {
        if (value[i] < '0' || value[i] > '9') return false;
        const size_t digit = (size_t)(value[i] - '0');
        if (parsed > (SIZE_MAX - digit) / 10U) return false;
        parsed = parsed * 10U + digit;
    }
    *result = parsed;
    return true;
}

static bool parse_chunked_body(const uint8_t *wire, size_t wire_length,
                               size_t cursor, uint8_t *body,
                               size_t body_capacity, size_t *body_length) {
    size_t output_length = 0U;
    for (;;) {
        size_t line_end = 0U;
        if (!find_crlf(wire, wire_length, cursor, &line_end) ||
            line_end == cursor) return false;

        size_t digits_end = line_end;
        for (size_t i = cursor; i < line_end; ++i) {
            if (wire[i] == ';') {
                digits_end = i;
                break;
            }
        }
        if (digits_end == cursor) return false;

        size_t chunk_size = 0U;
        for (size_t i = cursor; i < digits_end; ++i) {
            const uint8_t ch = wire[i];
            size_t digit;
            if (ch >= '0' && ch <= '9') digit = (size_t)(ch - '0');
            else if (ch >= 'a' && ch <= 'f') digit = (size_t)(ch - 'a' + 10U);
            else if (ch >= 'A' && ch <= 'F') digit = (size_t)(ch - 'A' + 10U);
            else return false;
            if (chunk_size > (SIZE_MAX - digit) / 16U) return false;
            chunk_size = chunk_size * 16U + digit;
        }

        cursor = line_end + 2U;
        if (chunk_size == 0U) {
            /* Trailer fields are ignored, but their framing must be valid. */
            for (;;) {
                if (!find_crlf(wire, wire_length, cursor, &line_end))
                    return false;
                if (line_end == cursor) {
                    *body_length = output_length;
                    return true;
                }
                bool has_colon = false;
                for (size_t i = cursor; i < line_end; ++i) {
                    if (wire[i] == ':') {
                        has_colon = true;
                        break;
                    }
                }
                if (!has_colon) return false;
                cursor = line_end + 2U;
            }
        }

        if (chunk_size > body_capacity - output_length ||
            cursor > wire_length || chunk_size > wire_length - cursor)
            return false;
        if (wire_length - cursor - chunk_size < 2U ||
            wire[cursor + chunk_size] != '\r' ||
            wire[cursor + chunk_size + 1U] != '\n') return false;

        for (size_t i = 0U; i < chunk_size; ++i)
            body[output_length + i] = wire[cursor + i];
        output_length += chunk_size;
        cursor += chunk_size + 2U;
    }
}


static int http_base64_value(uint8_t ch) {
    if (ch >= 'A' && ch <= 'Z') return (int)(ch - 'A');
    if (ch >= 'a' && ch <= 'z') return (int)(ch - 'a') + 26;
    if (ch >= '0' && ch <= '9') return (int)(ch - '0') + 52;
    if (ch == '+') return 62;
    if (ch == '/') return 63;
    return -1;
}

/* RFC 9530 Content-Digest sha-256 byte-sequence or the common legacy
 * Digest: sha-256=<base64> form. A SHA-256 value is exactly 32 octets.
 */
static bool http_parse_sha256_base64(
    const uint8_t *value, size_t length, uint8_t output[32]) {
    if (value == NULL || output == NULL || length != 44U ||
        value[43] != '=') return false;
    size_t produced = 0U;
    for (size_t offset = 0U; offset < length; offset += 4U) {
        const int a = http_base64_value(value[offset]);
        const int b = http_base64_value(value[offset + 1U]);
        const int d = http_base64_value(value[offset + 2U]);
        const bool final = offset == 40U;
        if (a < 0 || b < 0 || d < 0) return false;
        if (!final) {
            const int e = http_base64_value(value[offset + 3U]);
            if (e < 0) return false;
            output[produced++] = (uint8_t)((a << 2) | (b >> 4));
            output[produced++] = (uint8_t)((b << 4) | (d >> 2));
            output[produced++] = (uint8_t)((d << 6) | e);
        } else {
            if (value[offset + 3U] != '=' || (d & 0x03) != 0)
                return false;
            output[produced++] = (uint8_t)((a << 2) | (b >> 4));
            output[produced++] = (uint8_t)((b << 4) | (d >> 2));
        }
    }
    return produced == 32U;
}

static bool http_parse_sha256_hex(
    const uint8_t *value, size_t length, uint8_t output[32]) {
    if (value == NULL || output == NULL || length != 64U) return false;
    for (size_t i = 0U; i < 32U; ++i) {
        const uint8_t high_ch = value[i * 2U];
        const uint8_t low_ch = value[i * 2U + 1U];
        const int high = high_ch >= '0' && high_ch <= '9'
            ? (int)(high_ch - '0')
            : (high_ch >= 'a' && high_ch <= 'f'
                ? (int)(high_ch - 'a') + 10
                : (high_ch >= 'A' && high_ch <= 'F'
                    ? (int)(high_ch - 'A') + 10 : -1));
        const int low = low_ch >= '0' && low_ch <= '9'
            ? (int)(low_ch - '0')
            : (low_ch >= 'a' && low_ch <= 'f'
                ? (int)(low_ch - 'a') + 10
                : (low_ch >= 'A' && low_ch <= 'F'
                    ? (int)(low_ch - 'A') + 10 : -1));
        if (high < 0 || low < 0) return false;
        output[i] = (uint8_t)((high << 4) | low);
    }
    return true;
}

static bool http_parse_content_digest(
    const uint8_t *value, size_t length, uint8_t output[32]) {
    static const char prefix[] = "sha-256=:";
    static const char suffix = ':';
    if (value == NULL || length != sizeof(prefix) - 1U + 45U ||
        !bytes_equal_ci(value, sizeof(prefix) - 1U, prefix) ||
        value[length - 1U] != (uint8_t)suffix) return false;
    return http_parse_sha256_base64(
        value + sizeof(prefix) - 1U, 44U, output);
}

bool nexus_http_decode_headers(const uint8_t *wire, size_t wire_length,
                               size_t *header_length,
                               struct nexus_http_response *response) {
    if (wire == NULL || header_length == NULL || response == NULL ||
        wire_length < 12U) return false;
    size_t header_end = 0U;
    bool found_separator = false;
    for (size_t i = 0U; i + 3U < wire_length; ++i) {
        if (wire[i] == '\r' && wire[i + 1U] == '\n' &&
            wire[i + 2U] == '\r' && wire[i + 3U] == '\n') {
            header_end = i;
            found_separator = true;
            break;
        }
    }
    if (!found_separator) return false;

    size_t status_line_end = 0U;
    if (!find_crlf(wire, header_end + 2U, 0U, &status_line_end) ||
        status_line_end < 12U ||
        (!bytes_equal_ci(wire, 8U, "HTTP/1.0") &&
         !bytes_equal_ci(wire, 8U, "HTTP/1.1")) ||
        wire[8] != ' ' || wire[9] < '0' || wire[9] > '9' ||
        wire[10] < '0' || wire[10] > '9' ||
        wire[11] < '0' || wire[11] > '9' ||
        (status_line_end > 12U && wire[12] != ' ')) return false;

    bool have_length = false;
    bool have_transfer_encoding = false;
    bool have_location = false;
    bool have_content_type = false;
    bool have_content_disposition = false;
    bool have_sha256_digest = false;
    uint8_t sha256_digest[32] = {0U};
    size_t declared_length = 0U;
    char location[NEXUS_HTTP_LOCATION_MAX + 1U] = {0};
    char content_type[NEXUS_HTTP_CONTENT_TYPE_MAX + 1U] = {0};
    char content_disposition[NEXUS_HTTP_DISPOSITION_MAX + 1U] = {0};
    size_t cursor = status_line_end + 2U;
    while (cursor < header_end) {
        size_t line_end = 0U;
        if (!find_crlf(wire, header_end + 2U, cursor, &line_end) ||
            line_end <= cursor) return false;
        size_t colon = cursor;
        while (colon < line_end && wire[colon] != ':') ++colon;
        if (colon == cursor || colon == line_end) return false;
        for (size_t i = cursor; i < colon; ++i)
            if (wire[i] <= 0x20U || wire[i] >= 0x7FU) return false;

        size_t value_start = colon + 1U;
        size_t value_end = line_end;
        while (value_start < value_end &&
               (wire[value_start] == ' ' || wire[value_start] == '\t'))
            ++value_start;
        while (value_end > value_start &&
               (wire[value_end - 1U] == ' ' ||
                wire[value_end - 1U] == '\t')) --value_end;
        const size_t value_length = value_end - value_start;

        if (bytes_equal_ci(wire + cursor, colon - cursor, "Content-Length")) {
            size_t parsed_length = 0U;
            if (!parse_content_length(wire + value_start, value_length,
                                      &parsed_length)) return false;
            if (have_length && parsed_length != declared_length) return false;
            declared_length = parsed_length;
            have_length = true;
        } else if (bytes_equal_ci(wire + cursor, colon - cursor,
                                  "Transfer-Encoding")) {
            if (have_transfer_encoding ||
                !bytes_equal_ci(wire + value_start, value_length, "chunked"))
                return false;
            have_transfer_encoding = true;
        } else if (bytes_equal_ci(wire + cursor, colon - cursor, "Location")) {
            if (have_location || value_length == 0U ||
                value_length > NEXUS_HTTP_LOCATION_MAX) return false;
            for (size_t i = 0U; i < value_length; ++i) {
                const uint8_t ch = wire[value_start + i];
                if (ch <= 0x20U || ch >= 0x7FU) return false;
                location[i] = (char)ch;
            }
            location[value_length] = '\0';
            have_location = true;
        } else if (bytes_equal_ci(wire + cursor, colon - cursor,
                                  "Content-Type")) {
            if (have_content_type || value_length == 0U ||
                value_length > NEXUS_HTTP_CONTENT_TYPE_MAX) return false;
            for (size_t i = 0U; i < value_length; ++i) {
                const uint8_t ch = wire[value_start + i];
                if (ch < 0x20U || ch >= 0x7FU) return false;
                content_type[i] = (char)ch;
            }
            content_type[value_length] = '\0';
            have_content_type = true;
        } else if (bytes_equal_ci(wire + cursor, colon - cursor,
                                  "Content-Disposition")) {
            if (have_content_disposition || value_length == 0U ||
                value_length > NEXUS_HTTP_DISPOSITION_MAX) return false;
            for (size_t i = 0U; i < value_length; ++i) {
                const uint8_t ch = wire[value_start + i];
                if (ch < 0x20U || ch >= 0x7FU) return false;
                content_disposition[i] = (char)ch;
            }
            content_disposition[value_length] = '\0';
            have_content_disposition = true;
        } else if (bytes_equal_ci(wire + cursor, colon - cursor,
                                  "Content-Digest")) {
            static const char prefix[] = "sha-256=:";
            size_t algorithm = 0U;
            while (algorithm + sizeof(prefix) - 1U <= value_length &&
                   !bytes_equal_ci(wire + value_start + algorithm,
                                   sizeof(prefix) - 1U, prefix))
                ++algorithm;
            if (algorithm + sizeof(prefix) - 1U <= value_length) {
                if (have_sha256_digest ||
                    !http_parse_content_digest(
                        wire + value_start + algorithm,
                        value_length - algorithm, sha256_digest)) return false;
                have_sha256_digest = true;
            }
        } else if (bytes_equal_ci(wire + cursor, colon - cursor,
                                  "Digest")) {
            static const char prefix[] = "sha-256=";
            if (value_length >= sizeof(prefix) - 1U &&
                bytes_equal_ci(wire + value_start,
                               sizeof(prefix) - 1U, prefix)) {
                if (have_sha256_digest ||
                    !http_parse_sha256_base64(
                        wire + value_start + sizeof(prefix) - 1U,
                        value_length - (sizeof(prefix) - 1U),
                        sha256_digest)) return false;
                have_sha256_digest = true;
            }
        } else if (bytes_equal_ci(wire + cursor, colon - cursor,
                                  "X-Checksum-Sha256")) {
            if (have_sha256_digest ||
                !http_parse_sha256_hex(wire + value_start, value_length,
                                       sha256_digest)) return false;
            have_sha256_digest = true;
        }
        cursor = line_end + 2U;
    }
    if (have_length && have_transfer_encoding) return false;

    response->status_code = (uint16_t)(
        (wire[9] - '0') * 100U + (wire[10] - '0') * 10U + (wire[11] - '0'));
    response->body_length = 0U;
    response->content_length = declared_length;
    response->chunked = have_transfer_encoding;
    response->has_content_length = have_length;
    response->has_location = have_location;
    response->has_content_type = have_content_type;
    response->has_content_disposition = have_content_disposition;
    response->has_sha256_digest = have_sha256_digest;
    for (size_t i = 0U; i < sizeof(response->sha256_digest); ++i)
        response->sha256_digest[i] = sha256_digest[i];
    for (size_t i = 0U; i < sizeof(response->location); ++i)
        response->location[i] = location[i];
    for (size_t i = 0U; i < sizeof(response->content_type); ++i)
        response->content_type[i] = content_type[i];
    for (size_t i = 0U; i < sizeof(response->content_disposition); ++i)
        response->content_disposition[i] = content_disposition[i];
    *header_length = header_end + 4U;
    return true;
}

enum nexus_http_chunk_state {
    NEXUS_HTTP_CHUNK_SIZE = 0,
    NEXUS_HTTP_CHUNK_SIZE_LF,
    NEXUS_HTTP_CHUNK_DATA,
    NEXUS_HTTP_CHUNK_DATA_CR,
    NEXUS_HTTP_CHUNK_DATA_LF,
    NEXUS_HTTP_CHUNK_TRAILER,
    NEXUS_HTTP_CHUNK_TRAILER_LF,
    NEXUS_HTTP_CHUNK_DONE,
    NEXUS_HTTP_CHUNK_FAILED
};

void nexus_http_chunked_decoder_init(
    struct nexus_http_chunked_decoder *decoder, uint32_t maximum_bytes) {
    if (decoder == NULL) return;
    *decoder = (struct nexus_http_chunked_decoder){0};
    decoder->maximum_bytes = maximum_bytes;
    decoder->state = NEXUS_HTTP_CHUNK_SIZE;
}

static int http_hex_digit(uint8_t value) {
    if (value >= '0' && value <= '9') return (int)(value - '0');
    if (value >= 'a' && value <= 'f') return (int)(value - 'a' + 10U);
    if (value >= 'A' && value <= 'F') return (int)(value - 'A' + 10U);
    return -1;
}

bool nexus_http_chunked_decoder_feed(
    struct nexus_http_chunked_decoder *decoder,
    const uint8_t *input, size_t input_length, size_t *input_used,
    uint8_t *output, size_t output_capacity, size_t *output_length,
    bool *complete) {
    if (decoder == NULL || input_used == NULL || output_length == NULL ||
        complete == NULL || (input_length != 0U && input == NULL) ||
        (output_capacity != 0U && output == NULL) ||
        decoder->state == NEXUS_HTTP_CHUNK_FAILED)
        return false;

    size_t consumed = 0U;
    size_t produced = 0U;
    *complete = decoder->state == NEXUS_HTTP_CHUNK_DONE;
    while (consumed < input_length &&
           decoder->state != NEXUS_HTTP_CHUNK_DONE) {
        const uint8_t ch = input[consumed];
        switch (decoder->state) {
        case NEXUS_HTTP_CHUNK_SIZE: {
            if (ch == '\r') {
                if (!decoder->line_has_digits) goto malformed;
                decoder->state = NEXUS_HTTP_CHUNK_SIZE_LF;
                ++consumed;
                break;
            }
            if (ch == ';') {
                if (!decoder->line_has_digits ||
                    decoder->line_extension) goto malformed;
                decoder->line_extension = true;
                ++decoder->line_length;
                ++consumed;
                break;
            }
            if (decoder->line_extension) {
                if (!((ch >= 0x20U && ch <= 0x7EU) || ch == '\t'))
                    goto malformed;
                if (decoder->line_length >= NEXUS_HTTP_CHUNK_LINE_MAX)
                    goto malformed;
                ++decoder->line_length;
                ++consumed;
                break;
            }
            {
                const int digit = http_hex_digit(ch);
                if (digit < 0 || decoder->line_length >= 8U)
                    goto malformed;
                if (decoder->chunk_value >
                    (UINT32_MAX - (uint32_t)digit) / 16U)
                    goto malformed;
                decoder->chunk_value =
                    decoder->chunk_value * 16U + (uint32_t)digit;
                decoder->line_has_digits = true;
                ++decoder->line_length;
                ++consumed;
            }
            break;
        }
        case NEXUS_HTTP_CHUNK_SIZE_LF:
            if (ch != '\n') goto malformed;
            ++consumed;
            if (decoder->chunk_value == 0U) {
                decoder->state = NEXUS_HTTP_CHUNK_TRAILER;
                decoder->line_length = 0U;
                decoder->line_has_colon = false;
            } else {
                if (decoder->decoded_bytes > decoder->maximum_bytes ||
                    decoder->chunk_value >
                        decoder->maximum_bytes - decoder->decoded_bytes)
                    goto malformed;
                decoder->chunk_remaining = decoder->chunk_value;
                decoder->state = NEXUS_HTTP_CHUNK_DATA;
            }
            break;
        case NEXUS_HTTP_CHUNK_DATA: {
            if (produced == output_capacity) {
                goto output_full;
            }
            size_t amount = input_length - consumed;
            const size_t output_room = output_capacity - produced;
            if (amount > output_room) amount = output_room;
            if (amount > decoder->chunk_remaining)
                amount = decoder->chunk_remaining;
            for (size_t i = 0U; i < amount; ++i)
                output[produced + i] = input[consumed + i];
            consumed += amount;
            produced += amount;
            decoder->decoded_bytes += (uint32_t)amount;
            decoder->chunk_remaining -= (uint32_t)amount;
            if (decoder->chunk_remaining == 0U)
                decoder->state = NEXUS_HTTP_CHUNK_DATA_CR;
            break;
        }
        case NEXUS_HTTP_CHUNK_DATA_CR:
            if (ch != '\r') goto malformed;
            ++consumed;
            decoder->state = NEXUS_HTTP_CHUNK_DATA_LF;
            break;
        case NEXUS_HTTP_CHUNK_DATA_LF:
            if (ch != '\n') goto malformed;
            ++consumed;
            decoder->state = NEXUS_HTTP_CHUNK_SIZE;
            decoder->chunk_value = 0U;
            decoder->line_length = 0U;
            decoder->line_has_digits = false;
            decoder->line_extension = false;
            break;
        case NEXUS_HTTP_CHUNK_TRAILER:
            if (ch == '\r') {
                if (decoder->line_length != 0U &&
                    !decoder->line_has_colon) goto malformed;
                ++consumed;
                decoder->state = NEXUS_HTTP_CHUNK_TRAILER_LF;
                break;
            }
            if ((ch < 0x20U && ch != '\t') || ch >= 0x7FU ||
                decoder->line_length >= NEXUS_HTTP_CHUNK_LINE_MAX)
                goto malformed;
            if (ch == ':') {
                if (decoder->line_length == 0U) goto malformed;
                decoder->line_has_colon = true;
            }
            ++decoder->line_length;
            ++consumed;
            break;
        case NEXUS_HTTP_CHUNK_TRAILER_LF:
            if (ch != '\n') goto malformed;
            ++consumed;
            if (decoder->line_length == 0U) {
                decoder->state = NEXUS_HTTP_CHUNK_DONE;
                *complete = true;
            } else {
                decoder->state = NEXUS_HTTP_CHUNK_TRAILER;
                decoder->line_length = 0U;
                decoder->line_has_colon = false;
            }
            break;
        case NEXUS_HTTP_CHUNK_DONE:
            *complete = true;
            goto output_full;
        default:
            goto malformed;
        }
    }

output_full:
    *input_used = consumed;
    *output_length = produced;
    *complete = decoder->state == NEXUS_HTTP_CHUNK_DONE;
    return true;

malformed:
    decoder->state = NEXUS_HTTP_CHUNK_FAILED;
    *input_used = consumed;
    *output_length = produced;
    *complete = false;
    return false;
}

bool nexus_http_decode_response(const uint8_t *wire, size_t wire_length,
                                uint8_t *body, size_t body_capacity,
                                struct nexus_http_response *response) {
    if (wire == NULL || body == NULL || response == NULL ||
        wire_length < 12U) return false;

    size_t header_end = 0U;
    bool found_separator = false;
    for (size_t i = 0U; i + 3U < wire_length; ++i) {
        if (wire[i] == '\r' && wire[i + 1U] == '\n' &&
            wire[i + 2U] == '\r' && wire[i + 3U] == '\n') {
            header_end = i;
            found_separator = true;
            break;
        }
    }
    if (!found_separator) return false;

    size_t status_line_end = 0U;
    if (!find_crlf(wire, header_end + 2U, 0U, &status_line_end) ||
        status_line_end < 12U) return false;
    if (!bytes_equal_ci(wire, 8U, "HTTP/1.0") &&
        !bytes_equal_ci(wire, 8U, "HTTP/1.1")) return false;
    if (wire[8] != ' ' || wire[9] < '0' || wire[9] > '9' ||
        wire[10] < '0' || wire[10] > '9' ||
        wire[11] < '0' || wire[11] > '9' ||
        (status_line_end > 12U && wire[12] != ' ')) return false;
    const uint16_t status_code = (uint16_t)(
        (wire[9] - '0') * 100U + (wire[10] - '0') * 10U +
        (wire[11] - '0'));

    bool have_length = false;
    bool have_transfer_encoding = false;
    bool have_location = false;
    bool have_content_type = false;
    bool have_content_disposition = false;
    char location[NEXUS_HTTP_LOCATION_MAX + 1U] = {0};
    char content_type[NEXUS_HTTP_CONTENT_TYPE_MAX + 1U] = {0};
    char content_disposition[NEXUS_HTTP_DISPOSITION_MAX + 1U] = {0};
    size_t declared_length = 0U;
    size_t cursor = status_line_end + 2U;
    while (cursor < header_end) {
        size_t line_end = 0U;
        if (!find_crlf(wire, header_end + 2U, cursor, &line_end) ||
            line_end <= cursor) return false;
        size_t colon = cursor;
        while (colon < line_end && wire[colon] != ':') ++colon;
        if (colon == cursor || colon == line_end) return false;
        for (size_t i = cursor; i < colon; ++i) {
            const uint8_t ch = wire[i];
            if (ch <= 0x20U || ch >= 0x7FU) return false;
        }

        size_t value_start = colon + 1U;
        size_t value_end = line_end;
        while (value_start < value_end &&
               (wire[value_start] == ' ' || wire[value_start] == '\t'))
            ++value_start;
        while (value_end > value_start &&
               (wire[value_end - 1U] == ' ' ||
                wire[value_end - 1U] == '\t')) --value_end;
        const size_t value_length = value_end - value_start;

        if (bytes_equal_ci(wire + cursor, colon - cursor,
                           "Content-Length")) {
            size_t parsed_length = 0U;
            if (!parse_content_length(wire + value_start, value_length,
                                      &parsed_length)) return false;
            if (have_length && parsed_length != declared_length) return false;
            declared_length = parsed_length;
            have_length = true;
        } else if (bytes_equal_ci(wire + cursor, colon - cursor,
                                  "Transfer-Encoding")) {
            if (have_transfer_encoding ||
                !bytes_equal_ci(wire + value_start, value_length,
                                "chunked")) return false;
            have_transfer_encoding = true;
        } else if (bytes_equal_ci(wire + cursor, colon - cursor,
                                  "Location")) {
            if (have_location || value_length == 0U ||
                value_length > NEXUS_HTTP_LOCATION_MAX) return false;
            for (size_t i = 0U; i < value_length; ++i) {
                const uint8_t ch = wire[value_start + i];
                if (ch <= 0x20U || ch >= 0x7FU) return false;
                location[i] = (char)ch;
            }
            location[value_length] = '\0';
            have_location = true;
        } else if (bytes_equal_ci(wire + cursor, colon - cursor,
                                  "Content-Type")) {
            if (have_content_type || value_length == 0U ||
                value_length > NEXUS_HTTP_CONTENT_TYPE_MAX) return false;
            for (size_t i = 0U; i < value_length; ++i) {
                const uint8_t ch = wire[value_start + i];
                if (ch < 0x20U || ch >= 0x7FU) return false;
                content_type[i] = (char)ch;
            }
            content_type[value_length] = '\0';
            have_content_type = true;
        } else if (bytes_equal_ci(wire + cursor, colon - cursor,
                                  "Content-Disposition")) {
            if (have_content_disposition || value_length == 0U ||
                value_length > NEXUS_HTTP_DISPOSITION_MAX) return false;
            for (size_t i = 0U; i < value_length; ++i) {
                const uint8_t ch = wire[value_start + i];
                if (ch < 0x20U || ch >= 0x7FU) return false;
                content_disposition[i] = (char)ch;
            }
            content_disposition[value_length] = '\0';
            have_content_disposition = true;
        }
        cursor = line_end + 2U;
    }

    if (have_length && have_transfer_encoding) return false;
    const size_t body_start = header_end + 4U;
    if (body_start > wire_length) return false;
    const size_t available = wire_length - body_start;
    size_t body_length = 0U;
    if (have_transfer_encoding) {
        if (!parse_chunked_body(wire, wire_length, body_start, body,
                                body_capacity, &body_length)) return false;
    } else {
        body_length = have_length ? declared_length : available;
        if (body_length > available || body_length > body_capacity)
            return false;
        for (size_t i = 0U; i < body_length; ++i)
            body[i] = wire[body_start + i];
    }

    response->status_code = status_code;
    response->body_length = body_length;
    response->content_length = have_length ? declared_length : body_length;
    response->chunked = have_transfer_encoding;
    response->has_content_length = have_length;
    response->has_location = have_location;
    response->has_content_type = have_content_type;
    response->has_content_disposition = have_content_disposition;
    for (size_t i = 0U; i < sizeof(response->location); ++i)
        response->location[i] = location[i];
    for (size_t i = 0U; i < sizeof(response->content_type); ++i)
        response->content_type[i] = content_type[i];
    for (size_t i = 0U; i < sizeof(response->content_disposition); ++i)
        response->content_disposition[i] = content_disposition[i];
    return true;
}
