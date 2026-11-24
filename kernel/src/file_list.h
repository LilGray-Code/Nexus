#ifndef NEXUS_FILE_LIST_H
#define NEXUS_FILE_LIST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NEXUS_FILE_LIST_NAME_MAX 32U
#define NEXUS_FILE_LIST_MAX_ENTRIES 128U

enum nexus_file_sort_mode {
    NEXUS_FILE_SORT_NAME = 0,
    NEXUS_FILE_SORT_TYPE,
    NEXUS_FILE_SORT_SIZE,
    NEXUS_FILE_SORT_MODE_COUNT
};

struct nexus_file_list_item {
    char name[NEXUS_FILE_LIST_NAME_MAX];
    bool directory;
    uint32_t size;
};

/*
 * Stable, in-place ordering for the bounded Files application cache.
 * Directories always precede files. Size order is largest-first; ties use
 * the case-insensitive name order. The function rejects malformed entries.
 */
bool nexus_file_list_sort(struct nexus_file_list_item *items, size_t count,
                          enum nexus_file_sort_mode mode);
const char *nexus_file_sort_mode_label(enum nexus_file_sort_mode mode);

#endif
