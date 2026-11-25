#include "download.h"

#include <stdint.h>

static char upper_ascii(char value) {
    if (value >= 'a' && value <= 'z')
        return (char)(value - 'a' + 'A');
    return value;
}

static char lower_ascii(char value) {
    if (value >= 'A' && value <= 'Z')
        return (char)(value - 'A' + 'a');
    return value;
}

static bool equal_ci(const char *left, const char *right) {
    if (left == NULL || right == NULL) return false;
    size_t i = 0U;
    while (left[i] != '\0' && right[i] != '\0') {
        if (lower_ascii(left[i]) != lower_ascii(right[i])) return false;
        ++i;
    }
    return left[i] == '\0' && right[i] == '\0';
}

static int hex_digit(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

static bool filename_from_disposition(const char *header,
                                      char *candidate,
                                      size_t capacity,
                                      size_t *length) {
    if (header == NULL || candidate == NULL || capacity == 0U ||
        length == NULL) return false;

    for (size_t i = 0U; header[i] != '\0'; ++i) {
        if (lower_ascii(header[i]) != 'f') continue;
        static const char key[] = "filename=";
        size_t k = 0U;
        while (key[k] != '\0' && header[i + k] != '\0' &&
               lower_ascii(header[i + k]) == key[k]) ++k;
        if (key[k] != '\0') continue;

        size_t start = i + k;
        if (header[start] == '"') ++start;
        size_t end = start;
        while (header[end] != '\0' && header[end] != ';' &&
               header[end] != '"' && header[end] != '\r' &&
               header[end] != '\n') ++end;
        if (end == start || end - start >= capacity) return false;
        *length = end - start;
        for (size_t n = 0U; n < *length; ++n)
            candidate[n] = header[start + n];
        candidate[*length] = '\0';
        return true;
    }
    return false;
}

static bool filename_from_url(const char *url, char *candidate,
                              size_t capacity, size_t *length) {
    if (url == NULL || candidate == NULL || capacity == 0U ||
        length == NULL) return false;
    size_t path_start = 0U;
    for (size_t i = 0U; url[i] != '\0'; ++i) {
        if (url[i] == ':' && url[i + 1U] == '/' &&
            url[i + 2U] == '/') {
            path_start = i + 3U;
            break;
        }
    }

    size_t path_end = path_start;
    while (url[path_end] != '\0' && url[path_end] != '?' &&
           url[path_end] != '#') ++path_end;
    size_t base = path_end;
    while (base > path_start && url[base - 1U] != '/') --base;
    if (base == path_end) return false;

    *length = path_end - base;
    if (*length == 0U || *length >= capacity) return false;
    for (size_t i = 0U; i < *length; ++i)
        candidate[i] = url[base + i];
    candidate[*length] = '\0';
    return true;
}

static bool content_type_is(const char *actual, const char *expected) {
    if (actual == NULL || expected == NULL) return false;
    size_t i = 0U;
    while (expected[i] != '\0') {
        if (actual[i] == '\0' || lower_ascii(actual[i]) != expected[i])
            return false;
        ++i;
    }
    return actual[i] == '\0' || actual[i] == ';' ||
           actual[i] == ' ' || actual[i] == '\t';
}

static const char *default_extension(const char *content_type) {
    if (content_type == NULL) return ".BIN";
    if (content_type_is(content_type, "application/pdf") ||
        content_type_is(content_type, "application/x-pdf")) return ".PDF";
    if (content_type_is(content_type, "application/zip") ||
        content_type_is(content_type, "application/x-zip-compressed")) return ".ZIP";
    if (content_type_is(content_type, "application/json")) return ".JSON";
    if (content_type_is(content_type, "text/plain")) return ".TXT";
    if (content_type_is(content_type, "text/html") ||
        content_type_is(content_type, "application/xhtml+xml")) return ".HTML";
    if (content_type_is(content_type, "image/png")) return ".PNG";
    if (content_type_is(content_type, "image/jpeg")) return ".JPG";
    if (content_type_is(content_type, "image/gif")) return ".GIF";
    if (content_type_is(content_type, "image/webp")) return ".WEBP";
    if (content_type_is(content_type, "application/x-msdownload") ||
        content_type_is(content_type,
                        "application/vnd.microsoft.portable-executable"))
        return ".EXE";
    return ".BIN";
}

bool nexus_download_filename(const char *url,
                             const char *content_disposition,
                             const char *content_type,
                             char output[NEXUS_DOWNLOAD_NAME_MAX]) {
    if (output == NULL) return false;
    char candidate[192] = {0};
    size_t candidate_length = 0U;
    bool found = filename_from_disposition(
        content_disposition, candidate, sizeof(candidate),
        &candidate_length);
    if (!found) {
        found = filename_from_url(url, candidate, sizeof(candidate),
                                  &candidate_length);
    }
    if (!found) {
        static const char fallback[] = "DOWNLOAD";
        candidate_length = sizeof(fallback) - 1U;
        for (size_t i = 0U; i < candidate_length; ++i)
            candidate[i] = fallback[i];
    }

    /* Content-Disposition may include a path. Always retain the final leaf. */
    size_t leaf = 0U;
    for (size_t i = 0U; i < candidate_length; ++i) {
        if (candidate[i] == '/' || candidate[i] == '\\') leaf = i + 1U;
    }

    char safe[NEXUS_DOWNLOAD_NAME_MAX] = {0};
    size_t safe_length = 0U;
    bool last_was_dot = false;
    for (size_t i = leaf; i < candidate_length &&
         safe_length < NEXUS_DOWNLOAD_NAME_MAX - 1U; ++i) {
        char value = candidate[i];
        if (value == '%' && i + 2U < candidate_length) {
            const int high = hex_digit(candidate[i + 1U]);
            const int low = hex_digit(candidate[i + 2U]);
            if (high >= 0 && low >= 0) {
                value = (char)((high << 4) | low);
                i += 2U;
            } else {
                value = '_';
            }
        }
        const bool valid = (value >= 'a' && value <= 'z') ||
                           (value >= 'A' && value <= 'Z') ||
                           (value >= '0' && value <= '9') ||
                           value == '_' || value == '-' || value == '.';
        if (!valid) value = '_';
        if (value == '.' && (safe_length == 0U || last_was_dot)) {
            value = '_';
        }
        safe[safe_length++] = upper_ascii(value);
        last_was_dot = value == '.';
    }
    while (safe_length != 0U && safe[safe_length - 1U] == '.')
        safe[--safe_length] = '\0';
    if (safe_length == 0U) {
        safe[0] = 'D'; safe[1] = 'O'; safe[2] = 'W'; safe[3] = 'N';
        safe[4] = 'L'; safe[5] = 'O'; safe[6] = 'A'; safe[7] = 'D';
        safe_length = 8U;
    }

    bool has_extension = false;
    for (size_t i = 1U; i < safe_length; ++i) {
        if (safe[i] == '.') has_extension = true;
    }
    if (!has_extension) {
        const char *extension = default_extension(content_type);
        size_t ext_length = 0U;
        while (extension[ext_length] != '\0') ++ext_length;
        if (safe_length + ext_length >= NEXUS_DOWNLOAD_NAME_MAX) {
            safe_length = NEXUS_DOWNLOAD_NAME_MAX - 1U - ext_length;
            safe[safe_length] = '\0';
        }
        for (size_t i = 0U; i < ext_length; ++i)
            safe[safe_length++] = extension[i];
        safe[safe_length] = '\0';
    }

    for (size_t i = 0U; i < NEXUS_DOWNLOAD_NAME_MAX; ++i) {
        output[i] = i < safe_length ? safe[i] : '\0';
    }
    return safe_length != 0U;
}

bool nexus_download_unique_name(const char *base_name, uint32_t sequence,
                                char output[NEXUS_DOWNLOAD_NAME_MAX]) {
    if (base_name == NULL || output == NULL) return false;
    size_t length = 0U;
    while (length < NEXUS_DOWNLOAD_NAME_MAX &&
           base_name[length] != '\0') ++length;
    if (length == 0U || length >= NEXUS_DOWNLOAD_NAME_MAX) return false;

    if (sequence == 0U) {
        for (size_t i = 0U; i <= length; ++i) output[i] = base_name[i];
        return true;
    }

    size_t extension = length;
    for (size_t i = 1U; i < length; ++i) {
        if (base_name[i] == '.') extension = i;
    }

    char digits[11];
    size_t digit_count = 0U;
    uint32_t value = sequence;
    do {
        digits[digit_count++] = (char)('0' + value % 10U);
        value /= 10U;
    } while (value != 0U && digit_count < sizeof(digits));

    const size_t suffix_length = digit_count + 1U;
    const size_t extension_length = length - extension;
    if (suffix_length + extension_length >= NEXUS_DOWNLOAD_NAME_MAX)
        return false;
    size_t stem_length = extension;
    const size_t stem_capacity = NEXUS_DOWNLOAD_NAME_MAX - 1U -
                                 suffix_length - extension_length;
    if (stem_length > stem_capacity) stem_length = stem_capacity;
    if (stem_length == 0U) return false;

    size_t used = 0U;
    for (size_t i = 0U; i < stem_length; ++i)
        output[used++] = base_name[i];
    output[used++] = '_';
    while (digit_count != 0U)
        output[used++] = digits[--digit_count];
    for (size_t i = extension; i < length; ++i)
        output[used++] = base_name[i];
    output[used] = '\0';
    return true;
}
