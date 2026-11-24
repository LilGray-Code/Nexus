#include "browser_url.h"

#include <stdint.h>

static char lower_ascii(char value) {
    if (value >= 'A' && value <= 'Z')
        return (char)(value - 'A' + 'a');
    return value;
}

static bool starts_with_ci(const char *value, size_t length,
                           const char *prefix) {
    size_t i = 0U;
    while (prefix[i] != '\0') {
        if (i >= length ||
            lower_ascii(value[i]) != lower_ascii(prefix[i])) return false;
        ++i;
    }
    return true;
}

static bool append_span(char *buffer, size_t capacity, size_t *length,
                        const char *value, size_t value_length) {
    if (buffer == NULL || length == NULL ||
        (value_length != 0U && value == NULL) ||
        *length > capacity || value_length > capacity - *length)
        return false;
    for (size_t i = 0U; i < value_length; ++i)
        buffer[(*length)++] = value[i];
    return true;
}

static bool append_cstr(char *buffer, size_t capacity, size_t *length,
                        const char *value) {
    size_t value_length = 0U;
    if (value == NULL) return false;
    while (value[value_length] != '\0') ++value_length;
    return append_span(buffer, capacity, length, value, value_length);
}

static bool location_has_unknown_scheme(const char *location, size_t length) {
    for (size_t i = 0U; i < length; ++i) {
        if (location[i] == '/') return false;
        if (location[i] == '?' || location[i] == '#') return false;
        if (location[i] == ':') return true;
    }
    return false;
}

bool nexus_browser_resolve_redirect(
    const char *host, size_t host_length, const char *path,
    size_t path_length, bool current_https, const char *location,
    size_t location_length, char *output, size_t output_capacity,
    size_t *output_length) {
    if (host == NULL || path == NULL || location == NULL ||
        output == NULL || output_length == NULL ||
        host_length == 0U || location_length == 0U ||
        output_capacity == 0U) return false;

    size_t target_length = location_length;
    for (size_t i = 0U; i < target_length; ++i) {
        if (location[i] == '#') {
            target_length = i;
            break;
        }
        const unsigned char ch = (unsigned char)location[i];
        if (ch <= 0x20U || ch >= 0x7FU) return false;
    }
    if (target_length == 0U) return false;

    const bool absolute_https =
        starts_with_ci(location, target_length, "https://");
    const bool absolute_http =
        starts_with_ci(location, target_length, "http://");
    if (absolute_https || absolute_http) {
        if (current_https && absolute_http) return false;
        if (target_length >= output_capacity) return false;
        for (size_t i = 0U; i < target_length; ++i)
            output[i] = location[i];
        /* URI schemes are case-insensitive; normalize for the desktop parser. */
        if (absolute_https) {
            output[0] = 'h'; output[1] = 't'; output[2] = 't';
            output[3] = 'p'; output[4] = 's';
        } else {
            output[0] = 'h'; output[1] = 't'; output[2] = 't';
            output[3] = 'p';
        }
        output[target_length] = '\0';
        *output_length = target_length;
        return true;
    }

    if (location_has_unknown_scheme(location, target_length))
        return false;

    const char *scheme = current_https ? "https" : "http";
    const size_t scheme_length = current_https ? 5U : 4U;
    size_t used = 0U;
    if (starts_with_ci(location, target_length, "//")) {
        if (!append_span(output, output_capacity, &used,
                         scheme, scheme_length) ||
            !append_cstr(output, output_capacity, &used, ":") ||
            !append_span(output, output_capacity, &used,
                         location, target_length))
            return false;
    } else {
        if (!append_span(output, output_capacity, &used,
                         scheme, scheme_length) ||
            !append_cstr(output, output_capacity, &used, "://") ||
            !append_span(output, output_capacity, &used,
                         host, host_length))
            return false;

        if (location[0] == '/') {
            if (!append_span(output, output_capacity, &used,
                             location, target_length))
                return false;
        } else if (location[0] == '?') {
            size_t base_length = path_length;
            for (size_t i = 0U; i < base_length; ++i) {
                if (path[i] == '?') {
                    base_length = i;
                    break;
                }
            }
            if (!append_span(output, output_capacity, &used,
                             path, base_length) ||
                !append_span(output, output_capacity, &used,
                             location, target_length))
                return false;
        } else {
            size_t directory_length = path_length;
            for (size_t i = 0U; i < directory_length; ++i) {
                if (path[i] == '?' || path[i] == '#') {
                    directory_length = i;
                    break;
                }
            }
            while (directory_length != 0U &&
                   path[directory_length - 1U] != '/')
                --directory_length;
            if (!append_span(output, output_capacity, &used,
                             path, directory_length) ||
                !append_span(output, output_capacity, &used,
                             location, target_length))
                return false;
        }
    }

    if (used == 0U || used >= output_capacity) return false;
    output[used] = '\0';
    *output_length = used;
    return true;
}
