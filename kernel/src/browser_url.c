#include "browser_url.h"

#include <stddef.h>
#include <stdint.h>

static bool is_control_or_space(char c) {
    const unsigned char value = (unsigned char)c;
    return value <= 0x20U || value == 0x7FU;
}

static bool is_host_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '.' || c == '-';
}

static bool parse_port(const char *text, size_t length, uint16_t *port) {
    if (length == 0U || port == NULL) return false;
    uint32_t value = 0U;
    for (size_t i = 0U; i < length; ++i) {
        if (text[i] < '0' || text[i] > '9') return false;
        value = value * 10U + (uint32_t)(text[i] - '0');
        if (value > 65535U) return false;
    }
    if (value == 0U) return false;
    *port = (uint16_t)value;
    return true;
}

bool nexus_browser_url_parse(const char *input,
                             struct nexus_browser_url *output) {
    if (input == NULL || output == NULL) return false;
    struct nexus_browser_url parsed = {0};
    size_t length = 0U;
    while (input[length] != '\0') {
        if (is_control_or_space(input[length])) return false;
        ++length;
        if (length > NEXUS_BROWSER_HOST_MAX + NEXUS_BROWSER_PATH_MAX + 16U)
            return false;
    }

    size_t authority_start;
    if (length >= 7U &&
        input[0] == 'h' && input[1] == 't' && input[2] == 't' &&
        input[3] == 'p' && input[4] == ':' && input[5] == '/' &&
        input[6] == '/') {
        parsed.scheme = NEXUS_BROWSER_SCHEME_HTTP;
        parsed.port = 80U;
        authority_start = 7U;
    } else if (length >= 8U &&
        input[0] == 'h' && input[1] == 't' && input[2] == 't' &&
        input[3] == 'p' && input[4] == 's' && input[5] == ':' &&
        input[6] == '/' && input[7] == '/') {
        parsed.scheme = NEXUS_BROWSER_SCHEME_HTTPS;
        parsed.port = 443U;
        authority_start = 8U;
    } else {
        return false;
    }

    size_t authority_end = authority_start;
    while (authority_end < length && input[authority_end] != '/' &&
           input[authority_end] != '?' && input[authority_end] != '#') {
        if (input[authority_end] == '@' || input[authority_end] == '[' ||
            input[authority_end] == ']') return false;
        ++authority_end;
    }
    if (authority_end == authority_start) return false;

    size_t host_end = authority_end;
    size_t colon = authority_end;
    for (size_t i = authority_start; i < authority_end; ++i) {
        if (input[i] == ':') {
            if (colon != authority_end) return false;
            colon = i;
        } else if (!is_host_char(input[i])) {
            return false;
        }
    }
    if (colon != authority_end) host_end = colon;
    const size_t host_length = host_end - authority_start;
    if (host_length == 0U || host_length > NEXUS_BROWSER_HOST_MAX ||
        input[authority_start] == '.' ||
        input[host_end - 1U] == '.' ||
        input[authority_start] == '-' || input[host_end - 1U] == '-')
        return false;
    for (size_t i = 0U; i < host_length; ++i)
        parsed.host[i] = input[authority_start + i];
    parsed.host[host_length] = '\0';
    /* DNS labels are 1..63 octets and cannot begin or end with a hyphen.
     * Reject empty labels and overlong labels before passing the host to DNS. */
    size_t label_length = 0U;
    for (size_t i = 0U; i < host_length; ++i) {
        if (parsed.host[i] == '.') {
            if (label_length == 0U || label_length > 63U ||
                parsed.host[i - 1U] == '-') return false;
            label_length = 0U;
        } else {
            if (label_length == 0U && parsed.host[i] == '-') return false;
            ++label_length;
            if (label_length > 63U) return false;
        }
    }
    if (label_length == 0U || parsed.host[host_length - 1U] == '-')
        return false;

    if (colon != authority_end) {
        if (!parse_port(input + colon + 1U, authority_end - colon - 1U,
                        &parsed.port)) return false;
        parsed.explicit_port = true;
    }

    size_t path_start = authority_end;
    size_t path_end = length;
    for (size_t i = path_start; i < length; ++i) {
        if (input[i] == '#') {
            path_end = i;
            break;
        }
    }
    if (path_start == path_end || input[path_start] == '?') {
        parsed.path[0] = '/';
        size_t query_start = path_start;
        if (query_start < path_end && input[query_start] == '?') {
            size_t query_length = path_end - query_start;
            if (query_length + 1U > NEXUS_BROWSER_PATH_MAX) return false;
            for (size_t i = 0U; i < query_length; ++i)
                parsed.path[i + 1U] = input[query_start + i];
            parsed.path[query_length + 1U] = '\0';
        } else {
            parsed.path[1] = '\0';
        }
    } else {
        const size_t path_length = path_end - path_start;
        if (path_length > NEXUS_BROWSER_PATH_MAX) return false;
        for (size_t i = 0U; i < path_length; ++i)
            parsed.path[i] = input[path_start + i];
        parsed.path[path_length] = '\0';
    }
    *output = parsed;
    return true;
}


static bool url_append_text(char *output, size_t capacity, size_t *used,
                            const char *text) {
    if (output == NULL || used == NULL || text == NULL || *used >= capacity)
        return false;
    size_t length = 0U;
    while (text[length] != '\0') ++length;
    if (length > capacity - *used - 1U) return false;
    for (size_t i = 0U; i < length; ++i) output[(*used)++] = text[i];
    output[*used] = '\0';
    return true;
}

static bool url_append_char(char *output, size_t capacity, size_t *used,
                             char value) {
    if (output == NULL || used == NULL || capacity == 0U ||
        *used >= capacity - 1U) return false;
    output[(*used)++] = value;
    output[*used] = '\0';
    return true;
}

static bool url_format_absolute(const struct nexus_browser_url *url,
                                char *output, size_t capacity) {
    if (url == NULL || output == NULL || capacity == 0U) return false;
    output[0] = '\0';
    size_t used = 0U;
    if (url->scheme == NEXUS_BROWSER_SCHEME_HTTP) {
        if (!url_append_text(output, capacity, &used, "http://")) return false;
    } else if (url->scheme == NEXUS_BROWSER_SCHEME_HTTPS) {
        if (!url_append_text(output, capacity, &used, "https://")) return false;
    } else {
        return false;
    }
    if (!url_append_text(output, capacity, &used, url->host)) return false;
    if (url->explicit_port) {
        char digits[5];
        size_t count = 0U;
        uint16_t value = url->port;
        do {
            digits[count++] = (char)('0' + value % 10U);
            value = (uint16_t)(value / 10U);
        } while (value != 0U && count < sizeof(digits));
        if (!url_append_char(output, capacity, &used, ':')) return false;
        while (count != 0U) {
            if (!url_append_char(output, capacity, &used, digits[--count]))
                return false;
        }
    }
    return url_append_text(output, capacity, &used, url->path);
}

static bool url_normalize_path(const char *input, size_t length,
                               char *output, size_t capacity) {
    if (input == NULL || output == NULL || capacity < 2U ||
        length == 0U || input[0] != '/') return false;
    size_t segment_starts[NEXUS_BROWSER_PATH_MAX + 1U];
    size_t segment_count = 0U;
    size_t used = 1U;
    output[0] = '/';
    output[1] = '\0';

    bool trailing_slash = input[length - 1U] == '/';
    if (!trailing_slash) {
        size_t last = length;
        while (last > 0U && input[last - 1U] != '/') --last;
        const size_t last_length = length - last;
        trailing_slash =
            (last_length == 1U && input[last] == '.') ||
            (last_length == 2U && input[last] == '.' &&
             input[last + 1U] == '.');
    }

    size_t cursor = 1U;
    while (cursor < length) {
        const size_t start = cursor;
        while (cursor < length && input[cursor] != '/') ++cursor;
        const size_t segment_length = cursor - start;
        const bool dot = segment_length == 1U && input[start] == '.';
        const bool dot_dot = segment_length == 2U &&
            input[start] == '.' && input[start + 1U] == '.';
        if (dot_dot) {
            if (segment_count != 0U)
                used = segment_starts[--segment_count];
        } else if (segment_length != 0U && !dot) {
            if (used > 1U && output[used - 1U] != '/') {
                if (used >= capacity - 1U) return false;
                output[used++] = '/';
            }
            if (segment_count >= NEXUS_BROWSER_PATH_MAX ||
                segment_length > capacity - used - 1U) return false;
            segment_starts[segment_count++] = used;
            for (size_t i = 0U; i < segment_length; ++i)
                output[used++] = input[start + i];
            output[used] = '\0';
        }
        if (cursor < length) ++cursor;
    }
    if (trailing_slash && used > 1U && output[used - 1U] != '/') {
        if (used >= capacity - 1U) return false;
        output[used++] = '/';
        output[used] = '\0';
    }
    return true;
}

bool nexus_browser_url_resolve(const char *base_url, const char *reference,
                               char *output, size_t output_capacity) {
    if (base_url == NULL || reference == NULL || output == NULL ||
        output_capacity == 0U) return false;
    output[0] = '\0';

    size_t reference_length = 0U;
    while (reference[reference_length] != '\0') {
        ++reference_length;
        if (reference_length > NEXUS_BROWSER_HOST_MAX +
                              NEXUS_BROWSER_PATH_MAX + 24U) return false;
    }
    if (reference_length == 0U) return false;
    for (size_t i = 0U; i < reference_length; ++i) {
        const unsigned char c = (unsigned char)reference[i];
        if (reference[i] == '#') break;
        if (c == '\\' || c <= 0x20U || c > 0x7EU) return false;
    }

    struct nexus_browser_url base;
    if (!nexus_browser_url_parse(base_url, &base)) return false;

    if ((reference_length >= 7U &&
         reference[0] == 'h' && reference[1] == 't' &&
         reference[2] == 't' && reference[3] == 'p' &&
         reference[4] == ':' && reference[5] == '/' &&
         reference[6] == '/') ||
        (reference_length >= 8U &&
         reference[0] == 'h' && reference[1] == 't' &&
         reference[2] == 't' && reference[3] == 'p' &&
         reference[4] == 's' && reference[5] == ':' &&
         reference[6] == '/' && reference[7] == '/')) {
        struct nexus_browser_url absolute;
        return nexus_browser_url_parse(reference, &absolute) &&
               url_format_absolute(&absolute, output, output_capacity);
    }
    if (reference_length >= 2U && reference[0] == '/' &&
        reference[1] == '/') {
        char absolute_text[NEXUS_BROWSER_HOST_MAX +
                           NEXUS_BROWSER_PATH_MAX + 24U] = {0};
        size_t used = 0U;
        const char *scheme = base.scheme == NEXUS_BROWSER_SCHEME_HTTP
            ? "http:" : "https:";
        if (!url_append_text(absolute_text, sizeof(absolute_text),
                             &used, scheme) ||
            !url_append_text(absolute_text, sizeof(absolute_text),
                             &used, reference)) return false;
        struct nexus_browser_url absolute;
        return nexus_browser_url_parse(absolute_text, &absolute) &&
               url_format_absolute(&absolute, output, output_capacity);
    }

    size_t fragment_end = 0U;
    while (fragment_end < reference_length &&
           reference[fragment_end] != '#') ++fragment_end;
    if (fragment_end == 0U) return false;
    size_t query_start = fragment_end;
    for (size_t i = 0U; i < fragment_end; ++i) {
        if (reference[i] == '?') {
            query_start = i;
            break;
        }
        if (reference[i] == ':') return false;
    }
    const size_t reference_path_length =
        query_start == fragment_end ? fragment_end : query_start;

    size_t base_path_length = 0U;
    while (base.path[base_path_length] != '\0' &&
           base.path[base_path_length] != '?') ++base_path_length;
    char raw_path[(NEXUS_BROWSER_PATH_MAX + 1U) * 2U];
    size_t raw_length = 0U;

    if (reference_path_length == 0U && reference[0] == '?') {
        if (base_path_length >= sizeof(raw_path)) return false;
        for (size_t i = 0U; i < base_path_length; ++i)
            raw_path[raw_length++] = base.path[i];
    } else if (reference_path_length != 0U && reference[0] == '/') {
        if (reference_path_length >= sizeof(raw_path)) return false;
        for (size_t i = 0U; i < reference_path_length; ++i)
            raw_path[raw_length++] = reference[i];
    } else {
        size_t directory_length = base_path_length;
        while (directory_length > 0U &&
               base.path[directory_length - 1U] != '/') --directory_length;
        if (directory_length + reference_path_length >= sizeof(raw_path))
            return false;
        for (size_t i = 0U; i < directory_length; ++i)
            raw_path[raw_length++] = base.path[i];
        for (size_t i = 0U; i < reference_path_length; ++i)
            raw_path[raw_length++] = reference[i];
    }
    raw_path[raw_length] = '\0';

    char normalized_path[NEXUS_BROWSER_PATH_MAX + 1U];
    if (!url_normalize_path(raw_path, raw_length, normalized_path,
                            sizeof(normalized_path))) return false;

    char final_path[NEXUS_BROWSER_PATH_MAX + 1U];
    size_t final_length = 0U;
    for (size_t i = 0U; normalized_path[i] != '\0'; ++i)
        final_path[final_length++] = normalized_path[i];
    if (query_start < fragment_end) {
        const size_t query_length = fragment_end - query_start;
        if (query_length > sizeof(final_path) - final_length - 1U)
            return false;
        for (size_t i = 0U; i < query_length; ++i)
            final_path[final_length++] = reference[query_start + i];
    }
    final_path[final_length] = '\0';
    for (size_t i = 0U; i <= final_length; ++i)
        base.path[i] = final_path[i];
    return url_format_absolute(&base, output, output_capacity);
}
