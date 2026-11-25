#include "browser_search.h"

#include "html_text.h"

#include <stdint.h>

static char lower_ascii(char value) {
    if (value >= 'A' && value <= 'Z')
        return (char)(value - 'A' + 'a');
    return value;
}

static bool span_equal_ci(const char *left, size_t left_length,
                          const char *right) {
    size_t i = 0U;
    while (right[i] != '\0') {
        if (i >= left_length ||
            lower_ascii(left[i]) != lower_ascii(right[i])) return false;
        ++i;
    }
    return i == left_length;
}

static bool bytes_equal_ci(const uint8_t *left, size_t left_length,
                           const char *right) {
    size_t i = 0U;
    while (right[i] != '\0') {
        if (i >= left_length ||
            lower_ascii((char)left[i]) != lower_ascii(right[i])) return false;
        ++i;
    }
    return i == left_length;
}

static size_t find_ci(const uint8_t *data, size_t length, size_t start,
                      const char *needle) {
    size_t needle_length = 0U;
    while (needle[needle_length] != '\0') ++needle_length;
    if (data == NULL || needle_length == 0U || start > length ||
        needle_length > length - start) return SIZE_MAX;
    for (size_t i = start; i <= length - needle_length; ++i) {
        if (bytes_equal_ci(data + i, needle_length, needle)) return i;
    }
    return SIZE_MAX;
}

static bool append_byte(char *output, size_t capacity, size_t *length,
                        char value) {
    if (output == NULL || length == NULL || *length >= capacity)
        return false;
    output[(*length)++] = value;
    return true;
}

static bool encode_query(const char *query, size_t query_length,
                          char *output, size_t capacity,
                          size_t *output_length) {
    static const char hex[] = "0123456789ABCDEF";
    if (query == NULL || query_length == 0U || output == NULL ||
        output_length == NULL || capacity == 0U) return false;
    size_t used = 0U;
    for (size_t i = 0U; i < query_length; ++i) {
        const uint8_t ch = (uint8_t)query[i];
        if (ch < 0x20U || ch >= 0x7FU) return false;
        const bool unreserved =
            (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' ||
            ch == '.' || ch == '~';
        if (unreserved) {
            if (!append_byte(output, capacity, &used, (char)ch))
                return false;
        } else {
            if (!append_byte(output, capacity, &used, '%') ||
                !append_byte(output, capacity, &used,
                             hex[(ch >> 4) & 0x0FU]) ||
                !append_byte(output, capacity, &used,
                             hex[ch & 0x0FU])) return false;
        }
    }
    if (used >= capacity) return false;
    output[used] = '\0';
    *output_length = used;
    return true;
}

bool nexus_browser_search_build_path(
    const char *query, size_t query_length,
    char *path, size_t path_capacity, size_t *path_length) {
    static const char prefix[] = "/html/?q=";
    if (path == NULL || path_length == NULL || path_capacity == 0U)
        return false;
    size_t prefix_length = sizeof(prefix) - 1U;
    if (prefix_length >= path_capacity) return false;
    for (size_t i = 0U; i < prefix_length; ++i) path[i] = prefix[i];
    size_t encoded_length = 0U;
    if (!encode_query(query, query_length, path + prefix_length,
                      path_capacity - prefix_length, &encoded_length))
        return false;
    *path_length = prefix_length + encoded_length;
    return true;
}

bool nexus_browser_search_build_form_body(
    const char *query, size_t query_length,
    char *body, size_t body_capacity, size_t *body_length) {
    static const char prefix[] = "q=";
    static const char suffix[] = "&b=&kl=us-en";
    if (body == NULL || body_length == NULL || body_capacity == 0U)
        return false;
    const size_t prefix_length = sizeof(prefix) - 1U;
    const size_t suffix_length = sizeof(suffix) - 1U;
    if (prefix_length + suffix_length >= body_capacity) return false;
    for (size_t i = 0U; i < prefix_length; ++i) body[i] = prefix[i];
    size_t encoded_length = 0U;
    if (!encode_query(query, query_length, body + prefix_length,
                      body_capacity - prefix_length - suffix_length,
                      &encoded_length)) return false;
    size_t used = prefix_length + encoded_length;
    for (size_t i = 0U; i < suffix_length; ++i)
        body[used++] = suffix[i];
    body[used] = '\0';
    *body_length = used;
    return true;
}

bool nexus_browser_search_is_challenge(
    const uint8_t *html, size_t html_length) {
    if (html == NULL || html_length == 0U) return false;
    return find_ci(html, html_length, 0U, "anomaly-modal") != SIZE_MAX ||
           find_ci(html, html_length, 0U, "detected unusual traffic") != SIZE_MAX ||
           find_ci(html, html_length, 0U, "automated requests") != SIZE_MAX;
}

static bool find_tag_end(const uint8_t *html, size_t length, size_t start,
                         size_t *end) {
    if (html == NULL || end == NULL || start > length) return false;
    uint8_t quote = 0U;
    for (size_t i = start; i < length; ++i) {
        const uint8_t ch = html[i];
        if (quote != 0U) {
            if (ch == quote) quote = 0U;
        } else if (ch == '"' || ch == '\'') {
            quote = ch;
        } else if (ch == '>') {
            *end = i;
            return true;
        }
    }
    return false;
}

static bool html_space(uint8_t ch) {
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' || ch == '\f';
}

static bool class_has_token(const uint8_t *value, size_t length,
                            const char *token) {
    size_t cursor = 0U;
    while (cursor < length) {
        while (cursor < length && html_space(value[cursor])) ++cursor;
        const size_t start = cursor;
        while (cursor < length && !html_space(value[cursor])) ++cursor;
        if (cursor > start &&
            bytes_equal_ci(value + start, cursor - start, token)) return true;
    }
    return false;
}

static bool extract_anchor_attributes(
    const uint8_t *html, size_t start, size_t tag_end,
    char *href, size_t href_capacity, size_t *href_length,
    bool *is_result_link) {
    if (html == NULL || href == NULL || href_capacity == 0U ||
        href_length == NULL || is_result_link == NULL ||
        tag_end <= start + 2U) return false;

    *href_length = 0U;
    *is_result_link = false;
    size_t cursor = start + 2U;
    while (cursor < tag_end && !html_space(html[cursor]) &&
           html[cursor] != '/' && html[cursor] != '>') ++cursor;

    while (cursor < tag_end) {
        while (cursor < tag_end &&
               (html_space(html[cursor]) || html[cursor] == '/')) ++cursor;
        if (cursor >= tag_end) break;

        const size_t name_start = cursor;
        while (cursor < tag_end && !html_space(html[cursor]) &&
               html[cursor] != '=' && html[cursor] != '/' &&
               html[cursor] != '>') ++cursor;
        if (cursor == name_start) {
            ++cursor;
            continue;
        }
        const size_t name_length = cursor - name_start;
        while (cursor < tag_end && html_space(html[cursor])) ++cursor;
        if (cursor >= tag_end || html[cursor] != '=') continue;
        ++cursor;
        while (cursor < tag_end && html_space(html[cursor])) ++cursor;
        if (cursor >= tag_end) break;

        uint8_t quote = 0U;
        if (html[cursor] == '"' || html[cursor] == '\'')
            quote = html[cursor++];
        const size_t value_start = cursor;
        if (quote != 0U) {
            while (cursor < tag_end && html[cursor] != quote) ++cursor;
        } else {
            while (cursor < tag_end && !html_space(html[cursor])) ++cursor;
        }
        const size_t value_length = cursor - value_start;
        if (quote != 0U && cursor < tag_end) ++cursor;

        if (bytes_equal_ci(html + name_start, name_length, "class")) {
            *is_result_link = class_has_token(
                html + value_start, value_length, "result__a");
        } else if (bytes_equal_ci(html + name_start, name_length, "href")) {
            size_t out = 0U;
            for (size_t i = 0U; i < value_length; ++i) {
                if (html[value_start + i] == '&' && i + 4U < value_length &&
                    bytes_equal_ci(html + value_start + i, 5U, "&amp;")) {
                    if (!append_byte(href, href_capacity, &out, '&'))
                        return false;
                    i += 4U;
                } else {
                    const uint8_t ch = html[value_start + i];
                    if (ch <= 0x20U || ch >= 0x7FU) return false;
                    if (!append_byte(href, href_capacity, &out, (char)ch))
                        return false;
                }
            }
            if (out >= href_capacity) return false;
            href[out] = '\0';
            *href_length = out;
        }
    }
    return *href_length != 0U && *is_result_link;
}

static int hex_value(uint8_t ch) {
    if (ch >= '0' && ch <= '9') return (int)(ch - '0');
    if (ch >= 'a' && ch <= 'f') return (int)(ch - 'a' + 10U);
    if (ch >= 'A' && ch <= 'F') return (int)(ch - 'A' + 10U);
    return -1;
}

static bool starts_with_ci(const char *value, size_t length,
                           const char *prefix) {
    size_t prefix_length = 0U;
    while (prefix[prefix_length] != '\0') ++prefix_length;
    return length >= prefix_length &&
           span_equal_ci(value, prefix_length, prefix);
}

static bool normalize_result_url(const char *input, size_t input_length,
                                 char *output, size_t output_capacity) {
    if (input == NULL || output == NULL || output_capacity == 0U ||
        input_length == 0U) return false;

    char decoded[NEXUS_BROWSER_SEARCH_URL_MAX];
    size_t decoded_length = 0U;
    const size_t ddg_marker = find_ci(
        (const uint8_t *)input, input_length, 0U, "duckduckgo.com/l/");
    if (ddg_marker != SIZE_MAX) {
        const size_t query_marker = find_ci(
            (const uint8_t *)input, input_length, ddg_marker, "uddg=");
        if (query_marker != SIZE_MAX) {
            size_t cursor = query_marker + 5U;
            while (cursor < input_length && input[cursor] != '&') {
                if (input[cursor] == '%') {
                    if (cursor + 2U >= input_length) return false;
                    const int high = hex_value((uint8_t)input[cursor + 1U]);
                    const int low = hex_value((uint8_t)input[cursor + 2U]);
                    if (high < 0 || low < 0) return false;
                    const char ch = (char)((high << 4) | low);
                    if (ch <= 0x20 || (uint8_t)ch >= 0x7FU) return false;
                    if (!append_byte(decoded, sizeof(decoded),
                                     &decoded_length, ch)) return false;
                    cursor += 3U;
                } else {
                    const char ch = input[cursor++];
                    if (ch <= 0x20 || (uint8_t)ch >= 0x7FU) return false;
                    if (!append_byte(decoded, sizeof(decoded),
                                     &decoded_length, ch)) return false;
                }
            }
            if (decoded_length == 0U) return false;
            input = decoded;
            input_length = decoded_length;
        }
    }

    bool scheme_relative = false;
    size_t start = 0U;
    if (input_length >= 2U && input[0] == '/' && input[1] == '/') {
        scheme_relative = true;
        start = 2U;
    } else if (starts_with_ci(input, input_length, "https://")) {
        start = 8U;
    } else if (starts_with_ci(input, input_length, "http://")) {
        start = 7U;
    } else {
        return false;
    }
    if (start >= input_length) return false;
    for (size_t i = start; i < input_length; ++i) {
        const uint8_t ch = (uint8_t)input[i];
        if (ch <= 0x20U || ch >= 0x7FU || ch == '\\') return false;
        if (i == start && (ch == '/' || ch == '?' || ch == '#')) return false;
    }

    size_t output_length = 0U;
    if (scheme_relative) {
        if (!append_byte(output, output_capacity, &output_length, 'h') ||
            !append_byte(output, output_capacity, &output_length, 't') ||
            !append_byte(output, output_capacity, &output_length, 't') ||
            !append_byte(output, output_capacity, &output_length, 'p') ||
            !append_byte(output, output_capacity, &output_length, 's') ||
            !append_byte(output, output_capacity, &output_length, ':'))
            return false;
    }
    if (input_length >= output_capacity - output_length) return false;
    for (size_t i = 0U; i < input_length; ++i)
        output[output_length++] = input[i];
    output[output_length] = '\0';
    return true;
}

static size_t find_anchor_close(const uint8_t *html, size_t length,
                                size_t start) {
    for (size_t i = start; i + 3U < length; ++i) {
        if (html[i] == '<' && html[i + 1U] == '/' &&
            lower_ascii((char)html[i + 2U]) == 'a' &&
            (html[i + 3U] == '>' || html_space(html[i + 3U])))
            return i;
    }
    return SIZE_MAX;
}

static void extract_text(const uint8_t *html, size_t length,
                         char *output, size_t output_capacity) {
    if (output == NULL || output_capacity == 0U) return;
    output[0] = '\0';
    if (html == NULL || length == 0U) return;
    struct nexus_html_text_result result;
    if (!nexus_html_to_text(html, length, output, output_capacity, &result))
        output[0] = '\0';
}

static void extract_result_snippet(const uint8_t *html, size_t length,
                                   size_t start, char *output,
                                   size_t output_capacity) {
    if (output == NULL || output_capacity == 0U) return;
    output[0] = '\0';
    const size_t snippet_marker = find_ci(
        html, length, start, "result__snippet");
    if (snippet_marker == SIZE_MAX) return;
    size_t tag_start = snippet_marker;
    while (tag_start != 0U && html[tag_start] != '<') --tag_start;
    if (html[tag_start] != '<') return;
    size_t tag_end = 0U;
    if (!find_tag_end(html, length, tag_start + 1U, &tag_end)) return;
    const size_t content_start = tag_end + 1U;

    size_t close_div = find_ci(html, length, content_start, "</div");
    size_t close_anchor = find_ci(html, length, content_start, "</a");
    size_t close_span = find_ci(html, length, content_start, "</span");
    size_t close = close_div;
    if (close == SIZE_MAX || (close_anchor != SIZE_MAX && close_anchor < close))
        close = close_anchor;
    if (close == SIZE_MAX || (close_span != SIZE_MAX && close_span < close))
        close = close_span;
    if (close == SIZE_MAX || close <= content_start) return;
    extract_text(html + content_start, close - content_start,
                 output, output_capacity);
}

bool nexus_browser_search_parse(
    const uint8_t *html, size_t html_length,
    struct nexus_browser_search_result *results,
    size_t result_capacity, size_t *result_count) {
    if (html == NULL || html_length == 0U || results == NULL ||
        result_capacity == 0U || result_count == NULL) return false;
    *result_count = 0U;
    if (result_capacity > NEXUS_BROWSER_SEARCH_MAX_RESULTS)
        result_capacity = NEXUS_BROWSER_SEARCH_MAX_RESULTS;

    size_t cursor = 0U;
    while (cursor < html_length && *result_count < result_capacity) {
        const size_t anchor_start = find_ci(html, html_length, cursor, "<a");
        if (anchor_start == SIZE_MAX || anchor_start + 2U >= html_length)
            break;
        if (!html_space(html[anchor_start + 2U]) &&
            html[anchor_start + 2U] != '>' &&
            html[anchor_start + 2U] != '/') {
            cursor = anchor_start + 2U;
            continue;
        }

        size_t tag_end = 0U;
        if (!find_tag_end(html, html_length, anchor_start + 2U, &tag_end))
            break;
        char href[NEXUS_BROWSER_SEARCH_URL_MAX];
        size_t href_length = 0U;
        bool result_link = false;
        const bool attributes_ok = extract_anchor_attributes(
            html, anchor_start, tag_end, href, sizeof(href),
            &href_length, &result_link);
        const size_t content_start = tag_end + 1U;
        const size_t anchor_close = find_anchor_close(
            html, html_length, content_start);
        if (anchor_close == SIZE_MAX) break;
        const size_t after_close = find_ci(
            html, html_length, anchor_close, ">");
        if (after_close == SIZE_MAX) break;
        cursor = after_close + 1U;
        if (!attributes_ok || !result_link) continue;

        struct nexus_browser_search_result candidate = {0};
        if (!normalize_result_url(href, href_length,
                                  candidate.url, sizeof(candidate.url)))
            continue;
        extract_text(html + content_start,
                     anchor_close - content_start,
                     candidate.title, sizeof(candidate.title));
        if (candidate.title[0] == '\0') {
            size_t i = 0U;
            while (i < sizeof(candidate.title) - 1U &&
                   candidate.url[i] != '\0') {
                candidate.title[i] = candidate.url[i];
                ++i;
            }
            candidate.title[i] = '\0';
        }
        extract_result_snippet(html, html_length, cursor,
                               candidate.snippet,
                               sizeof(candidate.snippet));

        bool duplicate = false;
        for (size_t i = 0U; i < *result_count; ++i) {
            size_t a = 0U;
            while (candidate.url[a] != '\0' &&
                   results[i].url[a] != '\0' &&
                   candidate.url[a] == results[i].url[a]) ++a;
            if (candidate.url[a] == '\0' && results[i].url[a] == '\0') {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) results[(*result_count)++] = candidate;
    }
    return *result_count != 0U;
}

size_t nexus_browser_search_result_for_line(size_t document_line) {
    if (document_line < 2U) return SIZE_MAX;
    return (document_line - 2U) / 4U;
}
