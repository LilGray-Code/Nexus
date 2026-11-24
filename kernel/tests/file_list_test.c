#include "file_list.h"

#include <assert.h>
#include <string.h>

static void set_item(struct nexus_file_list_item *item, const char *name,
                     bool directory, uint32_t size) {
    *item = (struct nexus_file_list_item){0};
    size_t i = 0U;
    while (name[i] != '\0' && i < NEXUS_FILE_LIST_NAME_MAX - 1U) {
        item->name[i] = name[i];
        ++i;
    }
    item->directory = directory;
    item->size = size;
}

static void expect_names(const struct nexus_file_list_item *items,
                         const char *const *expected, size_t count) {
    for (size_t i = 0U; i < count; ++i)
        assert(strcmp(items[i].name, expected[i]) == 0);
}

int main(void) {
    struct nexus_file_list_item by_name[] = {
        {"zeta.txt", false, 10U},
        {"Pictures", true, 0U},
        {"notes.md", false, 30U},
        {"Documents", true, 0U},
        {"archive.bin", false, 50U},
        {"large.bin", false, 100U},
        {"noext", false, 20U},
        {"notes.txt", false, 30U}
    };
    const char *const name_order[] = {
        "Documents", "Pictures", "archive.bin", "large.bin",
        "noext", "notes.md", "notes.txt", "zeta.txt"
    };
    assert(nexus_file_list_sort(by_name,
        sizeof(by_name) / sizeof(by_name[0]), NEXUS_FILE_SORT_NAME));
    expect_names(by_name, name_order, sizeof(name_order) / sizeof(name_order[0]));
    assert(by_name[0].directory && by_name[1].directory);
    assert(!by_name[2].directory);

    struct nexus_file_list_item by_type[] = {
        {"zeta.txt", false, 10U},
        {"Pictures", true, 0U},
        {"notes.md", false, 30U},
        {"Documents", true, 0U},
        {"archive.bin", false, 50U},
        {"large.bin", false, 100U},
        {"noext", false, 20U},
        {"notes.txt", false, 30U}
    };
    const char *const type_order[] = {
        "Documents", "Pictures", "noext", "archive.bin",
        "large.bin", "notes.md", "notes.txt", "zeta.txt"
    };
    assert(nexus_file_list_sort(by_type,
        sizeof(by_type) / sizeof(by_type[0]), NEXUS_FILE_SORT_TYPE));
    expect_names(by_type, type_order, sizeof(type_order) / sizeof(type_order[0]));

    struct nexus_file_list_item by_size[] = {
        {"zeta.txt", false, 10U},
        {"Pictures", true, 0U},
        {"notes.md", false, 30U},
        {"Documents", true, 0U},
        {"archive.bin", false, 50U},
        {"large.bin", false, 100U},
        {"noext", false, 20U},
        {"notes.txt", false, 30U}
    };
    const char *const size_order[] = {
        "Documents", "Pictures", "large.bin", "archive.bin",
        "notes.md", "notes.txt", "noext", "zeta.txt"
    };
    assert(nexus_file_list_sort(by_size,
        sizeof(by_size) / sizeof(by_size[0]), NEXUS_FILE_SORT_SIZE));
    expect_names(by_size, size_order, sizeof(size_order) / sizeof(size_order[0]));

    assert(strcmp(nexus_file_sort_mode_label(NEXUS_FILE_SORT_NAME),
                  "SORT: NAME (S)") == 0);
    assert(strcmp(nexus_file_sort_mode_label(NEXUS_FILE_SORT_TYPE),
                  "SORT: TYPE (S)") == 0);
    assert(strcmp(nexus_file_sort_mode_label(NEXUS_FILE_SORT_SIZE),
                  "SORT: SIZE (S)") == 0);
    assert(nexus_file_list_sort(NULL, 0U, NEXUS_FILE_SORT_NAME));

    /* Case-insensitive substring filtering returns original array indices. */
    struct nexus_file_list_item filter_items[] = {
        {"Documents", true, 0U},
        {"notes.txt", false, 42U},
        {"image.png", false, 17U},
        {"meeting-notes.md", false, 73U},
        {"NOTES-OLD.TXT", false, 9U}
    };
    size_t filtered[5] = {0U};
    size_t filtered_count = 0U;
    assert(nexus_file_list_filter(filter_items, 5U, "notes",
                                  filtered, 5U, &filtered_count));
    assert(filtered_count == 3U);
    assert(filtered[0] == 1U && filtered[1] == 3U && filtered[2] == 4U);
    assert(nexus_file_list_filter(filter_items, 5U, ".TXT",
                                  filtered, 5U, &filtered_count));
    assert(filtered_count == 2U);
    assert(filtered[0] == 1U && filtered[1] == 4U);
    assert(nexus_file_list_filter(filter_items, 5U, "DOC",
                                  filtered, 5U, &filtered_count));
    assert(filtered_count == 1U && filtered[0] == 0U);
    assert(nexus_file_list_filter(filter_items, 5U, "",
                                  filtered, 5U, &filtered_count));
    assert(filtered_count == 5U && filtered[4] == 4U);
    assert(nexus_file_list_filter(filter_items, 5U, "not-present",
                                  filtered, 5U, &filtered_count));
    assert(filtered_count == 0U);
    assert(nexus_file_list_filter(NULL, 0U, "", NULL, 0U,
                                  &filtered_count));
    assert(filtered_count == 0U);
    assert(!nexus_file_list_filter(filter_items, 5U, "notes",
                                   filtered, 4U, &filtered_count));
    assert(!nexus_file_list_filter(filter_items, 5U, NULL,
                                   filtered, 5U, &filtered_count));
    char oversized_query[NEXUS_FILE_LIST_NAME_MAX + 1U];
    memset(oversized_query, 'X', sizeof(oversized_query));
    oversized_query[sizeof(oversized_query) - 1U] = '\0';
    assert(!nexus_file_list_filter(filter_items, 5U, oversized_query,
                                   filtered, 5U, &filtered_count));

    struct nexus_file_list_item malformed[1];
    memset(malformed, 0, sizeof(malformed));
    for (size_t i = 0U; i < NEXUS_FILE_LIST_NAME_MAX; ++i)
        malformed[0].name[i] = 'X';
    assert(!nexus_file_list_sort(malformed, 1U, NEXUS_FILE_SORT_NAME));
    assert(!nexus_file_list_sort(by_size, 1U,
                                 (enum nexus_file_sort_mode)99));
    assert(!nexus_file_list_sort(NULL, 0U,
                                 (enum nexus_file_sort_mode)99));

    struct nexus_file_list_item too_many[NEXUS_FILE_LIST_MAX_ENTRIES + 1U];
    for (size_t i = 0U; i < NEXUS_FILE_LIST_MAX_ENTRIES + 1U; ++i)
        set_item(&too_many[i], "ITEM", false, (uint32_t)i);
    assert(!nexus_file_list_sort(too_many,
        NEXUS_FILE_LIST_MAX_ENTRIES + 1U, NEXUS_FILE_SORT_NAME));
    return 0;
}
