#ifndef NEXUS_WINDOW_MANAGER_H
#define NEXUS_WINDOW_MANAGER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NEXUS_WM_MAX_WINDOWS 12U
#define NEXUS_WM_MAX_WORKSPACES 4U
#define NEXUS_WM_TITLE_MAX 31U

typedef uint32_t nexus_window_id;

enum nexus_window_flags {
    NEXUS_WINDOW_VISIBLE = 1U << 0,
    NEXUS_WINDOW_MINIMIZED = 1U << 1,
    NEXUS_WINDOW_MAXIMIZED = 1U << 2,
    NEXUS_WINDOW_FOCUSED = 1U << 3
};

struct nexus_window_info {
    nexus_window_id id;
    uint32_t application_id;
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
    int32_t restore_x;
    int32_t restore_y;
    uint32_t restore_width;
    uint32_t restore_height;
    uint32_t flags;
    uint8_t workspace;
    char title[NEXUS_WM_TITLE_MAX + 1U];
};

/*
 * Window-state manager. Coordinates are framebuffer pixels; z-order index
 * zero is the bottom-most window. This module owns metadata only, not pixels
 * or application surfaces.
 */
bool nexus_wm_init(uint32_t screen_width, uint32_t screen_height);
/* Update framebuffer dimensions without discarding live windows. */
bool nexus_wm_set_display_size(uint32_t screen_width, uint32_t screen_height);
bool nexus_wm_create(uint32_t application_id, const char *title,
                     int32_t x, int32_t y, uint32_t width, uint32_t height,
                     nexus_window_id *out_id);
bool nexus_wm_destroy(nexus_window_id id);
bool nexus_wm_set_title(nexus_window_id id, const char *title);
bool nexus_wm_move(nexus_window_id id, int32_t x, int32_t y);
bool nexus_wm_resize(nexus_window_id id, uint32_t width, uint32_t height);
bool nexus_wm_focus(nexus_window_id id);
bool nexus_wm_minimize(nexus_window_id id);
bool nexus_wm_restore(nexus_window_id id);
bool nexus_wm_maximize(nexus_window_id id);
bool nexus_wm_unmaximize(nexus_window_id id);
bool nexus_wm_get(nexus_window_id id, struct nexus_window_info *out_info);
nexus_window_id nexus_wm_focused(void);
nexus_window_id nexus_wm_hit_test(uint32_t x, uint32_t y);
size_t nexus_wm_count(void);
bool nexus_wm_z_order(size_t bottom_to_top_index, nexus_window_id *out_id);

/* Virtual desktop/workspace support. Window IDs and z-order remain global;
 * visibility, focus and hit-testing are scoped to the active workspace. */
uint8_t nexus_wm_active_workspace(void);
bool nexus_wm_switch_workspace(uint8_t workspace);
bool nexus_wm_move_to_workspace(nexus_window_id id, uint8_t workspace);
size_t nexus_wm_workspace_count(uint8_t workspace);
bool nexus_wm_workspace_z_order(uint8_t workspace, size_t bottom_to_top_index,
                                nexus_window_id *out_id);

#endif
