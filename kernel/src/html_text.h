#ifndef NEXUS_HTML_TEXT_H
#define NEXUS_HTML_TEXT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct nexus_html_text_result {
    size_t output_length;
    bool truncated;
    bool malformed;
};

/*
 * Convert a bounded HTML response body to readable, plain ASCII text.
 * Block elements become line breaks; common named/numeric entities are
 * decoded; comments, script, and style contents are omitted. This is a
 * text-extraction stage, not a CSS layout or JavaScript rendering engine.
 *
 * output_capacity includes space for the trailing NUL byte.
 */
bool nexus_html_to_text(const uint8_t *input, size_t input_length,
                        char *output, size_t output_capacity,
                        struct nexus_html_text_result *result);

#endif
