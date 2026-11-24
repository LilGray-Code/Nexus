#include "html_text.h"

#include <stdint.h>

struct html_output {
    char *buffer;
    size_t capacity;
    size_t length;
    bool pending_space;
    bool truncated;
};

static char lower_ascii(char value) {
    if (value >= 'A' && value <= 'Z')
        return (char)(value - 'A' + 'a');
    return value;
}

static bool equal_span_ci(const uint8_t *data, size_t length,
                          const char *text) {
    size_t i = 0U;
    while (text[i] != '\0') {
        if (i >= length || lower_ascii((char)data[i]) != text[i])
            return false;
        ++i;
    }
    return i == length;
}

static bool html_space(uint32_t value) {
    return value == 0x20U || value == 0x09U || value == 0x0AU ||
           value == 0x0DU || value == 0x0CU || value == 0xA0U;
}

static bool output_byte(struct html_output *out, char value) {
    if (out->length >= out->capacity - 1U) {
        out->truncated = true;
        return false;
    }
    out->buffer[out->length++] = value;
    return true;
}

static bool flush_pending_space(struct html_output *out) {
    if (!out->pending_space) return true;
    out->pending_space = false;
    if (out->length == 0U ||
        out->buffer[out->length - 1U] == '\n' ||
        out->buffer[out->length - 1U] == ' ')
        return true;
    return output_byte(out, ' ');
}

static bool output_codepoint(struct html_output *out, uint32_t value) {
    if (html_space(value)) {
        out->pending_space = true;
        return true;
    }
    if (!flush_pending_space(out)) return false;
    if (value < 0x20U || value > 0x7EU) value = (uint32_t)'?';
    return output_byte(out, (char)value);
}

static bool output_break(struct html_output *out) {
    out->pending_space = false;
    while (out->length != 0U &&
           out->buffer[out->length - 1U] == ' ')
        --out->length;
    if (out->length == 0U || out->buffer[out->length - 1U] == '\n')
        return true;
    return output_byte(out, '\n');
}

static bool output_list_marker(struct html_output *out) {
    if (!output_break(out)) return false;
    return output_byte(out, '-') && output_byte(out, ' ');
}

static bool block_tag(const uint8_t *name, size_t length) {
    static const char *const tags[] = {
        "address", "article", "aside", "blockquote", "body", "caption",
        "dd", "details", "dialog", "div", "dl", "dt", "fieldset",
        "figcaption", "figure", "footer", "form", "h1", "h2", "h3",
        "h4", "h5", "h6", "header", "hr", "li", "main", "menu",
        "nav", "ol", "p", "pre", "section", "summary", "table",
        "tbody", "tfoot", "thead", "tr", "ul"
    };
    for (size_t i = 0U; i < sizeof(tags) / sizeof(tags[0]); ++i) {
        if (equal_span_ci(name, length, tags[i])) return true;
    }
    return false;
}

static bool decode_named_entity(const uint8_t *name, size_t length,
                                uint32_t *value) {
    struct entity {
        const char *name;
        uint32_t value;
    };
    static const struct entity entities[] = {
        {"amp", '&'}, {"lt", '<'}, {"gt", '>'}, {"quot", '"'},
        {"apos", '\''}, {"nbsp", 0x20U}, {"copy", 0xA9U},
        {"reg", 0xAEU}, {"trade", 0x2122U}, {"ndash", 0x2013U},
        {"mdash", 0x2014U}, {"hellip", 0x2026U}, {"bull", 0x2022U},
        {"euro", 0x20ACU}, {"pound", 0x00A3U}, {"cent", 0x00A2U},
        {"yen", 0x00A5U}, {"lsquo", 0x2018U}, {"rsquo", 0x2019U},
        {"ldquo", 0x201CU}, {"rdquo", 0x201DU}, {"times", 0x00D7U},
        {"divide", 0x00F7U}
    };
    for (size_t i = 0U; i < sizeof(entities) / sizeof(entities[0]); ++i) {
        if (equal_span_ci(name, length, entities[i].name)) {
            *value = entities[i].value;
            return true;
        }
    }
    return false;
}

static bool decode_entity(const uint8_t *data, size_t length,
                          size_t start, size_t *next, uint32_t *value) {
    if (start >= length || data[start] != '&') return false;
    size_t end = start + 1U;
    while (end < length && end - start <= 16U && data[end] != ';')
        ++end;
    if (end >= length || data[end] != ';' ||
        end == start + 1U || end - start > 16U) return false;

    const uint8_t *name = data + start + 1U;
    const size_t name_length = end - start - 1U;
    if (name[0] == '#') {
        size_t cursor = 1U;
        unsigned base = 10U;
        if (cursor < name_length &&
            (name[cursor] == 'x' || name[cursor] == 'X')) {
            base = 16U;
            ++cursor;
        }
        if (cursor == name_length) return false;
        uint32_t parsed = 0U;
        for (; cursor < name_length; ++cursor) {
            const uint8_t ch = name[cursor];
            uint32_t digit;
            if (ch >= '0' && ch <= '9') digit = (uint32_t)(ch - '0');
            else if (base == 16U && ch >= 'a' && ch <= 'f')
                digit = (uint32_t)(ch - 'a' + 10U);
            else if (base == 16U && ch >= 'A' && ch <= 'F')
                digit = (uint32_t)(ch - 'A' + 10U);
            else return false;
            if (digit >= base ||
                parsed > (0x10FFFFU - digit) / base) return false;
            parsed = parsed * base + digit;
        }
        if (parsed == 0U || (parsed >= 0xD800U && parsed <= 0xDFFFU))
            parsed = (uint32_t)'?';
        *value = parsed;
    } else if (!decode_named_entity(name, name_length, value)) {
        return false;
    }
    *next = end + 1U;
    return true;
}

static bool find_tag_end(const uint8_t *data, size_t length,
                         size_t start, size_t *end) {
    uint8_t quote = 0U;
    for (size_t i = start; i < length; ++i) {
        const uint8_t ch = data[i];
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

static bool find_raw_close(const uint8_t *data, size_t length,
                           size_t start, const char *tag,
                           size_t *resume) {
    const size_t tag_length = tag[0] == 's' && tag[1] == 'c'
        ? 6U : (tag[0] == 's' ? 5U : 0U);
    if (tag_length == 0U) return false;
    for (size_t i = start; i + tag_length + 2U <= length; ++i) {
        if (data[i] != '<' || data[i + 1U] != '/') continue;
        if (!equal_span_ci(data + i + 2U, tag_length, tag)) continue;
        const size_t after_name = i + 2U + tag_length;
        if (after_name < length && data[after_name] != ' ' &&
            data[after_name] != '\t' && data[after_name] != '\r' &&
            data[after_name] != '\n' && data[after_name] != '>')
            continue;
        size_t end = 0U;
        if (!find_tag_end(data, length, after_name, &end)) {
            *resume = length;
            return true;
        }
        *resume = end + 1U;
        return true;
    }
    return false;
}

static bool parse_tag(const uint8_t *data, size_t length, size_t start,
                      size_t end, size_t *name_start, size_t *name_length,
                      bool *closing) {
    size_t cursor = start + 1U;
    *closing = false;
    if (cursor < end && data[cursor] == '/') {
        *closing = true;
        ++cursor;
    }
    while (cursor < end &&
           (data[cursor] == ' ' || data[cursor] == '\t' ||
            data[cursor] == '\r' || data[cursor] == '\n'))
        ++cursor;
    const size_t begin = cursor;
    while (cursor < end) {
        const uint8_t ch = data[cursor];
        if (!((ch >= 'a' && ch <= 'z') ||
              (ch >= 'A' && ch <= 'Z') ||
              (ch >= '0' && ch <= '9') || ch == '-'))
            break;
        ++cursor;
    }
    (void)length;
    if (cursor == begin) return false;
    *name_start = begin;
    *name_length = cursor - begin;
    return true;
}

bool nexus_html_to_text(const uint8_t *input, size_t input_length,
                        char *output, size_t output_capacity,
                        struct nexus_html_text_result *result) {
    if (output == NULL || output_capacity == 0U || result == NULL ||
        (input == NULL && input_length != 0U)) return false;

    struct html_output out = {
        .buffer = output,
        .capacity = output_capacity,
        .length = 0U,
        .pending_space = false,
        .truncated = false
    };
    bool malformed = false;
    size_t cursor = 0U;

    while (cursor < input_length && !out.truncated) {
        const uint8_t ch = input[cursor];
        if (ch == '&') {
            size_t next = cursor;
            uint32_t codepoint = 0U;
            if (decode_entity(input, input_length, cursor, &next,
                              &codepoint)) {
                if (!output_codepoint(&out, codepoint)) break;
                cursor = next;
                continue;
            }
            if (!output_codepoint(&out, '&')) break;
            ++cursor;
            continue;
        }

        if (ch != '<') {
            if (!output_codepoint(&out, ch)) break;
            ++cursor;
            continue;
        }

        if (cursor + 3U < input_length &&
            input[cursor + 1U] == '!' &&
            input[cursor + 2U] == '-' &&
            input[cursor + 3U] == '-') {
            size_t end = cursor + 4U;
            bool found = false;
            while (end + 2U < input_length) {
                if (input[end] == '-' && input[end + 1U] == '-' &&
                    input[end + 2U] == '>') {
                    cursor = end + 3U;
                    found = true;
                    break;
                }
                ++end;
            }
            if (!found) {
                malformed = true;
                cursor = input_length;
            }
            continue;
        }

        if (cursor + 8U < input_length &&
            input[cursor + 1U] == '!' &&
            input[cursor + 2U] == '[' &&
            input[cursor + 3U] == 'C' &&
            input[cursor + 4U] == 'D' &&
            input[cursor + 5U] == 'A' &&
            input[cursor + 6U] == 'T' &&
            input[cursor + 7U] == 'A' &&
            input[cursor + 8U] == '[') {
            size_t end = cursor + 9U;
            bool found = false;
            while (end + 2U < input_length) {
                if (input[end] == ']' && input[end + 1U] == ']' &&
                    input[end + 2U] == '>') {
                    cursor = end + 3U;
                    found = true;
                    break;
                }
                if (!output_codepoint(&out, input[end])) break;
                ++end;
            }
            if (!found && !out.truncated) malformed = true;
            continue;
        }

        size_t tag_end = 0U;
        if (!find_tag_end(input, input_length, cursor + 1U, &tag_end)) {
            malformed = true;
            if (!output_codepoint(&out, '<')) break;
            ++cursor;
            continue;
        }

        size_t name_start = 0U, name_length = 0U;
        bool closing = false;
        if (!parse_tag(input, input_length, cursor, tag_end,
                       &name_start, &name_length, &closing)) {
            /* Do not discard ordinary text that merely contains '<'. */
            if (!output_codepoint(&out, '<')) break;
            ++cursor;
            continue;
        }

        const uint8_t *name = input + name_start;
        if (!closing && (equal_span_ci(name, name_length, "script") ||
                         equal_span_ci(name, name_length, "style"))) {
            const char *raw_name = equal_span_ci(name, name_length, "script")
                ? "script" : "style";
            size_t resume = 0U;
            if (find_raw_close(input, input_length, tag_end + 1U,
                               raw_name, &resume)) {
                cursor = resume;
            } else {
                malformed = true;
                cursor = input_length;
            }
            continue;
        }

        if (equal_span_ci(name, name_length, "br") ||
            equal_span_ci(name, name_length, "hr")) {
            if (!output_break(&out)) break;
            if (equal_span_ci(name, name_length, "hr")) {
                if (!output_byte(&out, '-') || !output_byte(&out, '-') ||
                    !output_byte(&out, '-')) break;
                if (!output_break(&out)) break;
            }
        } else if (block_tag(name, name_length)) {
            if (equal_span_ci(name, name_length, "li") && !closing) {
                if (!output_list_marker(&out)) break;
            } else if (!output_break(&out)) {
                break;
            }
        } else if (equal_span_ci(name, name_length, "td") ||
                   equal_span_ci(name, name_length, "th")) {
            if (!closing) {
                out.pending_space = true;
            } else {
                if (!output_codepoint(&out, ' ')) break;
            }
        }

        cursor = tag_end + 1U;
    }

    while (out.length != 0U &&
           (out.buffer[out.length - 1U] == ' ' ||
            out.buffer[out.length - 1U] == '\n'))
        --out.length;
    out.buffer[out.length] = '\0';
    result->output_length = out.length;
    result->truncated = out.truncated;
    result->malformed = malformed;
    return true;
}
