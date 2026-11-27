#ifndef NEXUS_DESKTOP_SERVICES_H
#define NEXUS_DESKTOP_SERVICES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NEXUS_DESKTOP_WORKSPACE_COUNT 4U
#define NEXUS_DESKTOP_WINDOW_MAX 32U
#define NEXUS_DESKTOP_WINDOW_TITLE_MAX 32U
#define NEXUS_DESKTOP_NOTIFICATION_MAX 12U
#define NEXUS_DESKTOP_NOTIFICATION_TITLE_MAX 40U
#define NEXUS_DESKTOP_NOTIFICATION_TEXT_MAX 96U
#define NEXUS_DESKTOP_CLIPBOARD_HISTORY_MAX 8U
#define NEXUS_DESKTOP_CLIPBOARD_TEXT_MAX 256U
#define NEXUS_DESKTOP_SEARCH_MAX_ITEMS 64U
#define NEXUS_DESKTOP_SEARCH_TITLE_MAX 40U
#define NEXUS_DESKTOP_SEARCH_DETAIL_MAX 56U

enum nexus_desktop_snap {
    NEXUS_DESKTOP_SNAP_NONE = 0,
    NEXUS_DESKTOP_SNAP_LEFT,
    NEXUS_DESKTOP_SNAP_RIGHT,
    NEXUS_DESKTOP_SNAP_MAXIMIZED
};

enum nexus_desktop_notification_level {
    NEXUS_DESKTOP_NOTIFICATION_INFO = 0,
    NEXUS_DESKTOP_NOTIFICATION_SUCCESS,
    NEXUS_DESKTOP_NOTIFICATION_WARNING,
    NEXUS_DESKTOP_NOTIFICATION_ERROR
};

enum nexus_desktop_search_kind {
    NEXUS_DESKTOP_SEARCH_APP = 0,
    NEXUS_DESKTOP_SEARCH_FILE,
    NEXUS_DESKTOP_SEARCH_SETTING
};

struct nexus_desktop_rect {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
};

struct nexus_desktop_window_info {
    uint32_t id;
    uint8_t workspace;
    bool open;
    bool visible;
    bool minimized;
    bool maximized;
    bool focused;
    enum nexus_desktop_snap snap;
    struct nexus_desktop_rect bounds;
    char title[NEXUS_DESKTOP_WINDOW_TITLE_MAX];
};

struct nexus_desktop_notification {
    uint64_t sequence;
    uint64_t timestamp;
    enum nexus_desktop_notification_level level;
    bool unread;
    char title[NEXUS_DESKTOP_NOTIFICATION_TITLE_MAX];
    char message[NEXUS_DESKTOP_NOTIFICATION_TEXT_MAX];
};

struct nexus_desktop_search_item {
    enum nexus_desktop_search_kind kind;
    char title[NEXUS_DESKTOP_SEARCH_TITLE_MAX];
    char detail[NEXUS_DESKTOP_SEARCH_DETAIL_MAX];
};

void nexus_desktop_services_init(uint32_t screen_width,
                                 uint32_t screen_height);
uint32_t nexus_desktop_screen_width(void);
uint32_t nexus_desktop_screen_height(void);

uint8_t nexus_desktop_workspace_active(void);
bool nexus_desktop_workspace_activate(uint8_t workspace);
bool nexus_desktop_workspace_next(int8_t direction);

bool nexus_desktop_window_open(uint32_t id, uint8_t workspace,
                               const char *title,
                               const struct nexus_desktop_rect *bounds);
bool nexus_desktop_window_close(uint32_t id);
bool nexus_desktop_window_minimize(uint32_t id);
bool nexus_desktop_window_restore(uint32_t id);
bool nexus_desktop_window_focus(uint32_t id);
bool nexus_desktop_window_move(uint32_t id, int32_t x, int32_t y);
bool nexus_desktop_window_resize(uint32_t id,
                                 uint32_t width, uint32_t height);
bool nexus_desktop_window_snap(uint32_t id, enum nexus_desktop_snap snap);
bool nexus_desktop_window_set_bounds(uint32_t id,
                                     const struct nexus_desktop_rect *bounds);
size_t nexus_desktop_window_count(void);
bool nexus_desktop_window_get(size_t index,
                              struct nexus_desktop_window_info *info);
bool nexus_desktop_window_get_by_id(uint32_t id,
                                    struct nexus_desktop_window_info *info);

bool nexus_desktop_notification_push(
    const char *title, const char *message,
    enum nexus_desktop_notification_level level, uint64_t timestamp);
size_t nexus_desktop_notification_count(void);
size_t nexus_desktop_notification_unread_count(void);
bool nexus_desktop_notification_get(size_t newest_index,
                                    struct nexus_desktop_notification *item);
void nexus_desktop_notification_mark_read(void);
void nexus_desktop_notification_clear(void);

bool nexus_desktop_clipboard_set(const char *text);
size_t nexus_desktop_clipboard_count(void);
bool nexus_desktop_clipboard_get(size_t newest_index, char *text,
                                 size_t capacity, size_t *length);
void nexus_desktop_clipboard_clear(void);

bool nexus_desktop_search(
    const struct nexus_desktop_search_item *items, size_t item_count,
    const char *query, size_t *indices, size_t capacity,
    size_t *matched_count);

#endif
