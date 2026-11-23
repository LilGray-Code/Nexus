#include "browser_search.h"

#include <stddef.h>

static bool append_char(char *output, size_t capacity, size_t *length,
                        char value) {
    if (*length + 1U >= capacity) return false;
    output[*length] = value;
    ++*length;
    output[*length] = '\0';
    return true;
}

bool nexus_browser_search_url(const char *query, char *output,
                              size_t output_capacity) {
    static const char prefix[] =
        "https://lite.duckduckgo.com/lite/?q=";
    static const char hex[] = "0123456789ABCDEF";
    if (query == NULL || output == NULL || output_capacity == 0U) return false;
    output[0] = '\0';

    size_t query_length = 0U;
    while (query[query_length] != '\0') {
        const unsigned char value = (unsigned char)query[query_length];
        if (value < 0x20U || value == 0x7FU ||
            query_length >= NEXUS_BROWSER_SEARCH_QUERY_MAX) return false;
        ++query_length;
    }
    if (query_length == 0U) return false;

    size_t length = 0U;
    for (size_t i = 0U; i < sizeof(prefix) - 1U; ++i) {
        if (!append_char(output, output_capacity, &length, prefix[i]))
            return false;
    }

    for (size_t i = 0U; i < query_length; ++i) {
        const unsigned char value = (unsigned char)query[i];
        const bool unreserved =
            (value >= 'A' && value <= 'Z') ||
            (value >= 'a' && value <= 'z') ||
            (value >= '0' && value <= '9') ||
            value == '-' || value == '_' || value == '.' || value == '~';
        if (unreserved) {
            if (!append_char(output, output_capacity, &length, (char)value))
                return false;
        } else {
            if (!append_char(output, output_capacity, &length, '%') ||
                !append_char(output, output_capacity, &length,
                             hex[(value >> 4) & 0x0FU]) ||
                !append_char(output, output_capacity, &length,
                             hex[value & 0x0FU])) return false;
        }
    }
    return true;
}
