#include "desktop_services.h"

static struct nexus_desktop_window_info windows[NEXUS_DESKTOP_WINDOW_MAX];
static struct nexus_desktop_rect restore_bounds[NEXUS_DESKTOP_WINDOW_MAX];
static uint64_t z_order[NEXUS_DESKTOP_WINDOW_MAX];
static size_t window_count;
static uint64_t next_z_order;
static uint32_t active_window_id;
static uint32_t display_width;
static uint32_t display_height;
static uint8_t active_workspace;

static struct nexus_desktop_notification notifications[
    NEXUS_DESKTOP_NOTIFICATION_MAX];
static size_t notification_count;
static size_t notification_replace;
static uint64_t notification_sequence;

struct clipboard_entry {
    char text[NEXUS_DESKTOP_CLIPBOARD_TEXT_MAX];
    size_t length;
    uint64_t sequence;
};
static struct clipboard_entry clipboard[NEXUS_DESKTOP_CLIPBOARD_HISTORY_MAX];
static size_t clipboard_count;
static size_t clipboard_replace;
static uint64_t clipboard_sequence;

static size_t bounded_length(const char *text, size_t capacity) {
    if (text == NULL) return capacity;
    size_t length = 0U;
    while (length < capacity && text[length] != '\0') ++length;
    return length;
}

static void copy_text(char *destination, size_t capacity,
                      const char *source) {
    if (destination == NULL || capacity == 0U) return;
    size_t i = 0U;
    if (source != NULL) {
        while (i + 1U < capacity && source[i] != '\0') {
            destination[i] = source[i];
            ++i;
        }
    }
    destination[i] = '\0';
    while (++i < capacity) destination[i] = '\0';
}

static char upper_ascii(char value) {
    return value >= 'a' && value <= 'z'
        ? (char)(value - 'a' + 'A') : value;
}

static bool contains_ascii_ci(const char *text, const char *query) {
    if (text == NULL || query == NULL) return false;
    if (query[0] == '\0') return true;
    for (size_t start = 0U; text[start] != '\0'; ++start) {
        size_t offset = 0U;
        while (query[offset] != '\0' && text[start + offset] != '\0' &&
               upper_ascii(text[start + offset]) ==
                   upper_ascii(query[offset])) ++offset;
        if (query[offset] == '\0') return true;
    }
    return false;
}

static bool starts_with_ascii_ci(const char *text, const char *query) {
    if (text == NULL || query == NULL) return false;
    size_t i = 0U;
    while (query[i] != '\0') {
        if (text[i] == '\0' ||
            upper_ascii(text[i]) != upper_ascii(query[i])) return false;
        ++i;
    }
    return true;
}

static size_t window_find(uint32_t id) {
    if (id == 0U) return NEXUS_DESKTOP_WINDOW_MAX;
    for (size_t i = 0U; i < window_count; ++i) {
        if (windows[i].open && windows[i].id == id) return i;
    }
    return NEXUS_DESKTOP_WINDOW_MAX;
}

static void update_visibility(void) {
    for (size_t i = 0U; i < window_count; ++i) {
        windows[i].visible = windows[i].open &&
            !windows[i].minimized &&
            windows[i].workspace == active_workspace;
        windows[i].focused = windows[i].visible &&
            windows[i].id == active_window_id;
    }
    if (active_window_id != 0U) {
        const size_t focused = window_find(active_window_id);
        if (focused >= window_count || !windows[focused].visible)
            active_window_id = 0U;
    }
    if (active_window_id == 0U) {
        uint64_t highest = 0U;
        for (size_t i = 0U; i < window_count; ++i) {
            if (windows[i].visible && z_order[i] >= highest) {
                highest = z_order[i];
                active_window_id = windows[i].id;
            }
        }
    }
    for (size_t i = 0U; i < window_count; ++i)
        windows[i].focused = windows[i].visible &&
            windows[i].id == active_window_id;
}

static void normalize_rect(struct nexus_desktop_rect *rect) {
    if (rect == NULL) return;
    const uint32_t width_limit = display_width == 0U ? 1U : display_width;
    const uint32_t height_limit = display_height == 0U ? 1U : display_height;
    const uint32_t minimum_width = width_limit < 180U ? width_limit : 180U;
    const uint32_t minimum_height = height_limit < 120U ? height_limit : 120U;
    if (rect->width < minimum_width) rect->width = minimum_width;
    if (rect->height < minimum_height) rect->height = minimum_height;
    if (rect->width > width_limit) rect->width = width_limit;
    if (rect->height > height_limit) rect->height = height_limit;
    const uint32_t max_x = width_limit - rect->width;
    const uint32_t max_y = height_limit - rect->height;
    if (rect->x > max_x) rect->x = max_x;
    if (rect->y > max_y) rect->y = max_y;
}

void nexus_desktop_services_init(uint32_t screen_width,
                                 uint32_t screen_height) {
    display_width = screen_width == 0U ? 1U : screen_width;
    display_height = screen_height == 0U ? 1U : screen_height;
    for (size_t i = 0U; i < NEXUS_DESKTOP_WINDOW_MAX; ++i) {
        windows[i] = (struct nexus_desktop_window_info){0};
        restore_bounds[i] = (struct nexus_desktop_rect){0};
        z_order[i] = 0U;
    }
    window_count = 0U;
    next_z_order = 1U;
    active_window_id = 0U;
    active_workspace = 0U;
    for (size_t i = 0U; i < NEXUS_DESKTOP_NOTIFICATION_MAX; ++i)
        notifications[i] = (struct nexus_desktop_notification){0};
    notification_count = 0U;
    notification_replace = 0U;
    notification_sequence = 1U;
    for (size_t i = 0U; i < NEXUS_DESKTOP_CLIPBOARD_HISTORY_MAX; ++i)
        clipboard[i] = (struct clipboard_entry){0};
    clipboard_count = 0U;
    clipboard_replace = 0U;
    clipboard_sequence = 1U;
}

uint32_t nexus_desktop_screen_width(void) { return display_width; }
uint32_t nexus_desktop_screen_height(void) { return display_height; }
uint8_t nexus_desktop_workspace_active(void) { return active_workspace; }

bool nexus_desktop_workspace_activate(uint8_t workspace) {
    if (workspace >= NEXUS_DESKTOP_WORKSPACE_COUNT) return false;
    active_workspace = workspace;
    update_visibility();
    return true;
}

bool nexus_desktop_workspace_next(int8_t direction) {
    if (direction == 0) return false;
    if (direction > 0) {
        active_workspace = (uint8_t)((active_workspace + 1U) %
                                     NEXUS_DESKTOP_WORKSPACE_COUNT);
    } else {
        active_workspace = active_workspace == 0U
            ? (uint8_t)(NEXUS_DESKTOP_WORKSPACE_COUNT - 1U)
            : (uint8_t)(active_workspace - 1U);
    }
    update_visibility();
    return true;
}

bool nexus_desktop_window_open(uint32_t id, uint8_t workspace,
                               const char *title,
                               const struct nexus_desktop_rect *bounds) {
    if (id == 0U || workspace >= NEXUS_DESKTOP_WORKSPACE_COUNT ||
        title == NULL || bounds == NULL ||
        bounded_length(title, NEXUS_DESKTOP_WINDOW_TITLE_MAX) ==
            NEXUS_DESKTOP_WINDOW_TITLE_MAX) return false;
    if (window_find(id) < window_count ||
        window_count >= NEXUS_DESKTOP_WINDOW_MAX) return false;
    const size_t slot = window_count++;
    windows[slot] = (struct nexus_desktop_window_info){0};
    windows[slot].id = id;
    windows[slot].workspace = workspace;
    windows[slot].open = true;
    copy_text(windows[slot].title, sizeof(windows[slot].title), title);
    windows[slot].bounds = *bounds;
    normalize_rect(&windows[slot].bounds);
    restore_bounds[slot] = windows[slot].bounds;
    z_order[slot] = next_z_order++;
    if (workspace == active_workspace) active_window_id = id;
    update_visibility();
    return true;
}

bool nexus_desktop_window_close(uint32_t id) {
    const size_t slot = window_find(id);
    if (slot >= window_count) return false;
    windows[slot].open = false;
    windows[slot].visible = false;
    windows[slot].focused = false;
    windows[slot].minimized = false;
    if (active_window_id == id) active_window_id = 0U;
    /* Compact in-place so the table remains bounded and reusable. */
    for (size_t i = slot + 1U; i < window_count; ++i) {
        windows[i - 1U] = windows[i];
        restore_bounds[i - 1U] = restore_bounds[i];
        z_order[i - 1U] = z_order[i];
    }
    --window_count;
    update_visibility();
    return true;
}

bool nexus_desktop_window_minimize(uint32_t id) {
    const size_t slot = window_find(id);
    if (slot >= window_count) return false;
    windows[slot].minimized = true;
    if (active_window_id == id) active_window_id = 0U;
    update_visibility();
    return true;
}

bool nexus_desktop_window_restore(uint32_t id) {
    const size_t slot = window_find(id);
    if (slot >= window_count) return false;
    windows[slot].minimized = false;
    windows[slot].maximized = false;
    windows[slot].snap = NEXUS_DESKTOP_SNAP_NONE;
    normalize_rect(&restore_bounds[slot]);
    windows[slot].bounds = restore_bounds[slot];
    z_order[slot] = next_z_order++;
    if (windows[slot].workspace == active_workspace)
        active_window_id = id;
    update_visibility();
    return true;
}

bool nexus_desktop_window_focus(uint32_t id) {
    const size_t slot = window_find(id);
    if (slot >= window_count || windows[slot].workspace != active_workspace ||
        windows[slot].minimized) return false;
    z_order[slot] = next_z_order++;
    active_window_id = id;
    update_visibility();
    return true;
}

bool nexus_desktop_window_move(uint32_t id, int32_t x, int32_t y) {
    const size_t slot = window_find(id);
    if (slot >= window_count || windows[slot].maximized ||
        windows[slot].snap != NEXUS_DESKTOP_SNAP_NONE) return false;
    struct nexus_desktop_rect next = windows[slot].bounds;
    next.x = x < 0 ? 0U : (uint32_t)x;
    next.y = y < 0 ? 0U : (uint32_t)y;
    normalize_rect(&next);
    windows[slot].bounds = next;
    restore_bounds[slot] = next;
    return true;
}

bool nexus_desktop_window_resize(uint32_t id,
                                 uint32_t width, uint32_t height) {
    const size_t slot = window_find(id);
    if (slot >= window_count || windows[slot].maximized ||
        windows[slot].snap != NEXUS_DESKTOP_SNAP_NONE) return false;
    struct nexus_desktop_rect next = windows[slot].bounds;
    next.width = width;
    next.height = height;
    normalize_rect(&next);
    windows[slot].bounds = next;
    restore_bounds[slot] = next;
    return true;
}

bool nexus_desktop_window_set_bounds(uint32_t id,
                                     const struct nexus_desktop_rect *bounds) {
    const size_t slot = window_find(id);
    if (slot >= window_count || bounds == NULL) return false;
    if (windows[slot].maximized ||
        windows[slot].snap != NEXUS_DESKTOP_SNAP_NONE) return false;
    struct nexus_desktop_rect next = *bounds;
    normalize_rect(&next);
    windows[slot].bounds = next;
    restore_bounds[slot] = next;
    return true;
}

bool nexus_desktop_window_snap(uint32_t id, enum nexus_desktop_snap snap) {
    const size_t slot = window_find(id);
    if (slot >= window_count ||
        (unsigned int)snap > (unsigned int)NEXUS_DESKTOP_SNAP_MAXIMIZED)
        return false;
    if (snap == NEXUS_DESKTOP_SNAP_NONE) {
        windows[slot].maximized = false;
        windows[slot].snap = NEXUS_DESKTOP_SNAP_NONE;
        windows[slot].bounds = restore_bounds[slot];
    } else {
        if (windows[slot].snap == NEXUS_DESKTOP_SNAP_NONE &&
            !windows[slot].maximized) restore_bounds[slot] = windows[slot].bounds;
        windows[slot].maximized = snap == NEXUS_DESKTOP_SNAP_MAXIMIZED;
        windows[slot].snap = snap;
        windows[slot].bounds.x = snap == NEXUS_DESKTOP_SNAP_RIGHT
            ? display_width / 2U : 0U;
        windows[slot].bounds.y = 0U;
        windows[slot].bounds.width =
            snap == NEXUS_DESKTOP_SNAP_MAXIMIZED
                ? display_width
                : (snap == NEXUS_DESKTOP_SNAP_LEFT
                    ? display_width / 2U
                    : display_width - display_width / 2U);
        windows[slot].bounds.height = display_height;
    }
    if (windows[slot].workspace == active_workspace)
        active_window_id = id;
    update_visibility();
    return true;
}

size_t nexus_desktop_window_count(void) { return window_count; }

bool nexus_desktop_window_get(size_t index,
                              struct nexus_desktop_window_info *info) {
    if (info == NULL || index >= window_count) return false;
    *info = windows[index];
    return true;
}

bool nexus_desktop_window_get_by_id(uint32_t id,
                                    struct nexus_desktop_window_info *info) {
    const size_t slot = window_find(id);
    if (slot >= window_count || info == NULL) return false;
    *info = windows[slot];
    return true;
}

static void copy_bounded(char *destination, size_t capacity,
                         const char *source) {
    copy_text(destination, capacity, source);
}

bool nexus_desktop_notification_push(
    const char *title, const char *message,
    enum nexus_desktop_notification_level level, uint64_t timestamp) {
    if (title == NULL || message == NULL || title[0] == '\0' ||
        bounded_length(title, NEXUS_DESKTOP_NOTIFICATION_TITLE_MAX) ==
            NEXUS_DESKTOP_NOTIFICATION_TITLE_MAX ||
        bounded_length(message, NEXUS_DESKTOP_NOTIFICATION_TEXT_MAX) ==
            NEXUS_DESKTOP_NOTIFICATION_TEXT_MAX ||
        (unsigned int)level >
            (unsigned int)NEXUS_DESKTOP_NOTIFICATION_ERROR) return false;
    size_t slot;
    if (notification_count < NEXUS_DESKTOP_NOTIFICATION_MAX) {
        slot = (notification_replace + notification_count) %
               NEXUS_DESKTOP_NOTIFICATION_MAX;
        ++notification_count;
    } else {
        slot = notification_replace;
        notification_replace = (notification_replace + 1U) %
                               NEXUS_DESKTOP_NOTIFICATION_MAX;
    }
    struct nexus_desktop_notification *item = &notifications[slot];
    *item = (struct nexus_desktop_notification){0};
    item->sequence = notification_sequence++;
    item->timestamp = timestamp;
    item->level = level;
    item->unread = true;
    copy_bounded(item->title, sizeof(item->title), title);
    copy_bounded(item->message, sizeof(item->message), message);
    return true;
}

size_t nexus_desktop_notification_count(void) {
    return notification_count;
}

size_t nexus_desktop_notification_unread_count(void) {
    size_t unread = 0U;
    for (size_t i = 0U; i < notification_count; ++i) {
        const size_t slot = (notification_replace + i) %
                            NEXUS_DESKTOP_NOTIFICATION_MAX;
        if (notifications[slot].unread) ++unread;
    }
    return unread;
}

bool nexus_desktop_notification_get(size_t newest_index,
                                    struct nexus_desktop_notification *item) {
    if (item == NULL || newest_index >= notification_count) return false;
    const size_t newest = (notification_replace + notification_count - 1U) %
                          NEXUS_DESKTOP_NOTIFICATION_MAX;
    const size_t slot = (newest + NEXUS_DESKTOP_NOTIFICATION_MAX -
        (newest_index % NEXUS_DESKTOP_NOTIFICATION_MAX)) %
        NEXUS_DESKTOP_NOTIFICATION_MAX;
    *item = notifications[slot];
    return true;
}

void nexus_desktop_notification_mark_read(void) {
    for (size_t i = 0U; i < notification_count; ++i) {
        const size_t slot = (notification_replace + i) %
                            NEXUS_DESKTOP_NOTIFICATION_MAX;
        notifications[slot].unread = false;
    }
}

void nexus_desktop_notification_clear(void) {
    for (size_t i = 0U; i < NEXUS_DESKTOP_NOTIFICATION_MAX; ++i)
        notifications[i] = (struct nexus_desktop_notification){0};
    notification_count = 0U;
    notification_replace = 0U;
}

bool nexus_desktop_clipboard_set(const char *text) {
    if (text == NULL) return false;
    const size_t length = bounded_length(text,
        NEXUS_DESKTOP_CLIPBOARD_TEXT_MAX);
    if (length == 0U || length == NEXUS_DESKTOP_CLIPBOARD_TEXT_MAX)
        return false;
    if (clipboard_count != 0U) {
        const size_t newest = (clipboard_replace +
            clipboard_count - 1U) % NEXUS_DESKTOP_CLIPBOARD_HISTORY_MAX;
        if (clipboard[newest].length == length) {
            bool same = true;
            for (size_t i = 0U; i < length; ++i)
                if (clipboard[newest].text[i] != text[i]) same = false;
            if (same) return true;
        }
    }
    size_t slot;
    if (clipboard_count < NEXUS_DESKTOP_CLIPBOARD_HISTORY_MAX) {
        slot = (clipboard_replace + clipboard_count) %
               NEXUS_DESKTOP_CLIPBOARD_HISTORY_MAX;
        ++clipboard_count;
    } else {
        slot = clipboard_replace;
        clipboard_replace = (clipboard_replace + 1U) %
                            NEXUS_DESKTOP_CLIPBOARD_HISTORY_MAX;
    }
    clipboard[slot].length = length;
    clipboard[slot].sequence = clipboard_sequence++;
    for (size_t i = 0U; i < length; ++i) clipboard[slot].text[i] = text[i];
    clipboard[slot].text[length] = '\0';
    return true;
}

size_t nexus_desktop_clipboard_count(void) { return clipboard_count; }

bool nexus_desktop_clipboard_get(size_t newest_index, char *text,
                                 size_t capacity, size_t *length) {
    if (text == NULL || capacity == 0U || length == NULL ||
        newest_index >= clipboard_count) return false;
    const size_t newest = (clipboard_replace + clipboard_count - 1U) %
                          NEXUS_DESKTOP_CLIPBOARD_HISTORY_MAX;
    const size_t slot = (newest + NEXUS_DESKTOP_CLIPBOARD_HISTORY_MAX -
        (newest_index % NEXUS_DESKTOP_CLIPBOARD_HISTORY_MAX)) %
        NEXUS_DESKTOP_CLIPBOARD_HISTORY_MAX;
    if (clipboard[slot].length >= capacity) return false;
    for (size_t i = 0U; i < clipboard[slot].length; ++i)
        text[i] = clipboard[slot].text[i];
    text[clipboard[slot].length] = '\0';
    *length = clipboard[slot].length;
    return true;
}

void nexus_desktop_clipboard_clear(void) {
    for (size_t i = 0U; i < NEXUS_DESKTOP_CLIPBOARD_HISTORY_MAX; ++i)
        clipboard[i] = (struct clipboard_entry){0};
    clipboard_count = 0U;
    clipboard_replace = 0U;
}

static int compare_search_items(
    const struct nexus_desktop_search_item *items,
    size_t left, size_t right, const char *query) {
    const bool left_prefix = starts_with_ascii_ci(items[left].title, query);
    const bool right_prefix = starts_with_ascii_ci(items[right].title, query);
    if (left_prefix != right_prefix) return left_prefix ? -1 : 1;
    if (items[left].kind != items[right].kind)
        return items[left].kind < items[right].kind ? -1 : 1;
    size_t i = 0U;
    while (items[left].title[i] != '\0' &&
           items[right].title[i] != '\0') {
        const char a = upper_ascii(items[left].title[i]);
        const char b = upper_ascii(items[right].title[i]);
        if (a != b) return a < b ? -1 : 1;
        ++i;
    }
    if (items[left].title[i] == items[right].title[i]) return 0;
    return items[left].title[i] == '\0' ? -1 : 1;
}

bool nexus_desktop_search(
    const struct nexus_desktop_search_item *items, size_t item_count,
    const char *query, size_t *indices, size_t capacity,
    size_t *matched_count) {
    if (matched_count == NULL || query == NULL ||
        item_count > NEXUS_DESKTOP_SEARCH_MAX_ITEMS ||
        (item_count != 0U && (items == NULL || indices == NULL)) ||
        capacity < item_count) return false;
    if (bounded_length(query, NEXUS_DESKTOP_SEARCH_TITLE_MAX) ==
        NEXUS_DESKTOP_SEARCH_TITLE_MAX) return false;
    for (size_t i = 0U; i < item_count; ++i) {
        if ((unsigned int)items[i].kind >
                (unsigned int)NEXUS_DESKTOP_SEARCH_SETTING ||
            bounded_length(items[i].title,
                NEXUS_DESKTOP_SEARCH_TITLE_MAX) ==
                    NEXUS_DESKTOP_SEARCH_TITLE_MAX ||
            bounded_length(items[i].detail,
                NEXUS_DESKTOP_SEARCH_DETAIL_MAX) ==
                    NEXUS_DESKTOP_SEARCH_DETAIL_MAX) return false;
    }
    *matched_count = 0U;
    for (size_t i = 0U; i < item_count; ++i) {
        if (!contains_ascii_ci(items[i].title, query) &&
            !contains_ascii_ci(items[i].detail, query)) continue;
        indices[*matched_count] = i;
        ++*matched_count;
    }
    /* Bounded insertion sort; prefix matches are promoted, source data stays intact. */
    for (size_t i = 1U; i < *matched_count; ++i) {
        const size_t value = indices[i];
        size_t position = i;
        while (position > 0U &&
               compare_search_items(items, value, indices[position - 1U],
                                    query) < 0) {
            indices[position] = indices[position - 1U];
            --position;
        }
        indices[position] = value;
    }
    return true;
}
