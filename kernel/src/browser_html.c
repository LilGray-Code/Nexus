#include <stdbool.h>
#include "browser_html.h"
#include "browser_url.h"

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

static bool html_append_break(char *out, size_t capacity, size_t *used) {
    if (*used == 0U || out[*used - 1U] == '\n') return true;
    if (*used + 1U >= capacity) return false;
    out[(*used)++] = '\n';
    return true;
}

static bool html_is_block_tag(const uint8_t *name, size_t length) {
    static const char *const tags[] = {
        "address", "article", "blockquote", "div", "dl", "dt", "dd",
        "fieldset", "figcaption", "figure", "footer", "form", "h1",
        "h2", "h3", "h4", "h5", "h6", "header", "hr", "main",
        "nav", "ol", "p", "pre", "section", "table", "tbody",
        "td", "tfoot", "th", "thead", "tr", "ul"
    };
    for (size_t i = 0U; i < sizeof(tags) / sizeof(tags[0]); ++i) {
        size_t n = 0U;
        while (tags[i][n] != '\0') ++n;
        if (n == length && html_equal_ci(name, length, tags[i]))
            return true;
    }
    return false;
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
                pending_space = used != 0U && output[used - 1U] != '\n';
                continue;
            }
            size_t tag_start = i + 1U;
            while (tag_start < length && html_space(html[tag_start]))
                ++tag_start;
            const bool closing_tag =
                tag_start < length && html[tag_start] == '/';
            if (closing_tag) ++tag_start;
            size_t name_length = 0U;
            while (tag_start + name_length < length) {
                uint8_t c = html[tag_start + name_length];
                if (!((c >= 'a' && c <= 'z') ||
                      (c >= 'A' && c <= 'Z') ||
                      (c >= '0' && c <= '9') || c == '-')) break;
                ++name_length;
            }
            /* Match complete tag names: <scripture> is not <script>, and
             * must never cause the rest of a page to be discarded. */
            const bool is_script = name_length == 6U &&
                html_equal_ci(html + tag_start, name_length, "script");
            const bool is_style = name_length == 5U &&
                html_equal_ci(html + tag_start, name_length, "style");
            const bool hidden = is_script || is_style;
            const bool list_item = name_length == 2U &&
                html_equal_ci(html + tag_start, name_length, "li");
            const bool void_break = name_length == 2U &&
                (html_equal_ci(html + tag_start, name_length, "br") ||
                 html_equal_ci(html + tag_start, name_length, "hr"));
            const bool block = html_is_block_tag(html + tag_start,
                                                  name_length) ||
                list_item || void_break;

            /* Block boundaries belong after the previous block (closing
             * tags) or before the next block's content (opening tags).
             * Void breaks such as <br> and <hr> emit their break after the
             * tag. List prefixes stay on the same line as their item text. */
            if (block && (closing_tag || !void_break)) {
                if (!html_append_break(output, capacity, &used)) break;
                pending_space = false;
            }
            if (!closing_tag && list_item) {
                if (used + 2U >= capacity) break;
                output[used++] = '-';
                output[used++] = ' ';
            }
            i = tag_start + name_length;
            while (i < length && html[i] != '>') ++i;
            if (i < length) ++i;
            if (hidden && name_length != 0U) {
                const char *close = is_script ? "</script" : "</style";
                size_t close_len = is_script ? 8U : 7U;
                while (i + close_len <= length) {
                    const size_t after = i + close_len;
                    const bool name_ends = after == length ||
                        html_space(html[after]) || html[after] == '>' ||
                        html[after] == '/';
                    if (name_ends &&
                        html_equal_ci(html + i, close_len, close)) break;
                    ++i;
                }
                if (i + close_len <= length) {
                    i += close_len;
                    while (i < length && html[i] != '>') ++i;
                    if (i < length) ++i;
                } else i = length;
            }
            if (void_break && !closing_tag) {
                if (!html_append_break(output, capacity, &used)) break;
                pending_space = false;
            }
            if (block) pending_space = false;
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
    while (used != 0U &&
           (output[used - 1U] == '\n' || output[used - 1U] == ' '))
        --used;
    output[used] = '\0';
    return used;
}


static bool html_is_http_link(const uint8_t *value, size_t length) {
    return (length >= 7U &&
            value[0] == 'h' && value[1] == 't' && value[2] == 't' &&
            value[3] == 'p' && value[4] == ':' &&
            value[5] == '/' && value[6] == '/') ||
           (length >= 8U &&
            value[0] == 'h' && value[1] == 't' && value[2] == 't' &&
            value[3] == 'p' && value[4] == 's' && value[5] == ':' &&
            value[6] == '/' && value[7] == '/');
}

static bool html_link_contains_control_or_space(const uint8_t *value,
                                                size_t length) {
    for (size_t i = 0U; i < length; ++i) {
        if (value[i] <= 0x20U || value[i] > 0x7EU) return true;
    }
    return false;
}

size_t nexus_browser_html_extract_links_with_base(
    const uint8_t *html, size_t length, const char *base_url,
    char (*links)[NEXUS_BROWSER_HTML_LINK_MAX], size_t link_capacity) {
    if (links == NULL || link_capacity == 0U) return 0U;
    for (size_t i = 0U; i < link_capacity; ++i) links[i][0] = '\0';
    if (html == NULL) return 0U;
    size_t count = 0U;

    for (size_t i = 0U; i < length && count < link_capacity; ++i) {
        if (html[i] != '<') continue;
        size_t tag_start = i + 1U;
        while (tag_start < length && html_space(html[tag_start]))
            ++tag_start;
        if (tag_start >= length || html[tag_start] == '/' ||
            html[tag_start] == '!' || html[tag_start] == '?') continue;
        if (tag_start + 1U >= length ||
            (html[tag_start] != 'a' && html[tag_start] != 'A') ||
            !(html_space(html[tag_start + 1U]) ||
              html[tag_start + 1U] == '>' ||
              html[tag_start + 1U] == '/')) continue;

        size_t tag_end = tag_start + 1U;
        uint8_t quote = 0U;
        while (tag_end < length) {
            const uint8_t c = html[tag_end];
            if (quote != 0U) {
                if (c == quote) quote = 0U;
            } else if (c == '"' || c == '\'') {
                quote = c;
            } else if (c == '>') {
                break;
            }
            ++tag_end;
        }
        if (tag_end >= length) break;

        size_t cursor = tag_start + 1U;
        while (cursor < tag_end && count < link_capacity) {
            while (cursor < tag_end &&
                   (html_space(html[cursor]) || html[cursor] == '/'))
                ++cursor;
            const size_t key_start = cursor;
            while (cursor < tag_end &&
                   !html_space(html[cursor]) && html[cursor] != '=' &&
                   html[cursor] != '/' && html[cursor] != '>') ++cursor;
            const size_t key_length = cursor - key_start;
            if (key_length == 0U) {
                if (cursor < tag_end) ++cursor;
                continue;
            }
            while (cursor < tag_end && html_space(html[cursor])) ++cursor;
            if (cursor >= tag_end || html[cursor] != '=') continue;
            ++cursor;
            while (cursor < tag_end && html_space(html[cursor])) ++cursor;
            if (cursor >= tag_end) break;

            const bool quoted = html[cursor] == '"' || html[cursor] == '\'';
            const uint8_t delimiter = quoted ? html[cursor++] : 0U;
            const size_t value_start = cursor;
            if (quoted) {
                while (cursor < tag_end && html[cursor] != delimiter) ++cursor;
            } else {
                while (cursor < tag_end && !html_space(html[cursor]) &&
                       html[cursor] != '>') ++cursor;
            }
            const size_t value_length = cursor - value_start;
            if (quoted && cursor < tag_end && html[cursor] == delimiter)
                ++cursor;

            if (!html_equal_ci(html + key_start, key_length, "href") ||
                value_length == 0U ||
                value_length >= NEXUS_BROWSER_HTML_LINK_MAX) continue;

            char candidate[NEXUS_BROWSER_HTML_LINK_MAX];
            size_t candidate_length = 0U;
            for (size_t n = 0U; n < value_length;) {
                uint8_t decoded = html[value_start + n];
                size_t consumed = 1U;
                if (decoded == '&') {
                    size_t entity_end = n + 1U;
                    while (entity_end < value_length &&
                           entity_end - n <= 16U &&
                           html[value_start + entity_end] != ';' &&
                           html[value_start + entity_end] != '&') ++entity_end;
                    if (entity_end < value_length &&
                        html[value_start + entity_end] == ';' &&
                        html_decode_entity(html + value_start + n + 1U,
                            entity_end - n - 1U, &decoded) != 0U) {
                        consumed = entity_end - n + 1U;
                    } else {
                        decoded = '&';
                    }
                }
                if (candidate_length + 1U >= sizeof(candidate)) {
                    candidate_length = 0U;
                    break;
                }
                candidate[candidate_length++] = (char)decoded;
                n += consumed;
            }
            if (candidate_length == 0U ||
                html_link_contains_control_or_space(
                    (const uint8_t *)candidate, candidate_length)) continue;
            candidate[candidate_length] = '\0';

            char resolved[NEXUS_BROWSER_HTML_LINK_MAX];
            const bool absolute = html_is_http_link(
                (const uint8_t *)candidate, candidate_length);
            if (absolute) {
                if (!nexus_browser_url_resolve(candidate, candidate,
                        resolved, sizeof(resolved))) continue;
            } else {
                if (base_url == NULL ||
                    !nexus_browser_url_resolve(base_url, candidate,
                        resolved, sizeof(resolved))) continue;
            }
            candidate_length = 0U;
            while (resolved[candidate_length] != '\0' &&
                   candidate_length + 1U < sizeof(candidate)) {
                candidate[candidate_length] = resolved[candidate_length];
                ++candidate_length;
            }
            if (resolved[candidate_length] != '\0' || candidate_length == 0U)
                continue;
            candidate[candidate_length] = '\0';
            bool duplicate = false;
            for (size_t n = 0U; n < count; ++n) {
                size_t existing_length = 0U;
                while (existing_length < NEXUS_BROWSER_HTML_LINK_MAX &&
                       links[n][existing_length] != '\0') ++existing_length;
                if (existing_length == candidate_length) {
                    size_t match = 0U;
                    while (match < candidate_length &&
                           links[n][match] == candidate[match]) ++match;
                    if (match == candidate_length) duplicate = true;
                }
            }
            if (!duplicate) {
                for (size_t n = 0U; n <= candidate_length; ++n)
                    links[count][n] = candidate[n];
                ++count;
            }
        }
        i = tag_end;
    }
    return count;
}

size_t nexus_browser_html_extract_links(
    const uint8_t *html, size_t length,
    char (*links)[NEXUS_BROWSER_HTML_LINK_MAX], size_t link_capacity) {
    return nexus_browser_html_extract_links_with_base(
        html, length, NULL, links, link_capacity);
}
