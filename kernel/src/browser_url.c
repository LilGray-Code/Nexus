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
