#include "text_buffer.h"

static size_t line_start(const char *data, size_t cursor) {
    while (cursor > 0U && data[cursor - 1U] != '\n') --cursor;
    return cursor;
}

static size_t line_end(const char *data, size_t length, size_t cursor) {
    while (cursor < length && data[cursor] != '\n') ++cursor;
    return cursor;
}

static size_t smaller(size_t a, size_t b) {
    return a < b ? a : b;
}

bool nexus_text_buffer_insert(char *data, size_t capacity,
                              size_t *length, size_t *cursor, char value) {
    if (data == NULL || length == NULL || cursor == NULL ||
        capacity == 0U || *length >= capacity ||
        *cursor > *length || value == '\0' ||
        *length >= capacity - 1U) return false;
    for (size_t i = *length + 1U; i > *cursor; --i)
        data[i] = data[i - 1U];
    data[*cursor] = value;
    ++*length;
    ++*cursor;
    return true;
}

bool nexus_text_buffer_backspace(char *data, size_t capacity,
                                 size_t *length, size_t *cursor) {
    if (data == NULL || length == NULL || cursor == NULL ||
        capacity == 0U || *length >= capacity || *cursor > *length ||
        *cursor == 0U) return false;
    for (size_t i = *cursor - 1U; i < *length; ++i)
        data[i] = data[i + 1U];
    --*cursor;
    --*length;
    return true;
}

bool nexus_text_buffer_delete(char *data, size_t capacity,
                              size_t *length, size_t cursor) {
    if (data == NULL || length == NULL || capacity == 0U ||
        *length >= capacity || cursor > *length || cursor == *length) return false;
    for (size_t i = cursor; i < *length; ++i)
        data[i] = data[i + 1U];
    --*length;
    return true;
}

void nexus_text_buffer_move_left(size_t length, size_t *cursor) {
    if (cursor != NULL && *cursor > 0U && *cursor <= length) --*cursor;
}

void nexus_text_buffer_move_right(size_t length, size_t *cursor) {
    if (cursor != NULL && *cursor < length) ++*cursor;
}

void nexus_text_buffer_move_home(const char *data, size_t length,
                                 size_t *cursor) {
    if (data == NULL || cursor == NULL || *cursor > length) return;
    *cursor = line_start(data, *cursor);
}

void nexus_text_buffer_move_end(const char *data, size_t length,
                                size_t *cursor) {
    if (data == NULL || cursor == NULL || *cursor > length) return;
    *cursor = line_end(data, length, *cursor);
}

void nexus_text_buffer_move_up(const char *data, size_t length,
                               size_t *cursor) {
    if (data == NULL || cursor == NULL || *cursor > length) return;
    const size_t current_start = line_start(data, *cursor);
    if (current_start == 0U) return;
    const size_t column = *cursor - current_start;
    const size_t previous_end = current_start - 1U;
    const size_t previous_start = line_start(data, previous_end);
    *cursor = previous_start +
              smaller(column, previous_end - previous_start);
}

void nexus_text_buffer_move_down(const char *data, size_t length,
                                 size_t *cursor) {
    if (data == NULL || cursor == NULL || *cursor > length) return;
    const size_t current_start = line_start(data, *cursor);
    const size_t current_end = line_end(data, length, *cursor);
    if (current_end >= length) return;
    const size_t column = *cursor - current_start;
    const size_t next_start = current_end + 1U;
    const size_t next_end = line_end(data, length, next_start);
    *cursor = next_start + smaller(column, next_end - next_start);
}
