#include "file_list.h"

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

    /* A bounded stable insertion sort is appropriate for at most 128 rows. */
    for (size_t i = 1U; i < count; ++i) {
        const struct nexus_file_list_item key = items[i];
        size_t position = i;
        while (position > 0U &&
               compare_items(&key, &items[position - 1U], mode) < 0) {
            items[position] = items[position - 1U];
            --position;
        }
        items[position] = key;
    }
    return true;
}

const char *nexus_file_sort_mode_label(enum nexus_file_sort_mode mode) {
    switch (mode) {
        case NEXUS_FILE_SORT_NAME: return "SORT: NAME (S)";
        case NEXUS_FILE_SORT_TYPE: return "SORT: TYPE (S)";
        case NEXUS_FILE_SORT_SIZE: return "SORT: SIZE (S)";
        default: return "SORT: INVALID";
    }
}
