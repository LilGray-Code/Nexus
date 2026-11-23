#include "window_manager.h"

#define NEXUS_WM_MIN_WIDTH 160U
#define NEXUS_WM_MIN_HEIGHT 96U
#define NEXUS_WM_SLOT_MASK 0xFFU
#define NEXUS_WM_MAX_GENERATION 0x00FFFFFFU

struct window_slot {
    struct nexus_window_info info;
    uint32_t generation;
    bool occupied;
};

static struct window_slot slots[NEXUS_WM_MAX_WINDOWS];
static uint8_t z_order[NEXUS_WM_MAX_WINDOWS];
static size_t window_count;
static uint32_t display_width;
static uint32_t display_height;
static nexus_window_id focused_id;
static bool manager_ready;

static nexus_window_id make_id(size_t slot, uint32_t generation) {
    return (generation << 8) | (uint32_t)(slot + 1U);
}

static bool decode_id(nexus_window_id id, size_t *slot_out) {
    const uint32_t encoded_slot = id & NEXUS_WM_SLOT_MASK;
    const uint32_t generation = id >> 8;
    if (!manager_ready || encoded_slot == 0U ||
        encoded_slot > NEXUS_WM_MAX_WINDOWS || generation == 0U) {
        return false;
    }

    const size_t slot = (size_t)(encoded_slot - 1U);
    if (!slots[slot].occupied || slots[slot].generation != generation)
        return false;
    if (slot_out != NULL) *slot_out = slot;
    return true;
}

static void copy_title(char destination[NEXUS_WM_TITLE_MAX + 1U],
                       const char *source) {
    size_t i = 0U;
    if (source != NULL) {
        while (i < NEXUS_WM_TITLE_MAX && source[i] != '\0') {
            destination[i] = source[i];
            ++i;
        }
    }
    destination[i] = '\0';
    for (++i; i <= NEXUS_WM_TITLE_MAX; ++i) destination[i] = '\0';
}

static uint32_t minimum_width(void) {
    return display_width < NEXUS_WM_MIN_WIDTH
        ? display_width : NEXUS_WM_MIN_WIDTH;
}

static uint32_t minimum_height(void) {
    return display_height < NEXUS_WM_MIN_HEIGHT
        ? display_height : NEXUS_WM_MIN_HEIGHT;
}

static void clamp_geometry(struct nexus_window_info *info) {
    if (info->width < minimum_width()) info->width = minimum_width();
    if (info->height < minimum_height()) info->height = minimum_height();
    if (info->width > display_width) info->width = display_width;
    if (info->height > display_height) info->height = display_height;

    if (info->x < 0) info->x = 0;
    if (info->y < 0) info->y = 0;
    const int32_t max_x = (int32_t)(display_width - info->width);
    const int32_t max_y = (int32_t)(display_height - info->height);
    if (info->x > max_x) info->x = max_x;
    if (info->y > max_y) info->y = max_y;
}

static void bring_to_front(size_t slot) {
    size_t position = 0U;
    while (position < window_count && z_order[position] != slot) ++position;
    if (position == window_count) return;
    for (size_t i = position; i + 1U < window_count; ++i)
        z_order[i] = z_order[i + 1U];
    z_order[window_count - 1U] = (uint8_t)slot;
    focused_id = slots[slot].info.id;
}

static void focus_topmost(void) {
    focused_id = 0U;
    for (size_t i = window_count; i > 0U; --i) {
        struct window_slot *slot = &slots[z_order[i - 1U]];
        if (slot->occupied &&
            (slot->info.flags & NEXUS_WINDOW_MINIMIZED) == 0U) {
            focused_id = slot->info.id;
            return;
        }
    }
}

static void remove_from_z_order(size_t slot) {
    size_t position = 0U;
    while (position < window_count && z_order[position] != slot) ++position;
    if (position == window_count) return;
    for (size_t i = position; i + 1U < window_count; ++i)
        z_order[i] = z_order[i + 1U];
    --window_count;
}

bool nexus_wm_init(uint32_t screen_width, uint32_t screen_height) {
    if (screen_width == 0U || screen_height == 0U ||
        screen_width > 0x7FFFFFFFU || screen_height > 0x7FFFFFFFU)
        return false;

    display_width = screen_width;
    display_height = screen_height;
    window_count = 0U;
    focused_id = 0U;
    const bool was_initialized = manager_ready;
    for (size_t i = 0U; i < NEXUS_WM_MAX_WINDOWS; ++i) {
        slots[i].occupied = false;
        if (!was_initialized) {
            slots[i].generation = 1U;
        } else if (slots[i].generation >= NEXUS_WM_MAX_GENERATION) {
            slots[i].generation = 1U;
        } else {
            ++slots[i].generation;
        }
        slots[i].info.id = 0U;
        slots[i].info.flags = 0U;
    }
    manager_ready = true;
    return true;
}

bool nexus_wm_create(uint32_t application_id, const char *title,
                     int32_t x, int32_t y, uint32_t width, uint32_t height,
                     nexus_window_id *out_id) {
    if (!manager_ready || out_id == NULL || width == 0U || height == 0U ||
        window_count >= NEXUS_WM_MAX_WINDOWS) return false;

    size_t index = 0U;
    while (index < NEXUS_WM_MAX_WINDOWS && slots[index].occupied) ++index;
    if (index == NEXUS_WM_MAX_WINDOWS) return false;

    struct window_slot *slot = &slots[index];
    struct nexus_window_info *info = &slot->info;
    info->id = make_id(index, slot->generation);
    info->application_id = application_id;
    info->x = x;
    info->y = y;
    info->width = width;
    info->height = height;
    info->restore_x = x;
    info->restore_y = y;
    info->restore_width = width;
    info->restore_height = height;
    info->flags = NEXUS_WINDOW_VISIBLE;
    copy_title(info->title, title);
    clamp_geometry(info);

    slot->occupied = true;
    z_order[window_count++] = (uint8_t)index;
    bring_to_front(index);
    *out_id = info->id;
    return true;
}

bool nexus_wm_destroy(nexus_window_id id) {
    size_t index;
    if (!decode_id(id, &index)) return false;
    remove_from_z_order(index);
    slots[index].occupied = false;
    slots[index].info.flags = 0U;
    slots[index].info.id = 0U;
    if (slots[index].generation >= NEXUS_WM_MAX_GENERATION)
        slots[index].generation = 1U;
    else
        ++slots[index].generation;
    if (focused_id == id) focus_topmost();
    return true;
}

bool nexus_wm_set_title(nexus_window_id id, const char *title) {
    size_t index;
    if (!decode_id(id, &index) || title == NULL) return false;
    copy_title(slots[index].info.title, title);
    return true;
}

bool nexus_wm_move(nexus_window_id id, int32_t x, int32_t y) {
    size_t index;
    if (!decode_id(id, &index)) return false;
    struct nexus_window_info *info = &slots[index].info;
    if ((info->flags & NEXUS_WINDOW_MAXIMIZED) != 0U) return false;
    info->x = x;
    info->y = y;
    clamp_geometry(info);
    return true;
}

bool nexus_wm_resize(nexus_window_id id, uint32_t width, uint32_t height) {
    size_t index;
    if (!decode_id(id, &index) || width == 0U || height == 0U) return false;
    struct nexus_window_info *info = &slots[index].info;
    if ((info->flags & NEXUS_WINDOW_MAXIMIZED) != 0U) return false;
    info->width = width;
    info->height = height;
    clamp_geometry(info);
    return true;
}

bool nexus_wm_focus(nexus_window_id id) {
    size_t index;
    if (!decode_id(id, &index) ||
        (slots[index].info.flags & NEXUS_WINDOW_MINIMIZED) != 0U)
        return false;
    bring_to_front(index);
    return true;
}

bool nexus_wm_minimize(nexus_window_id id) {
    size_t index;
    if (!decode_id(id, &index)) return false;
    slots[index].info.flags |= NEXUS_WINDOW_MINIMIZED;
    if (focused_id == id) focus_topmost();
    return true;
}

bool nexus_wm_restore(nexus_window_id id) {
    size_t index;
    if (!decode_id(id, &index)) return false;
    slots[index].info.flags &= ~NEXUS_WINDOW_MINIMIZED;
    bring_to_front(index);
    return true;
}

bool nexus_wm_maximize(nexus_window_id id) {
    size_t index;
    if (!decode_id(id, &index)) return false;
    struct nexus_window_info *info = &slots[index].info;
    if ((info->flags & NEXUS_WINDOW_MAXIMIZED) == 0U) {
        info->restore_x = info->x;
        info->restore_y = info->y;
        info->restore_width = info->width;
        info->restore_height = info->height;
        info->flags |= NEXUS_WINDOW_MAXIMIZED;
    }
    info->flags &= ~NEXUS_WINDOW_MINIMIZED;
    info->x = 0;
    info->y = 0;
    info->width = display_width;
    info->height = display_height;
    bring_to_front(index);
    return true;
}

bool nexus_wm_unmaximize(nexus_window_id id) {
    size_t index;
    if (!decode_id(id, &index)) return false;
    struct nexus_window_info *info = &slots[index].info;
    if ((info->flags & NEXUS_WINDOW_MAXIMIZED) == 0U) return true;
    info->x = info->restore_x;
    info->y = info->restore_y;
    info->width = info->restore_width;
    info->height = info->restore_height;
    info->flags &= ~NEXUS_WINDOW_MAXIMIZED;
    clamp_geometry(info);
    return true;
}

bool nexus_wm_get(nexus_window_id id, struct nexus_window_info *out_info) {
    size_t index;
    if (!decode_id(id, &index) || out_info == NULL) return false;
    *out_info = slots[index].info;
    if (focused_id == id) out_info->flags |= NEXUS_WINDOW_FOCUSED;
    else out_info->flags &= ~NEXUS_WINDOW_FOCUSED;
    if ((out_info->flags & NEXUS_WINDOW_MINIMIZED) == 0U)
        out_info->flags |= NEXUS_WINDOW_VISIBLE;
    else
        out_info->flags &= ~NEXUS_WINDOW_VISIBLE;
    return true;
}

nexus_window_id nexus_wm_focused(void) {
    return manager_ready ? focused_id : 0U;
}

nexus_window_id nexus_wm_hit_test(uint32_t x, uint32_t y) {
    if (!manager_ready || x >= display_width || y >= display_height)
        return 0U;
    for (size_t i = window_count; i > 0U; --i) {
        const struct window_slot *slot = &slots[z_order[i - 1U]];
        const struct nexus_window_info *info = &slot->info;
        if (!slot->occupied ||
            (info->flags & NEXUS_WINDOW_MINIMIZED) != 0U) continue;
        if (x >= (uint32_t)info->x && y >= (uint32_t)info->y &&
            x - (uint32_t)info->x < info->width &&
            y - (uint32_t)info->y < info->height) return info->id;
    }
    return 0U;
}

size_t nexus_wm_count(void) {
    return manager_ready ? window_count : 0U;
}

bool nexus_wm_z_order(size_t bottom_to_top_index, nexus_window_id *out_id) {
    if (!manager_ready || out_id == NULL || bottom_to_top_index >= window_count)
        return false;
    const struct window_slot *slot = &slots[z_order[bottom_to_top_index]];
    if (!slot->occupied) return false;
    *out_id = slot->info.id;
    return true;
}
