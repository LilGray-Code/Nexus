#ifndef NEXUS_BROWSER_HISTORY_H
#define NEXUS_BROWSER_HISTORY_H

#include <stdbool.h>
#include <stddef.h>

#define NEXUS_BROWSER_HISTORY_CAPACITY 12U
#define NEXUS_BROWSER_HISTORY_URL_MAX 96U

struct nexus_browser_history {
    char entries[NEXUS_BROWSER_HISTORY_CAPACITY]
                [NEXUS_BROWSER_HISTORY_URL_MAX];
    size_t count;
    size_t current_index;
    bool has_current;
};

void nexus_browser_history_init(struct nexus_browser_history *history);
bool nexus_browser_history_push(struct nexus_browser_history *history,
                                const char *url);
bool nexus_browser_history_can_back(const struct nexus_browser_history *history);
bool nexus_browser_history_can_forward(const struct nexus_browser_history *history);
bool nexus_browser_history_back(struct nexus_browser_history *history,
                                char *destination, size_t capacity);
bool nexus_browser_history_forward(struct nexus_browser_history *history,
                                   char *destination, size_t capacity);

#endif
