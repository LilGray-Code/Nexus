#ifndef NEXUS_TEXT_BUFFER_H
#define NEXUS_TEXT_BUFFER_H

#include <stdbool.h>
#include <stddef.h>

bool nexus_text_buffer_insert(char *data, size_t capacity,
                              size_t *length, size_t *cursor, char value);
bool nexus_text_buffer_backspace(char *data, size_t capacity,
                                 size_t *length, size_t *cursor);
bool nexus_text_buffer_delete(char *data, size_t capacity,
                              size_t *length, size_t cursor);
void nexus_text_buffer_move_left(size_t length, size_t *cursor);
void nexus_text_buffer_move_right(size_t length, size_t *cursor);
void nexus_text_buffer_move_home(const char *data, size_t length,
                                 size_t *cursor);
void nexus_text_buffer_move_end(const char *data, size_t length,
                                size_t *cursor);
void nexus_text_buffer_move_up(const char *data, size_t length,
                               size_t *cursor);
void nexus_text_buffer_move_down(const char *data, size_t length,
                                 size_t *cursor);

#endif
