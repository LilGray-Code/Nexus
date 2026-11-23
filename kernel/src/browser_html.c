#include <stdbool.h>
#include "browser_html.h"

static bool html_equal_ci(const uint8_t *data, size_t length,
                          const char *word) {
    size_t i = 0U;
    while (word[i] != '\0') {
        if (i >= length) return false;
        uint8_t c = data[i];
        if (c >= 'A' && c <= 'Z') c = (uint8_t)(c + ('a' - 'A'));
        if (c != (uint8_t)word[i]) return false;
        ++i;
    }
    return true;
}

static bool html_space(uint8_t c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' ||
           c == '\f';
}

static bool html_append(char *out, size_t capacity, size_t *used,
                        uint8_t c, bool *pending_space) {
    if (html_space(c)) {
        if (*used != 0U) *pending_space = true;
        return true;
    }
    if (*pending_space && *used + 1U < capacity) out[(*used)++] = ' ';
    *pending_space = false;
    if (*used + 1U >= capacity) return false;
    out[(*used)++] = (c >= 0x20U && c <= 0x7eU) ? (char)c : ' ';
    return true;
}

static size_t html_decode_entity(const uint8_t *data, size_t length,
                                 uint8_t *decoded) {
    if (length == 0U) return 0U;
    if (data[0] == '#') {
        size_t i = 1U;
        unsigned base = 10U;
        if (i < length && (data[i] == 'x' || data[i] == 'X')) {
            base = 16U;
            ++i;
        }
        if (i == length) return 0U;
        unsigned value = 0U;
        for (; i < length; ++i) {
            unsigned digit;
            uint8_t c = data[i];
            if (c >= '0' && c <= '9') digit = (unsigned)(c - '0');
            else if (base == 16U && c >= 'a' && c <= 'f')
                digit = (unsigned)(c - 'a') + 10U;
            else if (base == 16U && c >= 'A' && c <= 'F')
                digit = (unsigned)(c - 'A') + 10U;
            else return 0U;
            if (digit >= base || value > (255U - digit) / base) return 0U;
            value = value * base + digit;
        }
        if (value >= 0x20U && value <= 0x7eU) {
            *decoded = (uint8_t)value;
            return 1U;
        }
        if (value == 9U || value == 10U || value == 13U) {
            *decoded = ' ';
            return 1U;
        }
        return 0U;
    }
    if (html_equal_ci(data, length, "amp")) *decoded = '&';
    else if (html_equal_ci(data, length, "lt")) *decoded = '<';
    else if (html_equal_ci(data, length, "gt")) *decoded = '>';
    else if (html_equal_ci(data, length, "quot")) *decoded = '"';
    else if (html_equal_ci(data, length, "apos") ||
             html_equal_ci(data, length, "#39")) *decoded = '\'';
    else if (html_equal_ci(data, length, "nbsp")) *decoded = ' ';
    else return 0U;
    return 1U;
}

size_t nexus_browser_html_to_text(const uint8_t *html, size_t length,
                                  char *output, size_t capacity) {
    if (output == NULL || capacity == 0U) return 0U;
    output[0] = '\0';
    if (html == NULL) return 0U;
    size_t used = 0U;
    bool pending_space = false;
    for (size_t i = 0U; i < length;) {
        if (html[i] == '<') {
            if (i + 3U < length && html[i + 1U] == '!' &&
                html[i + 2U] == '-' && html[i + 3U] == '-') {
                i += 4U;
                while (i + 2U < length &&
                       !(html[i] == '-' && html[i + 1U] == '-' &&
                         html[i + 2U] == '>')) ++i;
                if (i + 2U < length) i += 3U;
                pending_space = used != 0U;
                continue;
            }
            size_t tag_start = i + 1U;
            while (tag_start < length && html_space(html[tag_start]))
                ++tag_start;
            size_t name_length = 0U;
            while (tag_start + name_length < length) {
                uint8_t c = html[tag_start + name_length];
                if (!((c >= 'a' && c <= 'z') ||
                      (c >= 'A' && c <= 'Z'))) break;
                ++name_length;
            }
            /* Match complete tag names: <scripture> is not <script>, and
             * must never cause the rest of a page to be discarded. */
            const bool is_script = name_length == 6U &&
                html_equal_ci(html + tag_start, name_length, "script");
            const bool is_style = name_length == 5U &&
                html_equal_ci(html + tag_start, name_length, "style");
            const bool hidden = is_script || is_style;
            i = tag_start + name_length;
            while (i < length && html[i] != '>') ++i;
            if (i < length) ++i;
            if (hidden && name_length != 0U) {
                const char *close = is_script ? "</script" : "</style";
                size_t close_len = is_script ? 8U : 7U;
                while (i + close_len <= length &&
                       !html_equal_ci(html + i, close_len, close)) ++i;
                if (i + close_len <= length) {
                    i += close_len;
                    while (i < length && html[i] != '>') ++i;
                    if (i < length) ++i;
                } else i = length;
            }
            pending_space = used != 0U;
            continue;
        }
        if (html[i] == '&') {
            size_t end = i + 1U;
            while (end < length && end - i <= 16U &&
                   html[end] != ';' && html[end] != '<' &&
                   !html_space(html[end])) ++end;
            if (end < length && html[end] == ';' &&
                end > i + 1U && end - i <= 16U) {
                uint8_t decoded;
                if (html_decode_entity(html + i + 1U,
                                       end - i - 1U, &decoded) != 0U) {
                    if (!html_append(output, capacity, &used, decoded,
                                     &pending_space)) break;
                } else {
                    if (!html_append(output, capacity, &used, '&',
                                     &pending_space)) break;
                    for (size_t j = i + 1U; j <= end; ++j)
                        if (!html_append(output, capacity, &used, html[j],
                                         &pending_space)) break;
                }
                i = end + 1U;
                continue;
            }
        }
        if (!html_append(output, capacity, &used, html[i], &pending_space))
            break;
        ++i;
    }
    output[used] = '\0';
    return used;
}
