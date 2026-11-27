#include "file_list.h"

static struct nexus_file_list_item sort_scratch[NEXUS_FILE_LIST_MAX_ENTRIES];

static char ascii_upper(char value) {
    if (value >= 'a' && value <= 'z')
        return (char)(value - 'a' + 'A');
    return value;
}

static bool valid_name(const char *name) {
    if (name == NULL) return false;
    for (size_t i = 0U; i < NEXUS_FILE_LIST_NAME_MAX; ++i) {
        if (name[i] == '\0') return i != 0U;
    }
    return false;
}

static int compare_name(const char *left, const char *right) {
    for (size_t i = 0U; i < NEXUS_FILE_LIST_NAME_MAX; ++i) {
        const unsigned char a = (unsigned char)ascii_upper(left[i]);
        const unsigned char b = (unsigned char)ascii_upper(right[i]);
        if (a < b) return -1;
        if (a > b) return 1;
        if (a == 0U) return 0;
    }
    return 0;
}

static const char *file_extension(const char *name) {
    const char *extension = name;
    for (size_t i = 0U; i < NEXUS_FILE_LIST_NAME_MAX &&
         name[i] != '\0'; ++i) {
        if (name[i] == '.') extension = &name[i + 1U];
    }
    return extension == name ? "" : extension;
}

static int compare_items(const struct nexus_file_list_item *left,
                         const struct nexus_file_list_item *right,
                         enum nexus_file_sort_mode mode) {
    if (left->directory != right->directory)
        return left->directory ? -1 : 1;

    if (mode == NEXUS_FILE_SORT_SIZE && left->size != right->size)
        return left->size > right->size ? -1 : 1;

    if (mode == NEXUS_FILE_SORT_TYPE && !left->directory) {
        const int extension_order =
            compare_name(file_extension(left->name),
                         file_extension(right->name));
        if (extension_order != 0) return extension_order;
    }
    return compare_name(left->name, right->name);
}

bool nexus_file_list_sort(struct nexus_file_list_item *items, size_t count,
                          enum nexus_file_sort_mode mode) {
    if ((unsigned int)mode >= (unsigned int)NEXUS_FILE_SORT_MODE_COUNT)
        return false;
    if (count == 0U) return true;
    if (items == NULL || count > NEXUS_FILE_LIST_MAX_ENTRIES) return false;
    for (size_t i = 0U; i < count; ++i) {
        if (!valid_name(items[i].name)) return false;
    }

    /* Bottom-up stable merge sort keeps the 512-entry UI cache O(n log n)
     * rather than allowing a worst-case quadratic sort to stall the shell. */
    for (size_t width = 1U; width < count; width *= 2U) {
        for (size_t base = 0U; base < count; base += width * 2U) {
            size_t left = base;
            size_t middle = base + width;
            size_t right = base + width * 2U;
            if (middle > count) middle = count;
            if (right > count) right = count;
            size_t a = left, b = middle, out = left;
            while (a < middle && b < right) {
                if (compare_items(&items[a], &items[b], mode) <= 0)
                    sort_scratch[out++] = items[a++];
                else
                    sort_scratch[out++] = items[b++];
            }
            while (a < middle) sort_scratch[out++] = items[a++];
            while (b < right) sort_scratch[out++] = items[b++];
        }
        for (size_t i = 0U; i < count; ++i) items[i] = sort_scratch[i];
        if (width > count / 2U) break;
    }
    return true;
}


static bool name_contains_query(const char *name, const char *query,
                                size_t query_length) {
    if (query_length == 0U) return true;
    for (size_t start = 0U; name[start] != '\0'; ++start) {
        size_t offset = 0U;
        while (offset < query_length && name[start + offset] != '\0' &&
               ascii_upper(name[start + offset]) ==
                   ascii_upper(query[offset])) ++offset;
        if (offset == query_length) return true;
    }
    return false;
}

bool nexus_file_list_filter_mode(
    const struct nexus_file_list_item *items, size_t count, const char *query,
    enum nexus_file_filter_mode mode, size_t *indices, size_t capacity,
    size_t *matched_count) {
    if (matched_count == NULL || query == NULL ||
        (unsigned int)mode >= (unsigned int)NEXUS_FILE_FILTER_MODE_COUNT ||
        count > NEXUS_FILE_LIST_MAX_ENTRIES ||
        (count != 0U && (items == NULL || indices == NULL)) ||
        capacity < count) return false;

    size_t query_length = 0U;
    while (query_length < NEXUS_FILE_LIST_NAME_MAX &&
           query[query_length] != '\0') ++query_length;
    if (query_length == NEXUS_FILE_LIST_NAME_MAX) return false;

    *matched_count = 0U;
    for (size_t i = 0U; i < count; ++i) {
        if (!valid_name(items[i].name)) return false;
        if (mode == NEXUS_FILE_FILTER_FILES && items[i].directory) continue;
        if (mode == NEXUS_FILE_FILTER_DIRECTORIES && !items[i].directory) continue;
        if (!name_contains_query(items[i].name, query, query_length))
            continue;
        indices[*matched_count] = i;
        ++*matched_count;
    }
    return true;
}

bool nexus_file_list_filter(const struct nexus_file_list_item *items,
                            size_t count, const char *query,
                            size_t *indices, size_t capacity,
                            size_t *matched_count) {
    return nexus_file_list_filter_mode(items, count, query,
        NEXUS_FILE_FILTER_ALL, indices, capacity, matched_count);
}

const char *nexus_file_filter_mode_label(enum nexus_file_filter_mode mode) {
    switch (mode) {
        case NEXUS_FILE_FILTER_ALL: return "FILTER: ALL";
        case NEXUS_FILE_FILTER_FILES: return "FILTER: FILES";
        case NEXUS_FILE_FILTER_DIRECTORIES: return "FILTER: FOLDERS";
        default: return "FILTER: INVALID";
    }
}

const char *nexus_file_sort_mode_label(enum nexus_file_sort_mode mode) {
    switch (mode) {
        case NEXUS_FILE_SORT_NAME: return "SORT: NAME (S)";
        case NEXUS_FILE_SORT_TYPE: return "SORT: TYPE (S)";
        case NEXUS_FILE_SORT_SIZE: return "SORT: SIZE (S)";
        default: return "SORT: INVALID";
    }
}
