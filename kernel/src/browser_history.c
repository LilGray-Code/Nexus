#include "browser_history.h"

#include <stddef.h>

static bool url_length(const char *url, size_t *length) {
    if (url == NULL || length == NULL) return false;
    size_t used = 0U;
    while (used < NEXUS_BROWSER_HISTORY_URL_MAX && url[used] != '\0') {
        const unsigned char ch = (unsigned char)url[used];
        if (ch < 0x21U || ch > 0x7EU) return false;
        ++used;
    }
    if (used == 0U || used >= NEXUS_BROWSER_HISTORY_URL_MAX) return false;
    *length = used;
    return true;
}

static bool copy_url(const char *source, size_t length,
                     char *destination, size_t capacity) {
    if (source == NULL || destination == NULL ||
        capacity == 0U || length >= capacity) return false;
    for (size_t i = 0U; i < length; ++i) destination[i] = source[i];
    destination[length] = '\0';
    return true;
}

void nexus_browser_history_init(struct nexus_browser_history *history) {
    if (history == NULL) return;
    *history = (struct nexus_browser_history){0};
}

bool nexus_browser_history_push(struct nexus_browser_history *history,
                                const char *url) {
    size_t length = 0U;
    if (history == NULL || !url_length(url, &length) ||
        history->count > NEXUS_BROWSER_HISTORY_CAPACITY ||
        (history->has_current &&
         history->current_index >= history->count)) return false;

    if (history->has_current) {
        const char *current = history->entries[history->current_index];
        size_t current_length = 0U;
        if (!url_length(current, &current_length)) return false;
        if (current_length == length) {
            bool equal = true;
            for (size_t i = 0U; i < length; ++i) {
                if (current[i] != url[i]) {
                    equal = false;
                    break;
                }
            }
            if (equal) return true;
        }
        /* A new navigation after Back discards the old Forward branch. */
        history->count = history->current_index + 1U;
    }

    if (history->count == NEXUS_BROWSER_HISTORY_CAPACITY) {
        for (size_t i = 1U; i < NEXUS_BROWSER_HISTORY_CAPACITY; ++i) {
            for (size_t j = 0U; j < NEXUS_BROWSER_HISTORY_URL_MAX; ++j)
                history->entries[i - 1U][j] = history->entries[i][j];
        }
        --history->count;
        if (history->has_current && history->current_index != 0U)
            --history->current_index;
    }

    if (!copy_url(url, length, history->entries[history->count],
                  NEXUS_BROWSER_HISTORY_URL_MAX)) return false;
    history->current_index = history->count;
    ++history->count;
    history->has_current = true;
    return true;
}

bool nexus_browser_history_can_back(const struct nexus_browser_history *history) {
    return history != NULL && history->has_current &&
           history->count != 0U && history->current_index > 0U;
}

bool nexus_browser_history_can_forward(
    const struct nexus_browser_history *history) {
    return history != NULL && history->has_current &&
           history->current_index + 1U < history->count;
}

bool nexus_browser_history_back(struct nexus_browser_history *history,
                                char *destination, size_t capacity) {
    if (!nexus_browser_history_can_back(history) ||
        destination == NULL || capacity == 0U) return false;
    const size_t target = history->current_index - 1U;
    size_t length = 0U;
    const char *url = history->entries[target];
    if (!url_length(url, &length) || length >= capacity) return false;
    history->current_index = target;
    return copy_url(url, length, destination, capacity);
}

bool nexus_browser_history_forward(struct nexus_browser_history *history,
                                   char *destination, size_t capacity) {
    if (!nexus_browser_history_can_forward(history) ||
        destination == NULL || capacity == 0U) return false;
    const size_t target = history->current_index + 1U;
    size_t length = 0U;
    const char *url = history->entries[target];
    if (!url_length(url, &length) || length >= capacity) return false;
    history->current_index = target;
    return copy_url(url, length, destination, capacity);
}
