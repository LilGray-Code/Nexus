#include "boot_mode.h"

#include <stddef.h>

static bool token_equals(const char *start, size_t length,
                         const char *expected) {
    size_t expected_length = 0U;
    while (expected[expected_length] != '\0') ++expected_length;
    if (length != expected_length) return false;
    for (size_t i = 0U; i < length; ++i) {
        if (start[i] != expected[i]) return false;
    }
    return true;
}

bool nexus_boot_mode_is_installer(const char *command_line) {
    static const char option[] = "nexus.mode=installer";
    if (command_line == NULL) return false;

    const char *cursor = command_line;
    while (*cursor != '\0') {
        while (*cursor == ' ' || *cursor == '\t') ++cursor;
        if (*cursor == '\0') break;

        const char *start = cursor;
        while (*cursor != '\0' && *cursor != ' ' && *cursor != '\t')
            ++cursor;
        if (token_equals(start, (size_t)(cursor - start), option))
            return true;
    }
    return false;
}
