#include "desktop.h"

#include "blockdev.h"
#include "gpt.h"
#include "fat32.h"
#include "vfs.h"
#include "process.h"
#include "net.h"
#include "netdev.h"
#include "pci.h"
#include "usb_desc.h"
#include "wifi_chipset.h"
#include "nxfs.h"

#include "graphics.h"
#include "input.h"
#include "serial.h"
#include "memory.h"
#include "timer.h"
#include "partition_plan.h"
#include "installer_runtime.h"
#include "browser_url.h"
#include "browser_html.h"
#include "browser_search.h"
#include "browser_http.h"
#include "browser_download.h"
#include "calculator.h"
#include "launcher.h"
#include "window_manager.h"

#include <stddef.h>
#include <stdint.h>

enum desktop_view {
    DESKTOP_HOME = 0,
    DESKTOP_FILES,
    DESKTOP_BROWSER,
    DESKTOP_SETTINGS,
    DESKTOP_TERMINAL,
    DESKTOP_MONITOR,
    DESKTOP_CONTROL_CENTER,
    DESKTOP_CALCULATOR,
    DESKTOP_INSTALLER
};

enum desktop_snap_zone {
    DESKTOP_SNAP_LEFT = 0,
    DESKTOP_SNAP_RIGHT,
    DESKTOP_SNAP_MAXIMIZE,
    DESKTOP_SNAP_TOP_LEFT,
    DESKTOP_SNAP_TOP_RIGHT,
    DESKTOP_SNAP_BOTTOM_LEFT,
    DESKTOP_SNAP_BOTTOM_RIGHT,
    DESKTOP_SNAP_RESTORE
};

static void desktop_snap_window(nexus_window_id id,
                                enum desktop_snap_zone zone);

static enum desktop_view active_view = DESKTOP_HOME;
static uint64_t screen_width;
static uint64_t screen_height;
static bool desktop_ready;
static bool installer_environment;
static bool start_menu_open;
static bool workspace_overview_open;
static uint8_t workspace_overview_selected;
static char launcher_search[32];
static size_t launcher_visible_apps[7];
static size_t launcher_visible_count;
static size_t launcher_selected_index;
static const char *const launcher_app_names[7] = {
    "Files", "Nexus Browser", "Settings", "Terminal",
    "System Monitor", "Control Center", "Calculator"
};
static bool window_maximized;
static bool desktop_window_manager_ready;
static nexus_window_id desktop_app_windows[7];
enum desktop_window_drag_mode {
    DESKTOP_WINDOW_DRAG_NONE = 0,
    DESKTOP_WINDOW_DRAG_MOVE,
    DESKTOP_WINDOW_DRAG_RESIZE
};
static enum desktop_window_drag_mode desktop_window_drag_mode;
static nexus_window_id desktop_dragged_window;
static int64_t desktop_drag_offset_x;
static int64_t desktop_drag_offset_y;
static nexus_window_id desktop_last_title_click_window;
static uint64_t desktop_last_title_click_tick;
static uint64_t window_animation_offset;
/* Five coordinated palettes shared by the wallpaper, window chrome and
 * shell accents. The selected index is persisted in desktop.cfg. */
static const uint8_t desktop_palette_colors[5][6] = {
    {5U, 13U, 38U, 18U, 51U, 103U},   /* Midnight */
    {2U, 24U, 43U, 5U, 83U, 100U},    /* Ocean */
    {35U, 12U, 31U, 105U, 34U, 45U},  /* Ember */
    {20U, 13U, 42U, 71U, 42U, 120U},  /* Violet */
    {7U, 28U, 28U, 24U, 91U, 67U}     /* Forest */
};
static uint8_t settings_theme = 3U;
static uint8_t settings_wallpaper;
static bool settings_show_status = true;
static bool settings_high_contrast;
static bool settings_reduce_motion;
static char calculator_expression[NEXUS_CALCULATOR_EXPRESSION_MAX + 1U];
static char calculator_result[32] = "0";
static enum nexus_calculator_error calculator_error = NEXUS_CALCULATOR_OK;
static bool calculator_result_valid;
static bool mouse_left_was_down;
static bool files_last_click_valid;
static size_t files_last_click_index;
static uint64_t files_last_click_tick;
static bool keyboard_left_shift;
static bool keyboard_right_shift;
static bool keyboard_alt;
static bool keyboard_ctrl;
static bool keyboard_caps_lock;
static bool keyboard_caps_lock_down;
static void draw_pointer(void);
static void draw_shell(void);
static void desktop_animate_view(void);
static void erase_pointer(void);
static uint32_t pointer_under[15U][7U];
static bool pointer_under_valid;
static void desktop_reboot_after_install(void);
static char terminal_scancode_char(uint8_t code);
static void scan_files_once(void);
static void refresh_files_listing(void);
static int64_t pointer_x;
static int64_t pointer_y;
static bool files_scanned;
static bool files_scan_ok;
static size_t files_partition_count;
static uint64_t files_first_lba;
static uint64_t files_last_lba;
static bool files_vfs_ready;
#define FILES_EDITOR_MAX_BYTES NEXUS_NXFS_MAX_FILE_BYTES
#define FILES_EDITOR_VIEW_BYTES 256U

#define FILES_LISTING_MAX_ENTRIES 128U
#define FILES_LISTING_VISIBLE 6U

static char files_first_name[NEXUS_VFS_NAME_MAX];
static char files_preview[41];
static char files_listing[FILES_LISTING_MAX_ENTRIES][NEXUS_VFS_NAME_MAX];
static uint8_t files_listing_types[FILES_LISTING_MAX_ENTRIES];
static size_t files_listing_count;
static size_t files_selected_index;
static size_t files_listing_first_index;
static bool files_listing_truncated;
enum files_editor_state {
    FILES_EDITOR_CLOSED = 0,
    FILES_EDITOR_NAME,
    FILES_EDITOR_CONTENT,
    FILES_EDITOR_RENAME,
    FILES_EDITOR_COPY
};
static enum files_editor_state files_editor_state;
static char files_edit_name[NEXUS_VFS_NAME_MAX];
static char files_rename_source[NEXUS_VFS_NAME_MAX];
static char files_copy_source[NEXUS_VFS_NAME_MAX];
static char files_edit_content[FILES_EDITOR_MAX_BYTES + 1U];
static size_t files_edit_name_length;
static size_t files_edit_content_length;
static size_t files_edit_view_offset;
static bool files_edit_follow_end;
static bool files_delete_pending;
static char files_delete_name[NEXUS_VFS_NAME_MAX];
static bool files_search_active;
static char files_search_query[NEXUS_VFS_NAME_MAX];
static char files_search_input[NEXUS_VFS_NAME_MAX];
static char files_edit_status[64];
static char terminal_input[64];
static size_t terminal_input_length;
static enum nexus_firmware_mode installer_firmware = NEXUS_FIRMWARE_UEFI;
static size_t installer_selected_device;
static struct nexus_partition_plan installer_plan;
static bool installer_plan_valid;
static char terminal_output[160];

enum installer_ui_state {
    INSTALLER_UI_SELECT = 0,
    INSTALLER_UI_CONFIRM_ERASE,
    INSTALLER_UI_RUNNING,
    INSTALLER_UI_COMPLETE,
    INSTALLER_UI_FAILED
};
static enum installer_ui_state installer_ui_state = INSTALLER_UI_SELECT;
static char installer_confirmed_id[NEXUS_BLOCKDEV_ID_MAX];
static char installer_status_text[96] = "Select a supported target disk";
/* Destructive installation remains compile-time locked until end-to-end
 * disposable-disk validation is recorded. */

#define NEXUS_BROWSER_LINE_COUNT 64U
#define NEXUS_BROWSER_LINE_WIDTH 72U
#define NEXUS_BROWSER_SOURCE_COUNT 5U
#define NEXUS_BROWSER_RESPONSE_MAX 8192U
static char browser_address[NEXUS_BROWSER_HTML_LINK_MAX];
static size_t browser_address_length;
static char browser_status[80] = "Search the web or enter an HTTP address";
static bool browser_search_mode;
static bool browser_has_page;
static char browser_lines[NEXUS_BROWSER_LINE_COUNT][NEXUS_BROWSER_LINE_WIDTH];
static size_t browser_line_count;
static size_t browser_scroll_offset;
static uint8_t browser_response[NEXUS_BROWSER_RESPONSE_MAX];
static char browser_current_url[NEXUS_BROWSER_SEARCH_URL_MAX];
static bool browser_download_pending;
static char browser_sources[NEXUS_BROWSER_SOURCE_COUNT]
                          [NEXUS_BROWSER_HTML_LINK_MAX];
static size_t browser_source_count;

static uint64_t clamp_min(uint64_t value, uint64_t minimum) {
    return value < minimum ? minimum : value;
}

static void format_u64(uint64_t value, char output[21]) {
    char reverse[20];
    size_t count = 0;
    do {
        reverse[count++] = (char)('0' + (value % 10U));
        value /= 10U;
    } while (value != 0U && count < sizeof(reverse));
    for (size_t i = 0; i < count; ++i)
        output[i] = reverse[count - i - 1U];
    output[count] = '\0';
}

static void format_labeled_u64(const char *label, uint64_t value,
                                 char output[64]) {
    size_t length = 0U;
    while (label[length] != '\0' && length < 63U) {
        output[length] = label[length];
        ++length;
    }
    char number[21];
    format_u64(value, number);
    for (size_t i = 0; number[i] != '\0' && length < 63U; ++i)
        output[length++] = number[i];
    output[length] = '\0';
}

/* Draw the Nexus orb directly into the framebuffer. The same mark is
 * used by the boot splash, taskbar, launcher and first-party app chrome so
 * the product identity does not depend on a filesystem image decoder. */
static void desktop_draw_logo(uint64_t x, uint64_t y, uint64_t size) {
    if (size < 8U) return;
    const int64_t center = (int64_t)(size / 2U);
    const int64_t radius = (int64_t)(size * 43U / 100U);
    const int64_t glow_radius = radius + (int64_t)(size / 10U);
    for (int64_t py = -glow_radius; py <= glow_radius; ++py) {
        for (int64_t px = -glow_radius; px <= glow_radius; ++px) {
            const uint64_t d2 = (uint64_t)(px * px + py * py);
            const uint64_t r2 = (uint64_t)(radius * radius);
            if (d2 > (uint64_t)(glow_radius * glow_radius)) continue;
            uint8_t red, green, blue;
            if (d2 > r2) {
                red = 25U; green = 10U; blue = 68U;
            } else {
                const uint64_t edge = d2 * 255U / (r2 == 0U ? 1U : r2);
                red = (uint8_t)(96U + (edge * 98U / 255U));
                green = (uint8_t)(35U + (edge * 116U / 255U));
                blue = (uint8_t)(255U - (edge * 4U / 255U));
                if (py > radius / 2) {
                    const uint8_t lift = (uint8_t)((uint64_t)(py - radius / 2) *
                        42U / (uint64_t)(radius + 1));
                    red = (uint8_t)(red + lift);
                    green = (uint8_t)(green + lift);
                    blue = (uint8_t)(blue > lift ? blue : 255U);
                }
            }
            const int64_t sx = (int64_t)x + center + px;
            const int64_t sy = (int64_t)y + center + py;
            if (sx >= 0 && sy >= 0)
                (void)graphics_write_rgb_pixel((uint64_t)sx, (uint64_t)sy,
                                               red, green, blue);
        }
    }
    const uint64_t eye_width = size / 8U > 2U ? size / 8U : 2U;
    const uint64_t eye_height = size * 23U / 100U;
    const uint64_t eye_y = y + size * 34U / 100U;
    const uint64_t eye_left = x + size * 24U / 100U;
    const uint64_t eye_right = x + size * 59U / 100U;
    graphics_fill_rect(eye_left, eye_y, eye_width, eye_height, 255U, 255U, 255U);
    graphics_fill_rect(eye_right, eye_y, eye_width, eye_height, 255U, 255U, 255U);
    /* Soft rounded caps retain the character of the supplied reference. */
    for (uint64_t cap = 0U; cap < eye_width / 2U; ++cap) {
        const uint64_t inset = eye_width / 2U - cap;
        graphics_fill_rect(eye_left + inset, eye_y + cap,
                           eye_width - 2U * inset, 1U, 255U, 255U, 255U);
        graphics_fill_rect(eye_right + inset, eye_y + cap,
                           eye_width - 2U * inset, 1U, 255U, 255U, 255U);
        graphics_fill_rect(eye_left + inset, eye_y + eye_height - cap - 1U,
                           eye_width - 2U * inset, 1U, 255U, 255U, 255U);
        graphics_fill_rect(eye_right + inset, eye_y + eye_height - cap - 1U,
                           eye_width - 2U * inset, 1U, 255U, 255U, 255U);
    }
}

static void desktop_draw_app_icon(uint64_t x, uint64_t y,
                                     uint64_t size, size_t app) {
    static const uint8_t accent[7][3] = {
        {71U, 153U, 232U}, {45U, 185U, 202U}, {133U, 117U, 230U},
        {224U, 151U, 93U}, {66U, 188U, 151U}, {151U, 117U, 224U},
        {85U, 157U, 215U}
    };
    if (app >= 7U || size < 12U) return;
    graphics_fill_round_rect(x, y, size, size, size / 4U,
                             accent[app][0], accent[app][1], accent[app][2]);
    const uint64_t pad = size / 5U;
    const uint64_t inner = size - pad * 2U;
    if (app == 0U) {
        graphics_fill_round_rect(x + pad, y + size / 3U,
                                 inner, size / 2U, size / 10U,
                                 239U, 248U, 255U);
        graphics_fill_round_rect(x + pad, y + size / 4U,
                                 inner / 2U, size / 5U, size / 12U,
                                 239U, 248U, 255U);
    } else if (app == 1U) {
        graphics_draw_round_rect(x + pad, y + pad, inner, inner,
                                 size / 9U, 245U, 253U, 255U);
        graphics_fill_rect(x + pad + 1U, y + pad + inner / 3U,
                           inner - 2U, size / 12U, 245U, 253U, 255U);
        graphics_fill_rect(x + size / 2U - 1U, y + pad + inner / 2U,
                           2U, inner / 2U, 245U, 253U, 255U);
        graphics_fill_rect(x + pad, y + size / 2U,
                           inner, 2U, 245U, 253U, 255U);
    } else if (app == 2U) {
        graphics_draw_round_rect(x + pad, y + pad, inner, inner,
                                 size / 3U, 248U, 250U, 255U);
        graphics_fill_round_rect(x + size / 2U - size / 9U,
                                 y + size / 2U - size / 9U,
                                 size / 4U, size / 4U, size / 8U,
                                 248U, 250U, 255U);
    } else if (app == 3U) {
        graphics_fill_round_rect(x + pad, y + pad, inner, inner,
                                 size / 10U, 18U, 35U, 58U);
        graphics_draw_text(x + pad + size / 8U,
                           y + size / 2U - 3U, ">", 1U,
                           137U, 225U, 205U);
    } else if (app == 4U) {
        graphics_fill_round_rect(x + pad, y + pad, inner,
                                 inner * 2U / 3U, size / 10U,
                                 20U, 36U, 59U);
        graphics_fill_rect(x + pad + size / 7U, y + size * 3U / 5U,
                           size / 8U, size / 6U, 131U, 231U, 207U);
        graphics_fill_rect(x + size / 2U, y + size / 2U,
                           size / 8U, size / 4U, 131U, 231U, 207U);
        graphics_fill_rect(x + size * 2U / 3U, y + size * 5U / 12U,
                           size / 8U, size / 3U, 131U, 231U, 207U);
        graphics_fill_rect(x + size / 3U, y + size * 4U / 5U,
                           size / 3U, 2U, 245U, 253U, 255U);
    } else if (app == 5U) {
        /* Control Center: adjustment rails and slider knobs. */
        for (uint64_t rail = 0U; rail < 3U; ++rail) {
            const uint64_t ry = y + size / 3U + rail * (size / 5U);
            graphics_fill_round_rect(x + pad, ry, inner, 2U, 1U,
                                     236U, 243U, 255U);
        }
        graphics_fill_round_rect(x + size / 3U, y + size / 4U,
                                 size / 5U, size / 5U, size / 10U,
                                 255U, 255U, 255U);
        graphics_fill_round_rect(x + size * 3U / 5U, y + size * 9U / 20U,
                                 size / 5U, size / 5U, size / 10U,
                                 255U, 255U, 255U);
        graphics_fill_round_rect(x + size / 4U, y + size * 13U / 20U,
                                 size / 5U, size / 5U, size / 10U,
                                 255U, 255U, 255U);
    } else {
        graphics_draw_text_smooth(x + size / 4U, y + size / 3U, "=", 2U,
                           255U, 255U, 255U);
        graphics_fill_rect(x + size / 4U, y + size * 2U / 3U,
                           size / 2U, 2U, 255U, 255U, 255U);
    }
}

static void format_ipv4_address(const uint8_t address[4], char output[16]) {
    size_t length = 0U;
    for (size_t part = 0; part < 4U; ++part) {
        char number[21];
        format_u64(address[part], number);
        for (size_t i = 0; number[i] != '\0' && length < 15U; ++i)
            output[length++] = number[i];
        if (part != 3U && length < 15U) output[length++] = '.';
    }
    output[length] = '\0';
}

static void installer_refresh_plan(void) {
    installer_plan_valid = false;
    const size_t count = blockdev_count();
    if (count == 0U) {
        installer_selected_device = 0U;
        return;
    }
    if (installer_selected_device >= count) installer_selected_device = 0U;
    const struct nexus_block_device *device =
        blockdev_at(installer_selected_device);
    if (device == NULL) return;
    const enum nexus_firmware_mode layout =
        installer_firmware == NEXUS_FIRMWARE_LEGACY_BIOS
            ? NEXUS_FIRMWARE_DUAL : installer_firmware;
    installer_plan_valid = nexus_partition_plan_create(
        device->block_count, device->block_size, layout, &installer_plan);
}

/* Record the exact target whenever the user changes the drive selection.
 * This makes installer diagnostics and automated UI verification match the
 * device that will be revalidated at the destructive confirmation boundary.
 */
static void installer_log_selected_target(void) {
    const struct nexus_block_device *device =
        blockdev_at(installer_selected_device);
    const char *stable_id = device != NULL ? device->stable_id : "NONE";
    /* Keep both diagnostic prefixes for NVMe and SATA CI consumers. */
    serial_write("NEXUS INSTALLER TARGET: ");
    serial_write(stable_id);
    serial_write("\r\n");
    serial_write("NEXUS INSTALLER TARGET SELECTED: ");
    serial_write(stable_id);
    serial_write("\r\n");
}


static const char *installer_target_status(
    const struct nexus_block_device *device) {
    if (device == NULL) return "BLOCKED: NO DRIVE SELECTED";
    if (!blockdev_boot_media_detection_complete())
        return "BLOCKED: BOOT SOURCE IDENTITY UNKNOWN";
    if (blockdev_is_boot_media(device))
        return "BLOCKED: THIS IS THE BOOT DRIVE";
    if (blockdev_is_mounted(device))
        return "BLOCKED: DRIVE IS CURRENTLY MOUNTED";
    if (!blockdev_identity_is_unique(device))
        return "BLOCKED: DRIVE IDENTITY IS NOT UNIQUE";
    if (!blockdev_install_write_supported(device))
        return "BLOCKED: GUARDED WRITE PATH NOT SUPPORTED";
    return "SAFE CANDIDATE - READY FOR EXPLICIT CONFIRMATION";
}


static enum desktop_view desktop_view_for_app(size_t app_index) {
    switch (app_index) {
        case 0U: return DESKTOP_FILES;
        case 1U: return DESKTOP_BROWSER;
        case 2U: return DESKTOP_SETTINGS;
        case 3U: return DESKTOP_TERMINAL;
        case 4U: return DESKTOP_MONITOR;
        case 5U: return DESKTOP_CONTROL_CENTER;
        case 6U: return DESKTOP_CALCULATOR;
        default: return DESKTOP_INSTALLER;
    }
}

static bool settings_persisted;
static void desktop_open_app(size_t app_index);

/* Settings are stored as a tiny versioned record on the writable NXFS Data
 * volume. A checksum and strict range checks keep corrupt or old records
 * from changing the desktop into an invalid state. */
static uint8_t desktop_settings_checksum(const uint8_t *record, size_t length) {
    uint8_t checksum = 0xA5U;
    for (size_t i = 0U; i < length; ++i) checksum ^= record[i];
    return checksum;
}

static bool desktop_save_settings(void) {
    if (!vfs_nxfs_writable()) return false;
    uint8_t record[11] = {
        'N', 'X', 'S', '2', settings_theme, settings_wallpaper,
        settings_show_status ? 1U : 0U, window_maximized ? 1U : 0U,
        settings_high_contrast ? 1U : 0U,
        settings_reduce_motion ? 1U : 0U, 0U
    };
    record[10] = desktop_settings_checksum(record, 10U);
    const bool saved = vfs_write_file("desktop.cfg", record, sizeof(record));
    settings_persisted = saved;
    if (saved) serial_write("DESKTOP: settings saved to persistent NXFS\r\n");
    else serial_write("DESKTOP: settings save failed\r\n");
    return saved;
}

static void desktop_settings_activate(size_t option) {
    switch (option) {
        case 0U: settings_theme = (uint8_t)((settings_theme + 1U) % 5U) ; break;
        case 1U: settings_wallpaper = (uint8_t)((settings_wallpaper + 1U) % 5U); break;
        case 2U: settings_show_status = !settings_show_status; break;
        case 3U: window_maximized = !window_maximized; break;
        case 4U: {
            const uint32_t count = netdev_device_count();
            if (count > 1U) {
                const uint32_t next = (netdev_default_index() + 1U) % count;
                if (!net_select_device(next))
                    serial_write("NETWORK: adapter switch failed; previous interface restored\r\n");
            } else if (count == 1U) {
                (void)net_select_device(0U);
            } else {
                serial_write("NETWORK: no supported adapter driver is installed\r\n");
            }
            break;
        }
        case 5U: settings_high_contrast = !settings_high_contrast; break;
        case 6U: settings_reduce_motion = !settings_reduce_motion; break;
        default: return;
    }
    (void)desktop_save_settings();
}

static void desktop_control_center_activate(size_t option) {
    switch (option) {
        case 0U: desktop_settings_activate(0U); break;
        case 1U: desktop_settings_activate(1U); break;
        case 2U: desktop_settings_activate(5U); break;
        case 3U: desktop_settings_activate(6U); break;
        case 4U: desktop_settings_activate(2U); break;
        case 5U:
            active_view = DESKTOP_SETTINGS;
            start_menu_open = false;
            desktop_animate_view();
            break;
        default: return;
    }
}

static void desktop_load_settings(void) {
    if (!files_scanned) scan_files_once();
    if (!vfs_nxfs_mounted()) return;
    struct nexus_vfs_file file;
    if (!vfs_open_readonly("desktop.cfg", &file)) return;
    uint32_t size = 0U;
    uint8_t record[11] = {0U};
    size_t received = 0U;
    const bool size_ok = vfs_file_size(&file, &size) &&
                         (size == 9U || size == sizeof(record));
    const bool read_ok = size_ok && vfs_read(&file, record, size, &received);
    vfs_close(&file);
    const bool legacy = size == 9U && record[3] == '1';
    const bool current = size == sizeof(record) && record[3] == '2';
    const size_t checksum_offset = legacy ? 8U : 10U;
    if (!read_ok || received != size ||
        record[0] != 'N' || record[1] != 'X' || record[2] != 'S' ||
        (!legacy && !current) || record[4] > 4U || record[5] > 4U ||
        record[6] > 1U || record[7] > 1U ||
        (current && (record[8] > 1U || record[9] > 1U)) ||
        record[checksum_offset] != desktop_settings_checksum(record, checksum_offset)) {
        serial_write("DESKTOP: ignored invalid settings record\r\n");
        return;
    }
    settings_theme = record[4];
    settings_wallpaper = record[5];
    settings_show_status = record[6] != 0U;
    window_maximized = record[7] != 0U;
    settings_high_contrast = current && record[8] != 0U;
    settings_reduce_motion = current && record[9] != 0U;
    settings_persisted = true;
    serial_write("DESKTOP: restored settings from persistent NXFS\r\n");
}

static bool desktop_view_app_index(enum desktop_view view, size_t *index) {
    if (view < DESKTOP_FILES || view > DESKTOP_CALCULATOR || index == NULL)
        return false;
    *index = (size_t)(view - DESKTOP_FILES);
    return *index < 7U;
}

static bool desktop_app_window_info(size_t app,
                                    struct nexus_window_info *info) {
    if (!desktop_window_manager_ready || app >= 7U ||
        desktop_app_windows[app] == 0U || info == NULL) return false;
    if (!nexus_wm_get(desktop_app_windows[app], info)) {
        desktop_app_windows[app] = 0U;
        return false;
    }
    return true;
}

static void desktop_window_work_area(uint64_t *top, uint64_t *bottom,
                                     uint32_t *height) {
    const uint64_t t = clamp_min(screen_height / 16U, 34U);
    const uint64_t b = clamp_min(screen_height / 11U, 48U);
    const uint64_t available = screen_height > t + b
        ? screen_height - t - b : 1U;
    if (top != NULL) *top = t;
    if (bottom != NULL) *bottom = b;
    if (height != NULL)
        *height = (uint32_t)(available > 0xFFFFFFFFU
            ? 0xFFFFFFFFU : available);
}

static void desktop_sync_focused_window(void) {
    const nexus_window_id focused = nexus_wm_focused();
    struct nexus_window_info info;
    if (focused != 0U && nexus_wm_get(focused, &info) &&
        info.application_id >= (uint32_t)DESKTOP_FILES &&
        info.application_id <= (uint32_t)DESKTOP_CALCULATOR) {
        active_view = (enum desktop_view)info.application_id;
        window_maximized = (info.flags & NEXUS_WINDOW_MAXIMIZED) != 0U;
        return;
    }
    active_view = DESKTOP_HOME;
    window_maximized = false;
}

static void desktop_switch_workspace(uint8_t workspace) {
    if (!desktop_window_manager_ready ||
        !nexus_wm_switch_workspace(workspace)) return;
    start_menu_open = false;
    workspace_overview_open = false;
    desktop_window_drag_mode = DESKTOP_WINDOW_DRAG_NONE;
    desktop_dragged_window = 0U;
    desktop_sync_focused_window();
    desktop_animate_view();
}

static void desktop_cycle_workspace(int direction) {
    const uint8_t current = nexus_wm_active_workspace();
    const uint8_t next = direction < 0
        ? (uint8_t)((current + NEXUS_WM_MAX_WORKSPACES - 1U) %
                    NEXUS_WM_MAX_WORKSPACES)
        : (uint8_t)((current + 1U) % NEXUS_WM_MAX_WORKSPACES);
    desktop_switch_workspace(next);
}

static void desktop_move_focused_workspace(int direction) {
    const nexus_window_id focused = nexus_wm_focused();
    struct nexus_window_info info;
    if (focused == 0U || !nexus_wm_get(focused, &info)) return;
    const uint8_t current = info.workspace;
    const uint8_t target = direction < 0
        ? (uint8_t)((current + NEXUS_WM_MAX_WORKSPACES - 1U) %
                    NEXUS_WM_MAX_WORKSPACES)
        : (uint8_t)((current + 1U) % NEXUS_WM_MAX_WORKSPACES);
    if (nexus_wm_move_to_workspace(focused, target)) {
        workspace_overview_selected = target;
        desktop_sync_focused_window();
        desktop_animate_view();
    }
}

static void desktop_get_current_window_rect(uint64_t *x, uint64_t *y,
                                            uint64_t *width,
                                            uint64_t *height) {
    const uint64_t top = clamp_min(screen_height / 16U, 34U);
    const uint64_t bottom = clamp_min(screen_height / 11U, 48U);
    size_t app = 0U;
    struct nexus_window_info info;
    if (!installer_environment &&
        desktop_view_app_index(active_view, &app) &&
        desktop_app_window_info(app, &info)) {
        if (x != NULL) *x = (uint64_t)info.x;
        if (y != NULL) *y = top + (uint64_t)info.y;
        if (width != NULL) *width = info.width;
        if (height != NULL) *height = info.height;
        return;
    }

    uint64_t w = screen_width * 3U / 4U;
    uint64_t h = active_view == DESKTOP_HOME
        ? screen_height * 53U / 100U : 512U;
    if (w > 920U) w = 920U;
    if (h > 512U) h = 512U;
    if (w > screen_width - 16U) w = screen_width - 16U;
    if (h > screen_height - top - bottom - 16U)
        h = screen_height - top - bottom - 16U;
    if (window_maximized && active_view != DESKTOP_HOME) {
        w = screen_width - 32U;
        h = screen_height - top - bottom - 16U;
    }
    const uint64_t left = (screen_width - w) / 2U;
    const uint64_t upper = window_maximized && active_view != DESKTOP_HOME
        ? top + 8U : top + (screen_height - top - bottom - h) / 2U;
    if (x != NULL) *x = left;
    if (y != NULL) *y = upper;
    if (width != NULL) *width = w;
    if (height != NULL) *height = h;
}

static void desktop_return_home(void) {
    if (installer_environment) return;
    if (desktop_window_manager_ready) {
        /* Home is a separate shell layer. Minimize every application window
         * before showing it, preserving their taskbar entries for restore. */
        const size_t count = nexus_wm_count();
        for (size_t i = 0U; i < count; ++i) {
            nexus_window_id id = 0U;
            struct nexus_window_info info;
            if (nexus_wm_z_order(i, &id) && nexus_wm_get(id, &info) &&
                info.workspace == nexus_wm_active_workspace() &&
                (info.flags & NEXUS_WINDOW_MINIMIZED) == 0U)
                (void)nexus_wm_minimize(id);
        }
    }
    active_view = DESKTOP_HOME;
    window_maximized = false;
    start_menu_open = false;
}

static void desktop_close_window(nexus_window_id id) {
    struct nexus_window_info info;
    if (!desktop_window_manager_ready || id == 0U ||
        !nexus_wm_get(id, &info)) return;
    if (info.application_id >= (uint32_t)DESKTOP_FILES &&
        info.application_id <= (uint32_t)DESKTOP_CALCULATOR) {
        const size_t app = (size_t)(info.application_id - DESKTOP_FILES);
        if (app < 7U && desktop_app_windows[app] == id)
            desktop_app_windows[app] = 0U;
    }
    (void)nexus_wm_destroy(id);
    desktop_sync_focused_window();
}

static void desktop_close_focused_window(void) {
    if (!desktop_window_manager_ready) {
        desktop_return_home();
        return;
    }
    desktop_close_window(nexus_wm_focused());
}

static void desktop_toggle_app(size_t app) {
    struct nexus_window_info info;
    if (app >= 7U) return;
    if (!desktop_app_window_info(app, &info)) {
        desktop_open_app(app);
        return;
    }
    if (info.workspace != nexus_wm_active_workspace()) {
        desktop_switch_workspace(info.workspace);
        if (!desktop_app_window_info(app, &info)) return;
    }
    if (info.id == nexus_wm_focused() &&
        (info.flags & NEXUS_WINDOW_MINIMIZED) == 0U) {
        (void)nexus_wm_minimize(info.id);
        desktop_sync_focused_window();
    } else {
        (void)nexus_wm_restore(info.id);
        (void)nexus_wm_focus(info.id);
        desktop_sync_focused_window();
        if (active_view == DESKTOP_FILES) {
            scan_files_once();
            refresh_files_listing();
        }
    }
    start_menu_open = false;
    desktop_animate_view();
}

static bool desktop_handle_window_mouse_click(void) {
    if (!desktop_window_manager_ready || installer_environment) return false;
    const uint64_t top = clamp_min(screen_height / 16U, 34U);
    const uint64_t bottom = clamp_min(screen_height / 11U, 48U);
    if (pointer_y < (int64_t)top ||
        pointer_y >= (int64_t)(screen_height - bottom) ||
        pointer_x < 0 || pointer_y < 0) return false;

    const nexus_window_id target = nexus_wm_hit_test(
        (uint32_t)pointer_x, (uint32_t)(pointer_y - (int64_t)top));
    if (target == 0U) return false;
    struct nexus_window_info info;
    if (!nexus_wm_get(target, &info)) return false;

    const bool changed_focus = target != nexus_wm_focused();
    if (changed_focus) {
        if (!nexus_wm_focus(target)) return false;
        desktop_sync_focused_window();
        start_menu_open = false;
        if (active_view == DESKTOP_FILES) {
            scan_files_once();
            refresh_files_listing();
        }
    }

    const int64_t window_x = info.x;
    const int64_t window_y = (int64_t)top + info.y;
    const int64_t local_x = pointer_x - window_x;
    const int64_t local_y = pointer_y - window_y;
    if (local_y >= 0 && local_y < 36) {
        const int64_t controls_x = (int64_t)info.width - 66;
        if (local_x >= controls_x && local_x < controls_x + 64) {
            const int64_t control = local_x - controls_x;
            if (control < 18) {
                (void)nexus_wm_minimize(target);
                desktop_sync_focused_window();
            } else if (control >= 22 && control < 42) {
                if ((info.flags & NEXUS_WINDOW_MAXIMIZED) != 0U)
                    (void)nexus_wm_unmaximize(target);
                else
                    (void)nexus_wm_maximize(target);
                desktop_sync_focused_window();
                (void)desktop_save_settings();
            } else if (control >= 44) {
                desktop_close_window(target);
            }
            desktop_animate_view();
            return true;
        }
        const uint64_t now = timer_ticks();
        if (desktop_last_title_click_window == target &&
            now >= desktop_last_title_click_tick &&
            now - desktop_last_title_click_tick <= 45U) {
            desktop_last_title_click_window = 0U;
            desktop_snap_window(target,
                (info.flags & NEXUS_WINDOW_MAXIMIZED) != 0U
                    ? DESKTOP_SNAP_RESTORE : DESKTOP_SNAP_MAXIMIZE);
            desktop_animate_view();
            return true;
        }
        desktop_last_title_click_window = target;
        desktop_last_title_click_tick = now;
        desktop_window_drag_mode = DESKTOP_WINDOW_DRAG_MOVE;
        desktop_dragged_window = target;
        desktop_drag_offset_x = local_x;
        desktop_drag_offset_y = local_y;
        return true;
    }

    if (target == nexus_wm_focused() &&
        local_x >= (int64_t)info.width - 20 &&
        local_y >= (int64_t)info.height - 20) {
        desktop_window_drag_mode = DESKTOP_WINDOW_DRAG_RESIZE;
        desktop_dragged_window = target;
        return true;
    }
    return changed_focus;
}

static void desktop_update_window_drag(void) {
    struct nexus_window_info info;
    if (desktop_dragged_window == 0U ||
        !nexus_wm_get(desktop_dragged_window, &info)) {
        desktop_window_drag_mode = DESKTOP_WINDOW_DRAG_NONE;
        desktop_dragged_window = 0U;
        return;
    }
    const uint64_t top = clamp_min(screen_height / 16U, 34U);
    if (desktop_window_drag_mode == DESKTOP_WINDOW_DRAG_MOVE) {
        int64_t x = pointer_x - desktop_drag_offset_x;
        int64_t y = pointer_y - (int64_t)top - desktop_drag_offset_y;
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        if (x > 0x7FFFFFFFLL) x = 0x7FFFFFFFLL;
        if (y > 0x7FFFFFFFLL) y = 0x7FFFFFFFLL;
        (void)nexus_wm_move(desktop_dragged_window, (int32_t)x, (int32_t)y);
    } else if (desktop_window_drag_mode == DESKTOP_WINDOW_DRAG_RESIZE) {
        int64_t width = pointer_x - info.x;
        int64_t height = pointer_y - (int64_t)top - info.y;
        uint32_t work_height_value = 0U;
        desktop_window_work_area(NULL, NULL, &work_height_value);
        const uint64_t work_height = work_height_value;
        const uint64_t min_width = screen_width < 560U ? screen_width : 560U;
        const uint64_t min_height = work_height < 440U ? work_height : 440U;
        if (width < (int64_t)min_width) width = (int64_t)min_width;
        if (height < (int64_t)min_height) height = (int64_t)min_height;
        if (width > (int64_t)screen_width) width = (int64_t)screen_width;
        if (height > (int64_t)work_height) height = (int64_t)work_height;
        (void)nexus_wm_resize(desktop_dragged_window,
                              (uint32_t)width, (uint32_t)height);
    }
}

static bool desktop_draw_managed_windows(uint64_t top_bar);

static void desktop_open_view(enum desktop_view view) {
    if (view == DESKTOP_INSTALLER && !installer_environment) return;
    if (view == DESKTOP_HOME) {
        desktop_return_home();
        desktop_animate_view();
        return;
    }
    size_t app = 0U;
    if (desktop_window_manager_ready &&
        desktop_view_app_index(view, &app)) {
        struct nexus_window_info info;
        if (!desktop_app_window_info(app, &info)) {
            uint64_t top = 0U, bottom = 0U;
            uint32_t work_height = 0U;
            desktop_window_work_area(&top, &bottom, &work_height);
            uint64_t w = screen_width * 3U / 4U;
            uint64_t h = (uint64_t)work_height * 4U / 5U;
            if (w > 1120U) w = 1120U;
            if (h > 660U) h = 660U;
            if (w > screen_width) w = screen_width;
            if (h > work_height) h = work_height;
            const uint64_t x_offset = (uint64_t)(app % 3U) * 22U;
            const uint64_t y_offset = 18U + (uint64_t)(app % 4U) * 20U;
            uint64_t x = (screen_width - w) / 2U + x_offset;
            uint64_t y = y_offset;
            if (x + w > screen_width) x = screen_width - w;
            if (y + h > work_height) y = work_height - h;
            if (!nexus_wm_create((uint32_t)view, launcher_app_names[app],
                                 (int32_t)x, (int32_t)y,
                                 (uint32_t)w, (uint32_t)h,
                                 &desktop_app_windows[app])) {
                desktop_window_manager_ready = false;
            }
        } else {
            if (info.workspace != nexus_wm_active_workspace()) {
                desktop_switch_workspace(info.workspace);
                (void)desktop_app_window_info(app, &info);
            }
            if ((info.flags & NEXUS_WINDOW_MINIMIZED) != 0U)
                (void)nexus_wm_restore(info.id);
            else
                (void)nexus_wm_focus(info.id);
        }
        struct nexus_window_info focused_info;
        if (desktop_window_manager_ready &&
            nexus_wm_get(nexus_wm_focused(), &focused_info)) {
            active_view = view;
            window_maximized =
                (focused_info.flags & NEXUS_WINDOW_MAXIMIZED) != 0U;
        } else {
            active_view = view;
        }
        start_menu_open = false;
        if (view == DESKTOP_FILES) {
            scan_files_once();
            refresh_files_listing();
        }
        desktop_animate_view();
        return;
    }
    if (active_view == view) return;
    active_view = view;
    start_menu_open = false;
    if (view == DESKTOP_INSTALLER) {
        installer_refresh_plan();
        installer_log_selected_target();
    }
    desktop_animate_view();
}

static void desktop_open_app(size_t app_index) {
    if (app_index >= 7U) return;
    desktop_open_view(desktop_view_for_app(app_index));
}

static void desktop_launcher_refresh(void) {
    launcher_visible_count = nexus_launcher_filter(
        launcher_search, launcher_app_names, 7U,
        launcher_visible_apps, 7U);
    if (launcher_visible_count == 0U) {
        launcher_selected_index = 0U;
    } else if (launcher_selected_index >= launcher_visible_count) {
        launcher_selected_index = launcher_visible_count - 1U;
    }
}

static void desktop_launcher_reset(void) {
    launcher_search[0] = '\0';
    launcher_selected_index = 0U;
    desktop_launcher_refresh();
}

static void desktop_launcher_open_selected(void) {
    if (launcher_visible_count == 0U) return;
    desktop_open_app(launcher_visible_apps[launcher_selected_index]);
}

static uint64_t desktop_taskbar_apps_start(void) {
    /* Six pinned apps in the dock; Control Center has its own right-side
     * quick-access surface so it is not duplicated in the icon row. */
    const uint64_t apps_width = 6U * 42U;
    return screen_width > apps_width + 96U
        ? (screen_width - apps_width) / 2U : 56U;
}
static uint64_t desktop_taskbar_control_x(void) {
    return screen_width > 52U ? screen_width - 50U : 0U;
}
static void desktop_draw_taskbar(uint64_t bottom_bar) {
    const uint64_t y = screen_height - bottom_bar;
    const uint64_t button_y = y + 2U;
    const uint64_t button_h = bottom_bar - 2U;
    graphics_fill_round_rect(10U,y+4U,screen_width-20U,bottom_bar-8U,
                             16U,10U,17U,31U);
    graphics_draw_round_rect(10U,y+4U,screen_width-20U,bottom_bar-8U,
                             16U,66U,88U,119U);
    graphics_fill_round_rect(18U,y+7U,screen_width-36U,2U,1U,
                             58U,124U,158U);
    graphics_fill_round_rect(15U,button_y+4U,38U,
        button_h>8U?button_h-8U:button_h,11U,
        start_menu_open?36U:22U,start_menu_open?83U:39U,
        start_menu_open?132U:59U);
    desktop_draw_logo(23U,button_y+(button_h>24U?11U:4U),22U);

    const uint64_t apps_start=desktop_taskbar_apps_start();
    for(size_t i=0U;i<7U;++i) {
        if(i==5U) continue;
        const size_t visible_index=i<5U?i:i-1U;
        const uint64_t x=apps_start+(uint64_t)visible_index*42U;
        struct nexus_window_info app_info;
        const bool running = desktop_app_window_info(i, &app_info);
        const bool other_workspace = running &&
            app_info.workspace != nexus_wm_active_workspace();
        const bool selected = active_view == desktop_view_for_app(i) &&
            running && app_info.id == nexus_wm_focused();
        graphics_fill_round_rect(x+2U,button_y+3U,36U,
            button_h>8U?button_h-8U:button_h,10U,
            selected?39U:(running?29U:20U),
            selected?67U:(running?49U:34U),
            selected?94U:(running?70U:53U));
        if(selected || running) graphics_draw_round_rect(x+2U,button_y+3U,36U,
            button_h>8U?button_h-8U:button_h,10U,
            selected?101U:(other_workspace?139U:72U),
            selected?143U:(other_workspace?105U:111U),
            selected?184U:(other_workspace?205U:145U));
        desktop_draw_app_icon(x+10U,
            button_y+(button_h>22U?(button_h-20U)/2U:1U),20U,i);
        if(selected) graphics_fill_round_rect(x+13U,y+bottom_bar-6U,
                                               14U,2U,1U,108U,215U,235U);
        else if(running) graphics_fill_round_rect(x+17U,y+bottom_bar-6U,
            6U,2U,1U,other_workspace?177U:106U,
            other_workspace?132U:173U,other_workspace?220U:204U);
    }
    const uint64_t control_x=desktop_taskbar_control_x();
    graphics_fill_round_rect(control_x+2U,button_y+3U,38U,
        button_h>8U?button_h-8U:button_h,11U,
        active_view==DESKTOP_CONTROL_CENTER?39U:20U,
        active_view==DESKTOP_CONTROL_CENTER?67U:34U,
        active_view==DESKTOP_CONTROL_CENTER?94U:53U);
    desktop_draw_app_icon(control_x+11U,
        button_y+(button_h>22U?(button_h-20U)/2U:1U),20U,5U);
    if(settings_show_status && screen_width>820U) {
        char uptime[21]; format_u64(timer_ticks()/100U,uptime);
        graphics_draw_text(screen_width-142U,button_y+16U,"UP",
                           1U,137U,167U,192U);
        graphics_draw_text(screen_width-119U,button_y+16U,uptime,
                           1U,235U,243U,250U);
    }
}static void desktop_draw_start_menu(uint64_t bottom_bar) {
    if(!start_menu_open) return;
    const uint64_t menu_width=screen_width>500U?460U:screen_width-16U;
    const uint64_t menu_height=360U;
    const uint64_t menu_x=(screen_width-menu_width)/2U;
    const uint64_t menu_bottom=screen_height>bottom_bar
        ?screen_height-bottom_bar:0U;
    const uint64_t top_limit=clamp_min(screen_height/16U,34U)+8U;
    uint64_t menu_y=menu_bottom>menu_height
        ?menu_bottom-menu_height+2U:top_limit;
    if(menu_y<top_limit) menu_y=top_limit;
    desktop_launcher_refresh();
    graphics_fill_round_rect(menu_x+7U,menu_y+10U,menu_width,menu_height,
                             22U,1U,5U,16U);
    graphics_fill_gradient_rect(menu_x,menu_y,menu_width,menu_height,
                                27U,39U,57U,14U,23U,39U);
    graphics_draw_round_rect(menu_x,menu_y,menu_width,menu_height,
                             22U,76U,105U,135U);
    graphics_fill_gradient_rect(menu_x+1U,menu_y+1U,menu_width-2U,3U,
                                 71U,164U,211U,133U,111U,224U);
    desktop_draw_logo(menu_x+20U,menu_y+16U,24U);
    graphics_draw_text_smooth(menu_x+56U,menu_y+19U,"NEXUS", 2U,246U,249U,255U);
    graphics_draw_text(menu_x+57U,menu_y+41U,"FIND YOUR NEXT TASK",1U,
                       151U,181U,205U);

    const uint64_t search_x=menu_x+18U;
    const uint64_t search_y=menu_y+59U;
    const uint64_t search_w=menu_width-36U;
    graphics_fill_round_rect(search_x,search_y,search_w,32U,9U,
                             9U,20U,34U);
    graphics_draw_round_rect(search_x,search_y,search_w,32U,9U,
                             75U,126U,157U);
    graphics_draw_text(search_x+11U,search_y+20U,"SEARCH",1U,
                       116U,207U,222U);
    graphics_draw_text(search_x+70U,search_y+20U,
                       launcher_search[0] ? launcher_search : "Type an app name",
                       1U,launcher_search[0]?245U:145U,
                       launcher_search[0]?249U:169U,
                       launcher_search[0]?255U:190U);
    graphics_draw_text(menu_x+20U,menu_y+104U,"APPLICATIONS",1U,
                       141U,177U,204U);

    if(launcher_visible_count==0U) {
        graphics_draw_text(menu_x+22U,menu_y+150U,"NO MATCHING APPS",
                           1U,239U,177U,119U);
    } else if(menu_width>400U) {
        const uint64_t card_width=(menu_width-54U)/2U;
        const uint64_t row_top=menu_y+113U;
        for(size_t i=0U;i<launcher_visible_count;++i) {
            const size_t app=launcher_visible_apps[i];
            const uint64_t col=i%2U,row=i/2U;
            const uint64_t x=menu_x+18U+col*(card_width+12U);
            const uint64_t y=row_top+row*55U;
            const bool selected=i==launcher_selected_index;
            graphics_fill_round_rect(x,y,card_width,48U,10U,
                selected?42U:24U,selected?76U:39U,selected?99U:58U);
            graphics_draw_round_rect(x,y,card_width,48U,10U,
                selected?111U:66U,selected?176U:91U,selected?201U:119U);
            desktop_draw_app_icon(x+10U,y+11U,25U,app);
            graphics_draw_text(x+45U,y+28U,launcher_app_names[app],1U,
                               235U,243U,250U);
        }
    } else {
        const uint64_t row_top=menu_y+112U;
        for(size_t i=0U;i<launcher_visible_count;++i) {
            const size_t app=launcher_visible_apps[i];
            const uint64_t y=row_top+(uint64_t)i*32U;
            const bool selected=i==launcher_selected_index;
            graphics_fill_round_rect(menu_x+14U,y,menu_width-28U,29U,
                8U,selected?42U:24U,selected?76U:39U,selected?99U:58U);
            desktop_draw_app_icon(menu_x+20U,y+5U,19U,app);
            graphics_draw_text(menu_x+49U,y+17U,launcher_app_names[app],1U,
                               235U,243U,250U);
        }
    }
    graphics_draw_text(menu_x+20U,menu_y+menu_height-13U,
                       "TYPE TO SEARCH   UP/DOWN MOVE   ENTER OPEN   ESC CLOSE",
                       1U,132U,165U,193U);
}

static uint64_t desktop_workspace_tab_x(uint8_t workspace) {
    return 180U + (uint64_t)workspace * 34U;
}

static uint64_t desktop_workspace_overview_x(void) {
    return 326U;
}

static void desktop_draw_workspace_switcher(uint64_t top_bar) {
    if (screen_width < 500U) return;
    graphics_draw_text(122U, top_bar / 2U - 4U, "DESKS",
                       1U, 139U, 184U, 210U);
    const uint8_t active = nexus_wm_active_workspace();
    for (uint8_t workspace = 0U;
         workspace < NEXUS_WM_MAX_WORKSPACES; ++workspace) {
        const uint64_t x = desktop_workspace_tab_x(workspace);
        const bool selected = workspace == active;
        graphics_fill_round_rect(x, 8U, 28U, top_bar - 16U, 7U,
            selected ? 48U : 24U, selected ? 91U : 43U,
            selected ? 119U : 62U);
        if (selected)
            graphics_draw_round_rect(x, 8U, 28U, top_bar - 16U, 7U,
                                     94U, 169U, 197U);
        char number[2] = {(char)('1' + workspace), '\0'};
        graphics_draw_text(x + 10U, top_bar / 2U - 4U, number, 1U,
                           selected ? 255U : 174U,
                           selected ? 255U : 197U,
                           selected ? 255U : 216U);
    }
    const uint64_t overview_x = desktop_workspace_overview_x();
    graphics_fill_round_rect(overview_x, 8U, 82U, top_bar - 16U, 7U,
                             workspace_overview_open ? 47U : 24U,
                             workspace_overview_open ? 78U : 43U,
                             workspace_overview_open ? 99U : 62U);
    graphics_draw_text(overview_x + 8U, top_bar / 2U - 4U, "TASK VIEW",
                       1U, 226U, 239U, 249U);
}

static void desktop_workspace_overview_geometry(uint64_t *x, uint64_t *y,
                                                uint64_t *width,
                                                uint64_t *height) {
    uint64_t panel_width = screen_width > 940U ? 900U :
        (screen_width > 32U ? screen_width - 32U : screen_width);
    uint64_t panel_height = screen_height > 520U ? 430U :
        (screen_height > 32U ? screen_height - 32U : screen_height);
    if (panel_width < 220U) panel_width = screen_width;
    if (panel_height < 140U) panel_height = screen_height;
    if (x != NULL) *x = (screen_width - panel_width) / 2U;
    if (y != NULL) *y = (screen_height - panel_height) / 2U;
    if (width != NULL) *width = panel_width;
    if (height != NULL) *height = panel_height;
}

static void desktop_draw_workspace_overview(void) {
    if (!workspace_overview_open || installer_environment ||
        !desktop_window_manager_ready || screen_width < 320U ||
        screen_height < 220U) return;
    uint64_t panel_x = 0U, panel_y = 0U, panel_width = 0U, panel_height = 0U;
    desktop_workspace_overview_geometry(&panel_x, &panel_y,
                                        &panel_width, &panel_height);
    graphics_fill_rect(0U, 0U, screen_width, screen_height,
                       5U, 10U, 22U);
    graphics_fill_round_rect(panel_x + 8U, panel_y + 12U,
                             panel_width, panel_height, 22U,
                             1U, 5U, 16U);
    graphics_fill_gradient_rect(panel_x, panel_y, panel_width, panel_height,
                                24U, 38U, 59U, 13U, 22U, 38U);
    graphics_draw_round_rect(panel_x, panel_y, panel_width, panel_height,
                              22U, 77U, 111U, 143U);
    graphics_fill_gradient_rect(panel_x + 1U, panel_y + 1U,
                                panel_width - 2U, 3U,
                                70U, 160U, 211U, 135U, 111U, 226U);
    graphics_draw_text(panel_x + 24U, panel_y + 30U,
                       "TASK VIEW", 2U, 245U, 249U, 255U);
    graphics_draw_text(panel_x + 24U, panel_y + 52U,
                       "FOUR DESKTOPS  /  KEEP PROJECTS AND WINDOWS SEPARATE",
                       1U, 145U, 184U, 210U);
    const uint64_t gap = 8U;
    const uint64_t inner_x = panel_x + 18U;
    const uint64_t inner_y = panel_y + 70U;
    const uint64_t inner_width = panel_width - 36U;
    const uint64_t card_width = (inner_width -
        (NEXUS_WM_MAX_WORKSPACES - 1U) * gap) /
        NEXUS_WM_MAX_WORKSPACES;
    const uint64_t card_height = panel_height - 116U;
    const uint8_t active = nexus_wm_active_workspace();
    for (uint8_t workspace = 0U;
         workspace < NEXUS_WM_MAX_WORKSPACES; ++workspace) {
        const uint64_t x = inner_x + (uint64_t)workspace *
                           (card_width + gap);
        const uint64_t y = inner_y;
        const bool selected = workspace == workspace_overview_selected;
        const bool current = workspace == active;
        graphics_fill_round_rect(x, y, card_width, card_height, 12U,
            selected ? 39U : 20U, selected ? 73U : 37U,
            selected ? 95U : 56U);
        graphics_draw_round_rect(x, y, card_width, card_height, 12U,
            selected ? 117U : 68U, selected ? 176U : 104U,
            selected ? 201U : 135U);
        char heading[16] = "DESKTOP 1";
        heading[8] = (char)('1' + workspace);
        if (card_width > 100U) {
            graphics_draw_text(x + 10U, y + 22U, heading, 1U,
                               241U, 247U, 253U);
        } else {
            char short_heading[3] = {'D', (char)('1' + workspace), '\0'};
            graphics_draw_text(x + 10U, y + 22U, short_heading, 1U,
                               241U, 247U, 253U);
        }
        graphics_draw_text(x + 10U, y + 42U,
                           current ? "CURRENT" : "SWITCH",
                           1U, current ? 112U : 151U,
                           current ? 222U : 185U,
                           current ? 190U : 209U);
        char count_text[24] = "WINDOWS ";
        char count_number[21];
        format_u64(nexus_wm_workspace_count(workspace), count_number);
        size_t count_pos = 8U;
        for (size_t n = 0U; count_number[n] != '\0' &&
             count_pos < sizeof(count_text) - 1U; ++n)
            count_text[count_pos++] = count_number[n];
        count_text[count_pos] = '\0';
        if (card_width > 100U)
            graphics_draw_text(x + 10U, y + 62U, count_text, 1U,
                               139U, 184U, 210U);
        const size_t windows = nexus_wm_workspace_count(workspace);
        if (windows == 0U) {
            graphics_fill_round_rect(x + 9U, y + 82U,
                card_width - 18U, card_height > 112U ? 46U : 20U,
                7U, 14U, 28U, 44U);
            if (card_width > 100U)
                graphics_draw_text(x + 17U, y + 98U, "EMPTY DESKTOP",
                                   1U, 130U, 164U, 188U);
        } else {
            const size_t rows = windows < 3U ? windows : 3U;
            for (size_t row = 0U; row < rows; ++row) {
                nexus_window_id id = 0U;
                struct nexus_window_info info;
                if (!nexus_wm_workspace_z_order(workspace, row, &id) ||
                    !nexus_wm_get(id, &info)) continue;
                const uint64_t row_y = y + 82U + (uint64_t)row * 39U;
                if (row_y + 31U > y + card_height - 8U) break;
                graphics_fill_round_rect(x + 9U, row_y,
                    card_width - 18U, 32U, 6U, 11U, 24U, 39U);
                graphics_draw_rect(x + 13U, row_y + 6U, 12U, 2U,
                                   80U, 162U, 190U);
                if (card_width > 140U)
                    graphics_draw_text(x + 30U, row_y + 19U,
                        info.title, 1U, 223U, 235U, 244U);
            }
        }
    }
    graphics_draw_text(panel_x + 24U, panel_y + panel_height - 16U,
        "CLICK A DESKTOP TO SWITCH  /  ESC TO CLOSE",
        1U, 143U, 176U, 201U);
}

static void desktop_workspace_overview_click(void) {
    uint64_t panel_x = 0U, panel_y = 0U, panel_width = 0U, panel_height = 0U;
    desktop_workspace_overview_geometry(&panel_x, &panel_y,
                                        &panel_width, &panel_height);
    const uint64_t inner_x = panel_x + 18U;
    const uint64_t inner_y = panel_y + 70U;
    const uint64_t gap = 8U;
    const uint64_t card_width =
        (panel_width - 36U - (NEXUS_WM_MAX_WORKSPACES - 1U) * gap) /
        NEXUS_WM_MAX_WORKSPACES;
    if (pointer_x >= (int64_t)inner_x &&
        pointer_x < (int64_t)(inner_x + panel_width - 36U) &&
        pointer_y >= (int64_t)inner_y &&
        pointer_y < (int64_t)(panel_y + panel_height - 42U)) {
        const uint64_t relative = (uint64_t)pointer_x - inner_x;
        const uint64_t stride = card_width + gap;
        const uint64_t workspace = relative / stride;
        const uint64_t local_x = relative % stride;
        if (workspace < NEXUS_WM_MAX_WORKSPACES && local_x < card_width) {
            desktop_switch_workspace((uint8_t)workspace);
            return;
        }
    }
    workspace_overview_open = false;
}

static void desktop_draw_startup_animation(void) {
    /* A short, timer-paced splash gives the user visible feedback while the
     * installed desktop is being brought up. Keep it framebuffer-only so it
     * works on both BIOS and UEFI without a graphics driver.
     *
     * The centered layout uses fixed offsets from the midpoint. On unusually
     * small framebuffers those offsets could underflow unsigned coordinates
     * and wrap drawing calls to the far edge of the display. Skip the optional
     * splash rather than risk corrupt geometry; the regular shell still draws. */
    if (screen_width < 176U || screen_height < 188U) {
        serial_write("DESKTOP: startup splash skipped for small framebuffer\r\n");
        return;
    }
    const uint64_t center_x = screen_width / 2U;
    const uint64_t center_y = screen_height / 2U;
    const uint64_t panel_width = screen_width > 520U ? 440U : screen_width - 32U;
    const uint64_t panel_x = (screen_width - panel_width) / 2U;
    const uint64_t title_y = center_y > 70U ? center_y - 70U : 12U;
    const uint64_t bar_y = center_y + 52U;
    const uint64_t bar_width = panel_width > 64U ? panel_width - 64U : 32U;

    graphics_fill_rect(0U, 0U, screen_width, screen_height, 5U, 11U, 25U);
    graphics_fill_rect(panel_x, title_y - 24U, panel_width, 176U,
                       12U, 25U, 45U);
    graphics_draw_rect(panel_x, title_y - 24U, panel_width, 176U,
                       39U, 111U, 143U);
    desktop_draw_logo(center_x - 32U, title_y - 22U, 64U);
    graphics_draw_text(center_x - 64U, title_y + 48U,
                       "NEXUS", 2U, 239U, 247U, 255U);
    graphics_draw_text(center_x - 88U, title_y + 78U,
                       "Starting your workspace", 1U,
                       165U, 190U, 211U);

    for (uint64_t frame = 0U; frame < 20U; ++frame) {
        for (uint64_t dot = 0U; dot < 5U; ++dot) {
            const uint64_t x = center_x - 30U + dot * 15U;
            const bool active = dot == frame % 5U || dot == (frame + 4U) % 5U;
            const uint8_t level = active ? 225U : 75U;
            graphics_fill_rect(x, center_y + 20U, 8U, 8U,
                               45U, level, active ? 220U : 135U);
        }
        graphics_fill_rect(panel_x + 32U, bar_y, bar_width, 5U,
                           34U, 52U, 72U);
        const uint64_t progress = (bar_width * (frame + 1U)) / 20U;
        graphics_fill_rect(panel_x + 32U, bar_y, progress, 5U,
                           47U, 177U, 202U);

        const uint64_t deadline = timer_ticks() + 3U;
        __asm__ volatile ("sti" : : : "memory");
        while (timer_ticks() < deadline)
            __asm__ volatile ("hlt" : : : "memory");
        __asm__ volatile ("cli" : : : "memory");
    }
}

static void calculator_clear(void) {
    calculator_expression[0] = '\0';
    calculator_result[0] = '0';
    calculator_result[1] = '\0';
    calculator_error = NEXUS_CALCULATOR_OK;
    calculator_result_valid = false;
}
static void calculator_format(int64_t value,char output[32]) {
    char reverse[20]; size_t n=0U;
    uint64_t magnitude=value<0 ? (uint64_t)(-(value+1))+1U : (uint64_t)value;
    do { reverse[n++]=(char)('0'+magnitude%10U); magnitude/=10U; }
    while(magnitude!=0U && n<sizeof(reverse));
    size_t at=0U; if(value<0) output[at++]='-';
    while(n!=0U && at<30U) output[at++]=reverse[--n];
    output[at]='\0';
}
static bool calculator_operator(char ch) {
    return ch=='+' || ch=='-' || ch=='*' || ch=='/' || ch=='%';
}
static void calculator_append(char ch) {
    bool starts_new=(ch>='0' && ch<='9') || ch=='(';
    if(calculator_result_valid && starts_new) {
        calculator_expression[0]='\0'; calculator_result_valid=false;
    }
    size_t n=0U;
    while(n<NEXUS_CALCULATOR_EXPRESSION_MAX && calculator_expression[n]!='\0') ++n;
    bool allowed=(ch>='0' && ch<='9') || calculator_operator(ch) ||
                 ch=='(' || ch==')';
    if(!allowed || n>=NEXUS_CALCULATOR_EXPRESSION_MAX) return;
    calculator_expression[n]=ch; calculator_expression[n+1U]='\0';
    calculator_error=NEXUS_CALCULATOR_OK; calculator_result_valid=false;
}
static void calculator_backspace(void) {
    size_t n=0U;
    while(n<NEXUS_CALCULATOR_EXPRESSION_MAX && calculator_expression[n]!='\0') ++n;
    if(n!=0U) calculator_expression[n-1U]='\0';
    calculator_error=NEXUS_CALCULATOR_OK; calculator_result_valid=false;
}
static void calculator_evaluate(void) {
    int64_t value=0;
    calculator_error=nexus_calculator_evaluate(calculator_expression,&value);
    if(calculator_error!=NEXUS_CALCULATOR_OK) {
        calculator_result_valid=false; return;
    }
    calculator_format(value,calculator_result);
    size_t n=0U;
    while(calculator_result[n]!='\0' && n<sizeof(calculator_expression)-1U) {
        calculator_expression[n]=calculator_result[n]; ++n;
    }
    calculator_expression[n]='\0'; calculator_result_valid=true;
}
static void calculator_button(size_t row,size_t col) {
    static const char keys[5][4]={
        {'C','(',')','/'},{'7','8','9','*'},{'4','5','6','-'},
        {'1','2','3','+'},{'0','%','<','='}
    };
    if(row>=5U || col>=4U) return;
    char key=keys[row][col];
    if(key=='C') calculator_clear();
    else if(key=='<') calculator_backspace();
    else if(key=='=') calculator_evaluate();
    else calculator_append(key);
}


static void desktop_draw_desktop_icons(uint64_t top_bar,
                                       uint64_t bottom_bar) {
    static const char *const labels[7] = {
        "FILES", "BROWSER", "SETTINGS", "TERMINAL",
        "MONITOR", "CONTROL", "CALCULATOR"
    };
    static const uint8_t label_widths[7] = {
        40U, 56U, 56U, 56U, 56U, 56U, 72U
    };
    const uint64_t usable_bottom = screen_height > bottom_bar
        ? screen_height - bottom_bar : 0U;
    graphics_draw_text(22U, top_bar + 10U, "NEXUS DESKTOP", 1U,
                       224U, 237U, 250U);

    /* Familiar desktop shortcuts down the left edge. Running apps get a
     * subtle indicator, while hover highlights make targets easy to find. */
    for (size_t app = 0U; app < 7U; ++app) {
        const uint64_t col = app / 6U;
        const uint64_t row = app % 6U;
        const uint64_t x = 18U + col * 92U;
        const uint64_t y = top_bar + 28U + row * 76U;
        if (y + 70U > usable_bottom) continue;
        const bool hovered = pointer_x >= (int64_t)x &&
            pointer_x < (int64_t)(x + 76U) &&
            pointer_y >= (int64_t)y &&
            pointer_y < (int64_t)(y + 70U);
        graphics_fill_round_rect(x, y, 76U, 70U, 10U,
                                 hovered ? 42U : 19U,
                                 hovered ? 66U : 34U,
                                 hovered ? 87U : 51U);
        if (hovered)
            graphics_draw_round_rect(x, y, 76U, 70U, 10U,
                                     112U, 164U, 202U);
        desktop_draw_app_icon(x + 23U, y + 7U, 29U, app);
        graphics_draw_text(x + (76U - label_widths[app]) / 2U,
                           y + 57U, labels[app], 1U,
                           238U, 245U, 252U);
        struct nexus_window_info info;
        if (desktop_app_window_info(app, &info))
            graphics_fill_round_rect(x + 34U, y + 66U, 8U, 2U, 1U,
                                     102U, 202U, 229U);
    }

    /* Desktop widget: live, useful system facts instead of a large empty
     * landing panel. It stays within the work area on smaller resolutions. */
    if (screen_width > 760U && usable_bottom > top_bar + 198U) {
        const uint64_t x = screen_width - 286U;
        const uint64_t y = top_bar + 24U;
        graphics_fill_round_rect(x + 5U, y + 7U, 268U, 166U, 16U,
                                 1U, 5U, 15U);
        graphics_fill_gradient_rect(x, y, 268U, 166U,
                                    32U, 48U, 67U, 15U, 26U, 43U);
        graphics_draw_round_rect(x, y, 268U, 166U, 16U,
                                 78U, 105U, 137U);
        graphics_draw_text(x + 16U, y + 22U, "SYSTEM OVERVIEW", 1U,
                           241U, 247U, 253U);
        char memory[21], uptime[21], disks[21], adapters[21];
        format_u64(memory_free_bytes() / (1024U * 1024U), memory);
        format_u64(timer_ticks() / 100U, uptime);
        format_u64(blockdev_count(), disks);
        format_u64(netdev_device_count(), adapters);
        const uint64_t x1 = x + 14U;
        const uint64_t x2 = x + 140U;
        const uint64_t y1 = y + 42U;
        const uint64_t y2 = y + 97U;
        const uint64_t card_w = 114U;
        const uint64_t card_h = 45U;
        const uint8_t card_r = settings_high_contrast ? 3U : 20U;
        const uint8_t card_g = settings_high_contrast ? 3U : 38U;
        const uint8_t card_b = settings_high_contrast ? 3U : 56U;
        graphics_fill_round_rect(x1, y1, card_w, card_h, 8U,
                                 card_r, card_g, card_b);
        graphics_fill_round_rect(x2, y1, card_w, card_h, 8U,
                                 card_r, card_g, card_b);
        graphics_fill_round_rect(x1, y2, card_w, card_h, 8U,
                                 card_r, card_g, card_b);
        graphics_fill_round_rect(x2, y2, card_w, card_h, 8U,
                                 card_r, card_g, card_b);
        graphics_draw_text(x1 + 9U, y1 + 12U, "FREE MiB", 1U,
                           133U, 188U, 211U);
        graphics_draw_text(x1 + 9U, y1 + 30U, memory, 1U,
                           246U, 249U, 253U);
        graphics_draw_text(x2 + 9U, y1 + 12U, "UPTIME s", 1U,
                           133U, 188U, 211U);
        graphics_draw_text(x2 + 9U, y1 + 30U, uptime, 1U,
                           246U, 249U, 253U);
        graphics_draw_text(x1 + 9U, y2 + 12U, "DISKS", 1U,
                           133U, 188U, 211U);
        graphics_draw_text(x1 + 9U, y2 + 30U, disks, 1U,
                           246U, 249U, 253U);
        graphics_draw_text(x2 + 9U, y2 + 12U, "ADAPTERS", 1U,
                           133U, 188U, 211U);
        graphics_draw_text(x2 + 9U, y2 + 30U, adapters, 1U,
                           246U, 249U, 253U);
    }
}

static void desktop_snap_window(nexus_window_id id,
                                enum desktop_snap_zone zone) {
    struct nexus_window_info info;
    if (id == 0U || !desktop_window_manager_ready ||
        !nexus_wm_get(id, &info)) return;
    if (zone == DESKTOP_SNAP_MAXIMIZE) {
        (void)nexus_wm_maximize(id);
        desktop_sync_focused_window();
        return;
    }
    if (zone == DESKTOP_SNAP_RESTORE) {
        (void)nexus_wm_unmaximize(id);
        desktop_sync_focused_window();
        return;
    }
    if ((info.flags & NEXUS_WINDOW_MAXIMIZED) != 0U)
        (void)nexus_wm_unmaximize(id);

    uint32_t work_height = 0U;
    desktop_window_work_area(NULL, NULL, &work_height);
    const uint32_t display_width = screen_width > 0xFFFFFFFFU
        ? 0xFFFFFFFFU : (uint32_t)screen_width;
    if (display_width < 320U || work_height < 192U) return;
    const uint32_t half_width = display_width / 2U;
    const uint32_t half_height = work_height / 2U;
    uint32_t x = 0U, y = 0U;
    uint32_t width = half_width, height = work_height;
    switch (zone) {
        case DESKTOP_SNAP_LEFT:
            break;
        case DESKTOP_SNAP_RIGHT:
            x = half_width;
            width = display_width - half_width;
            break;
        case DESKTOP_SNAP_TOP_LEFT:
            height = half_height;
            break;
        case DESKTOP_SNAP_TOP_RIGHT:
            x = half_width;
            width = display_width - half_width;
            height = half_height;
            break;
        case DESKTOP_SNAP_BOTTOM_LEFT:
            y = half_height;
            height = work_height - half_height;
            break;
        case DESKTOP_SNAP_BOTTOM_RIGHT:
            x = half_width;
            y = half_height;
            width = display_width - half_width;
            height = work_height - half_height;
            break;
        case DESKTOP_SNAP_MAXIMIZE:
        case DESKTOP_SNAP_RESTORE:
        default:
            return;
    }
    if (nexus_wm_resize(id, width, height))
        (void)nexus_wm_move(id, (int32_t)x, (int32_t)y);
    desktop_sync_focused_window();
}

static void desktop_snap_window_at_pointer(nexus_window_id id) {
    uint64_t top = 0U;
    uint32_t work_height = 0U;
    desktop_window_work_area(&top, NULL, &work_height);
    const uint64_t edge = clamp_min(screen_width / 100U, 12U);
    if (pointer_y <= (int64_t)(top + 8U)) {
        desktop_snap_window(id, DESKTOP_SNAP_MAXIMIZE);
    } else if (pointer_x <= (int64_t)edge) {
        const uint64_t middle = top + work_height / 2U;
        desktop_snap_window(id, pointer_y < (int64_t)middle
            ? DESKTOP_SNAP_TOP_LEFT : DESKTOP_SNAP_BOTTOM_LEFT);
    } else if (pointer_x >= (int64_t)(screen_width - edge)) {
        const uint64_t middle = top + work_height / 2U;
        desktop_snap_window(id, pointer_y < (int64_t)middle
            ? DESKTOP_SNAP_TOP_RIGHT : DESKTOP_SNAP_BOTTOM_RIGHT);
    }
}

static void desktop_draw_window_surface(enum desktop_view view,
                                        uint64_t window_x,
                                        uint64_t window_y,
                                        uint64_t window_width,
                                        uint64_t window_height,
                                        bool maximized,
                                        bool animate) {
    const enum desktop_view saved_view = active_view;
    const bool saved_maximized = window_maximized;
    active_view = view;
    window_maximized = maximized;
    const uint64_t panel_x = window_x;
    const uint64_t panel_y = window_y +
        (animate ? window_animation_offset : 0U);
    const uint64_t panel_width = window_width;
    const uint64_t panel_height = window_height;
    graphics_fill_round_rect(panel_x + 7U, panel_y + 10U, panel_width,
                              panel_height, 17U, 1U, 5U, 17U);
    graphics_fill_gradient_rect(panel_x,panel_y,panel_width,panel_height,
                                24U,36U,54U,13U,22U,37U);
    graphics_draw_round_rect(panel_x,panel_y,panel_width,panel_height,
                             20U,75U,98U,124U);
    /* Shared dark chrome with a restrained aurora accent line. */
    graphics_fill_round_rect(panel_x+1U,panel_y+1U,panel_width-2U,43U,
                             18U,26U,42U,62U);
    graphics_fill_gradient_rect(panel_x+1U,panel_y+16U,panel_width-2U,27U,
                                26U,42U,62U,19U,32U,49U);
    graphics_fill_gradient_rect(panel_x+1U,panel_y+41U,panel_width-2U,2U,
                                69U,170U,207U,139U,111U,225U);

    const char *window_title = "NEXUS HOME";
    if (active_view == DESKTOP_FILES) window_title = "FILES";
    if (active_view == DESKTOP_BROWSER) window_title = "NEXUS BROWSER";
    if (active_view == DESKTOP_SETTINGS) window_title = "SETTINGS";
    if (active_view == DESKTOP_TERMINAL) window_title = "NEXUS TERMINAL";
    if (active_view == DESKTOP_MONITOR) window_title = "SYSTEM MONITOR";
    if (active_view == DESKTOP_CONTROL_CENTER) window_title = "NEXUS CONTROL CENTER";
    if (active_view == DESKTOP_CALCULATOR) window_title = "NEXUS CALCULATOR";
    if (active_view == DESKTOP_INSTALLER) window_title = "NEXUS INSTALLER";
    desktop_draw_logo(panel_x + 7U, panel_y + 8U, 22U);
    graphics_draw_text_smooth(panel_x + 33U, panel_y + 13U, window_title,
                              2U, 242U, 246U, 255U);
    /* Custom window controls; close is wired to the shell input handler. */
    const uint64_t controls_x = panel_x + panel_width - 66U;
    graphics_fill_round_rect(controls_x, panel_y + 12U, 14U, 12U,
                             4U, 47U, 118U, 181U);
    graphics_draw_text(controls_x + 4U, panel_y + 15U, "-", 1U,
                       229U, 239U, 248U);
    graphics_fill_round_rect(controls_x + 22U, panel_y + 12U, 14U, 12U,
                             4U, 47U, 118U, 181U);
    graphics_draw_rect(controls_x + 26U, panel_y + 15U, 6U, 6U,
                       207U, 224U, 239U);
    graphics_fill_round_rect(controls_x + 44U, panel_y + 12U, 14U, 12U,
                             4U, 218U, 82U, 98U);
    graphics_draw_text(controls_x + 47U, panel_y + 16U, "X", 1U,
                       255U, 239U, 241U);

    const uint64_t text_x = panel_x + 32U;
    const uint64_t text_y = panel_y +
        (active_view == DESKTOP_INSTALLER ? 48U : 82U);
    if (active_view == DESKTOP_HOME) {
        graphics_draw_text_smooth(text_x, text_y, "WELCOME TO YOUR WORKSPACE", 2U, 245U, 249U, 255U);
        graphics_draw_text(text_x + 2U, text_y + 29U,
                           "YOUR TOOLS, FILES AND SYSTEM STATUS IN ONE PLACE",
                           1U, 139U, 190U, 218U);
        graphics_fill_round_rect(text_x, text_y + 43U, 146U, 18U, 8U,
                                 22U, 64U, 76U);
        graphics_draw_text(text_x + 9U, text_y + 48U,
                           "NEXUS WORKSPACE", 1U, 130U, 232U, 218U);

        /* Responsive four-column app deck with large, consistent targets. */
        const uint64_t tile_y = text_y + 68U;
        const uint64_t tile_gap = 8U;
        const uint64_t tile_width = (panel_width - 56U - 3U * tile_gap) / 4U;
        static const char *const labels[7] = {
            "FILES", "WEB", "SETTINGS", "TERMINAL",
            "MONITOR", "CONTROL", "CALCULATOR"
        };
        static const uint8_t label_widths[7] = {
            40U, 24U, 64U, 64U, 56U, 56U, 72U
        };
        static const uint8_t accent[7][3] = {
            {75U, 166U, 235U}, {56U, 194U, 210U},
            {150U, 129U, 238U}, {232U, 157U, 99U},
            {77U, 202U, 165U}, {165U, 134U, 232U},
            {95U, 174U, 232U}
        };
        for (size_t app = 0U; app < 7U; ++app) {
            const uint64_t col = app % 4U;
            const uint64_t row = app / 4U;
            const uint64_t x = panel_x + 28U + col * (tile_width + tile_gap);
            const uint64_t y = tile_y + row * 56U;
            graphics_fill_round_rect(x, y, tile_width, 50U, 11U,
                                     22U, 40U, 62U);
            graphics_draw_round_rect(x, y, tile_width, 50U, 11U,
                                     68U, 104U, 140U);
            graphics_fill_round_rect(x + 1U, y + 8U, 3U, 34U, 1U,
                                     accent[app][0], accent[app][1],
                                     accent[app][2]);
            desktop_draw_app_icon(x + 10U, y + 12U, 25U, app);
            const uint64_t label_x = x + (tile_width > label_widths[app]
                ? (tile_width - label_widths[app]) / 2U : 5U);
            graphics_draw_text(label_x, y + 43U, labels[app],
                               1U, 239U, 246U, 253U);
        }

        graphics_draw_text(text_x, text_y + 184U, "LIVE SYSTEM SNAPSHOT",
                           1U, 139U, 190U, 218U);
        const uint64_t card_gap = 8U;
        const uint64_t card_width = (panel_width - 92U) / 4U;
        const uint64_t card_y = text_y + 201U;
        const uint64_t card_h = 48U;
        const char *card_labels[4] = {
            "MEM FREE", "UPTIME", "STORAGE", "NETWORK"
        };
        char free_mem[21], uptime[21];
        format_u64(memory_free_bytes() / (1024U * 1024U), free_mem);
        format_u64(timer_ticks() / 100U, uptime);
        char memory_value[40] = "";
        size_t memory_pos = 0U;
        while (free_mem[memory_pos] != '\0' &&
               memory_pos < sizeof(memory_value) - 1U) {
            memory_value[memory_pos] = free_mem[memory_pos];
            ++memory_pos;
        }
        const char *memory_suffix = " MiB";
        for (size_t n = 0U; memory_suffix[n] != '\0' &&
             memory_pos < sizeof(memory_value) - 1U; ++n)
            memory_value[memory_pos++] = memory_suffix[n];
        memory_value[memory_pos] = '\0';
        char uptime_value[40] = "";
        size_t uptime_pos = 0U;
        while (uptime[uptime_pos] != '\0' &&
               uptime_pos < sizeof(uptime_value) - 2U) {
            uptime_value[uptime_pos] = uptime[uptime_pos];
            ++uptime_pos;
        }
        uptime_value[uptime_pos++] = 's';
        uptime_value[uptime_pos] = '\0';
        const char *storage_label =
            vfs_nxfs_mounted() && vfs_nxfs_writable() ? "NXFS WRITABLE" :
            (vfs_root_mounted() ? "MOUNTED" : "VOLATILE RAM");
        const uint32_t adapters = netdev_device_count();
        const char *adapter = adapters == 0U ? "NO ADAPTER" :
            netdev_device_name_at(netdev_default_index());
        if (adapter == NULL) adapter = "UNKNOWN";
        const char *card_values[4] = {
            memory_value, uptime_value, storage_label, adapter
        };
        static const uint8_t card_accent[4][3] = {
            {65U, 190U, 210U}, {132U, 125U, 229U},
            {73U, 191U, 159U}, {219U, 160U, 102U}
        };
        for (size_t i = 0U; i < 4U; ++i) {
            const uint64_t x = panel_x + 28U + (uint64_t)i *
                               (card_width + card_gap);
            graphics_fill_round_rect(x, card_y, card_width, card_h,
                                     9U, 20U, 37U, 57U);
            graphics_draw_round_rect(x, card_y, card_width, card_h,
                                     9U, 68U, 105U, 139U);
            graphics_fill_round_rect(x + 1U, card_y + 8U, 3U,
                                     card_h - 16U, 1U,
                                     card_accent[i][0], card_accent[i][1],
                                     card_accent[i][2]);
            graphics_draw_text(x + 10U, card_y + 14U, card_labels[i],
                               1U, 147U, 183U, 205U);
            graphics_draw_text(x + 10U, card_y + 35U, card_values[i],
                               1U, 242U, 247U, 252U);
        }
        graphics_draw_text(text_x, panel_y + panel_height - 16U,
                           "F1-F7 OPEN APPS   CTRL+SPACE SEARCH   ALT+TAB SWITCH",
                           1U, 151U, 190U, 213U);
    } else if (active_view == DESKTOP_SETTINGS) {
        graphics_draw_text_smooth(text_x, text_y, "MAKE NEXUS YOURS", 2U, 241U, 248U, 255U);
        graphics_draw_text(text_x, text_y + 30U,
                           "PERSONALIZATION AND DESKTOP BEHAVIOR", 1U, 111U, 207U, 231U);
        const uint64_t row_x = panel_x + 24U;
        const uint64_t row_w = panel_width - 48U;
        const uint64_t row_y = text_y + 54U;
        static const char *const theme_names[5] = {
            "MIDNIGHT", "OCEAN", "EMBER", "VIOLET", "FOREST"
        };
        static const char *const wallpaper_names[5] = {
            "AURORA", "GRADIENT", "SOLID", "GRID", "ORBIT"
        };
        const char *theme_name = theme_names[settings_theme < 5U
            ? settings_theme : 0U];
        const char *wallpaper_name = wallpaper_names[settings_wallpaper < 5U
            ? settings_wallpaper : 0U];
        const char *status_name = settings_show_status ? "ON" : "OFF";
        const char *window_name = window_maximized ? "MAXIMIZED" : "WINDOWED";
        const uint32_t network_count = netdev_device_count();
        const uint32_t network_index = netdev_default_index();
        const char *network_name = network_count == 0U ? "NO ADAPTER" :
            netdev_device_name_at(network_index);
        if (network_name == NULL) network_name = "UNKNOWN";
        const char *contrast_name = settings_high_contrast ? "ON" : "OFF";
        const char *motion_name = settings_reduce_motion ? "REDUCED" : "STANDARD";
        const char *labels[7] = {"COLOR THEME", "WALLPAPER", "TASKBAR STATUS",
                                 "WINDOW SIZE", "NETWORK ADAPTER",
                                 "HIGH CONTRAST", "MOTION EFFECTS"};
        const char *values[7] = {theme_name, wallpaper_name, status_name,
                                 window_name, network_name, contrast_name, motion_name};
        const char *hints[7] = {"1  CYCLE FIVE COLOR PALETTES", "2  CYCLE FIVE WALLPAPERS",
                                "3  SHOW OR HIDE UPTIME", "4  TOGGLE WINDOW SIZE",
                                "5  SELECT NEXT INSTALLED ADAPTER",
                                "6  INCREASE TEXT AND CONTROL CONTRAST",
                                "7  REDUCE APP TRANSITION ANIMATION"};
        graphics_draw_text(row_x, row_y - 14U, "DISPLAY AND ACCESSIBILITY",
                           1U, 111U, 207U, 231U);
        for (size_t i = 0U; i < 7U; ++i) {
            const uint64_t y = row_y + (uint64_t)i * 42U;
            const uint8_t bg = settings_high_contrast ? 9U : 25U;
            graphics_fill_round_rect(row_x, y, row_w, 36U, 8U, bg, bg + 18U, bg + 23U);
            graphics_draw_round_rect(row_x, y, row_w, 36U, 8U,
                                     settings_high_contrast ? 220U : 88U,
                                     settings_high_contrast ? 235U : 123U,
                                     settings_high_contrast ? 255U : 157U);
            graphics_fill_round_rect(row_x + 1U, y + 6U, 3U, 24U, 1U,
                                i == 0U ? 45U : (i == 1U ? 117U : (i == 2U ? 82U : 217U)),
                                i == 0U ? 177U : (i == 1U ? 136U : (i == 2U ? 198U : 164U)),
                                i == 0U ? 202U : (i == 1U ? 244U : (i == 2U ? 155U : 83U)));
            graphics_draw_text(row_x + 15U, y + 6U, labels[i], 1U,
                               settings_high_contrast ? 255U : 180U,
                               settings_high_contrast ? 255U : 205U, 255U);
            graphics_draw_text(row_x + row_w / 2U, y + 6U, values[i], 1U, 255U, 255U, 255U);
            graphics_draw_text(row_x + 15U, y + 23U, hints[i], 1U,
                               settings_high_contrast ? 230U : 135U,
                               settings_high_contrast ? 230U : 175U,
                               settings_high_contrast ? 255U : 205U);
        }
        char display_mode[64] = "FRAMEBUFFER ";
        char display_width[21], display_height[21];
        format_u64(screen_width, display_width);
        format_u64(screen_height, display_height);
        size_t display_pos = 11U;
        for (size_t n = 0U; display_width[n] != '\0' && display_pos < 58U; ++n)
            display_mode[display_pos++] = display_width[n];
        display_mode[display_pos++] = 'X';
        for (size_t n = 0U; display_height[n] != '\0' && display_pos < 62U; ++n)
            display_mode[display_pos++] = display_height[n];
        display_mode[display_pos] = '\0';
        graphics_draw_text(row_x, row_y + 7U * 42U + 2U, display_mode,
                           1U, 111U, 207U, 231U);
        graphics_draw_text(text_x, panel_y + panel_height - 34U,
                           settings_persisted ? "PREFERENCES SAVED ON THIS DEVICE" : "SESSION ONLY - PERSISTENT NXFS NOT AVAILABLE",
                           1U, settings_persisted ? 112U : 226U,
                           settings_persisted ? 222U : 177U, settings_persisted ? 190U : 112U);
        graphics_draw_text(text_x, panel_y + panel_height - 18U,
                           "1-7 CHANGE OPTIONS   ESC RETURN HOME   SETTINGS APPLY IMMEDIATELY",
                           1U, 151U, 178U, 201U);
    } else if (active_view == DESKTOP_MONITOR) {
        graphics_draw_text_smooth(text_x, text_y, "SYSTEM MONITOR", 2U, 241U, 248U, 255U);
        graphics_draw_text(text_x, text_y + 30U,
                           "LIVE RESOURCE AND DEVICE OVERVIEW", 1U, 111U, 207U, 231U);
        char memory_total[21], memory_free[21], uptime[21], device_count[21];
        format_u64(memory_total_bytes() / (1024U * 1024U), memory_total);
        format_u64(memory_free_bytes() / (1024U * 1024U), memory_free);
        format_u64(timer_ticks() / 100U, uptime);
        format_u64(blockdev_count(), device_count);
        const char *labels[4] = {"PHYSICAL MEMORY (MiB)", "FREE MEMORY (MiB)",
                                 "UPTIME (SECONDS)", "BLOCK DEVICES"};
        const char *values[4] = {memory_total, memory_free, uptime, device_count};
        for (size_t i = 0U; i < 4U; ++i) {
            const uint64_t y = text_y + 66U + (uint64_t)i * 55U;
            graphics_fill_round_rect(panel_x + 24U, y, panel_width - 48U,
                                      43U, 9U, 25U, 43U, 66U);
            graphics_draw_round_rect(panel_x + 24U, y, panel_width - 48U,
                                     43U, 9U, 68U, 103U, 137U);
            graphics_draw_text(panel_x + 40U, y + 16U, labels[i],
                               1U, 145U, 177U, 198U);
            graphics_draw_text(panel_x + panel_width - 130U, y + 16U, values[i],
                               1U, 235U, 245U, 252U);
        }
        graphics_draw_text(text_x, text_y + 296U,
                           vfs_nxfs_mounted() && vfs_nxfs_writable()
                               ? "DATA VOLUME: NXFS MOUNTED / WRITABLE"
                               : (vfs_root_mounted() ? "DATA VOLUME: MOUNTED" :
                                  "DATA VOLUME: VOLATILE RAM"),
                           1U, 119U, 229U, 211U);
        graphics_draw_text(text_x, panel_y + panel_height - 18U,
                           "ESC RETURN HOME   VALUES REFRESH WHEN THE DESKTOP REDRAWS",
                           1U, 151U, 178U, 201U);
    } else if (active_view == DESKTOP_CONTROL_CENTER) {
        graphics_draw_text_smooth(text_x,text_y,"CONTROL CENTER", 2U,245U,249U,255U);
        graphics_draw_text(text_x,text_y+30U,
            "LIVE SYSTEM STATUS  /  QUICK PERSONALIZATION",
            1U,139U,184U,210U);
        char memory_text[32],uptime_text[32],number[21];
        format_u64(memory_free_bytes()/(1024U*1024U),number);
        size_t pos=0U;
        while(number[pos]!='\0' && pos<sizeof(memory_text)-1U) {
            memory_text[pos]=number[pos]; ++pos;
        }
        const char *memory_suffix=" MiB free";
        for(size_t i=0U;memory_suffix[i]!='\0' && pos<sizeof(memory_text)-1U;++i)
            memory_text[pos++]=memory_suffix[i];
        memory_text[pos]='\0';
        format_u64(timer_ticks()/100U,number); pos=0U;
        while(number[pos]!='\0' && pos<sizeof(uptime_text)-2U) {
            uptime_text[pos]=number[pos]; ++pos;
        }
        uptime_text[pos++]='s'; uptime_text[pos]='\0';
        const uint32_t adapters=netdev_device_count();
        const char *adapter=adapters==0U?"No supported adapter":
            netdev_device_name_at(netdev_default_index());
        if(adapter==NULL) adapter="Adapter detected";
        const char *storage=vfs_nxfs_mounted() && vfs_nxfs_writable()
            ?"NXFS writable":(vfs_root_mounted()?"Mounted / read-only":"Volatile RAM only");
        const char *status_labels[4]={
            "AVAILABLE MEMORY","SESSION UPTIME","DATA STORAGE","NETWORK ADAPTER"
        };
        const char *status_values[4]={memory_text,uptime_text,storage,adapter};
        const uint64_t status_x=panel_x+24U,status_y=text_y+54U;
        const uint64_t status_w=(panel_width-64U)/2U;
        static const uint8_t status_accent[4][3]={
            {65U,190U,210U},{132U,125U,229U},{73U,191U,159U},{219U,160U,102U}
        };
        for(size_t i=0U;i<4U;++i) {
            const uint64_t col=i%2U,row=i/2U;
            const uint64_t x=status_x+col*(status_w+16U);
            const uint64_t y=status_y+row*58U;
            graphics_fill_round_rect(x,y,status_w,50U,11U,22U,36U,54U);
            graphics_draw_round_rect(x,y,status_w,50U,11U,62U,88U,116U);
            graphics_fill_round_rect(x+1U,y+9U,3U,32U,1U,
                status_accent[i][0],status_accent[i][1],status_accent[i][2]);
            graphics_draw_text(x+13U,y+15U,status_labels[i],1U,146U,178U,202U);
            graphics_draw_text(x+13U,y+35U,status_values[i],1U,241U,247U,252U);
        }
        graphics_draw_text(text_x,text_y+184U,"QUICK SETTINGS",1U,139U,184U,210U);
        const char *quick_labels[6]={
            "COLOR THEME","WALLPAPER","HIGH CONTRAST",
            "MOTION EFFECTS","TASKBAR STATUS","ALL SETTINGS"
        };
        const char *quick_values[6]={
            settings_theme==0U?"MIDNIGHT":(settings_theme==1U?"OCEAN":"EMBER"),
            settings_wallpaper==0U?"AURORA":(settings_wallpaper==1U?"GRADIENT":"SOLID"),
            settings_high_contrast?"ENABLED":"STANDARD",
            settings_reduce_motion?"REDUCED":"FULL",
            settings_show_status?"VISIBLE":"HIDDEN",
            "OPEN SETTINGS"
        };
        const uint64_t quick_x=panel_x+24U,quick_y=text_y+198U;
        const uint64_t quick_w=(panel_width-64U)/3U;
        for(size_t i=0U;i<6U;++i) {
            const uint64_t col=i%3U,row=i/3U;
            const uint64_t x=quick_x+col*(quick_w+8U);
            const uint64_t y=quick_y+row*62U;
            const bool selected=(i==2U && settings_high_contrast) ||
                (i==3U && settings_reduce_motion) ||
                (i==4U && settings_show_status);
            graphics_fill_round_rect(x,y,quick_w,54U,10U,
                selected?34U:25U,selected?65U:43U,selected?82U:62U);
            graphics_draw_round_rect(x,y,quick_w,54U,10U,
                selected?108U:72U,selected?154U:105U,selected?170U:133U);
            graphics_draw_text(x+10U,y+19U,quick_labels[i],1U,157U,190U,213U);
            graphics_draw_text(x+10U,y+40U,quick_values[i],1U,244U,249U,255U);
        }
        graphics_draw_text(text_x,panel_y+panel_height-18U,
            "1-5 TOGGLE QUICK SETTINGS   ENTER OPENS ALL SETTINGS",
            1U,141U,173U,198U);
    } else if (active_view == DESKTOP_CALCULATOR) {
        graphics_draw_text_smooth(text_x,text_y,"NEXUS CALCULATOR", 2U,241U,248U,255U);
        graphics_draw_text(text_x,text_y+30U,
            "INTEGER MODE  /  PRECEDENCE  /  PARENTHESES",1U,111U,207U,231U);
        const uint64_t display_x=panel_x+24U, display_y=text_y+62U;
        const uint64_t display_w=panel_width-48U;
        graphics_fill_round_rect(display_x,display_y,display_w,54U,9U,8U,18U,29U);
        graphics_draw_round_rect(display_x,display_y,display_w,54U,9U,61U,97U,127U);
        graphics_draw_text(display_x+12U,display_y+18U,
            calculator_expression[0] ? calculator_expression : "Enter an expression",
            1U,231U,241U,250U);
        graphics_draw_text(display_x+display_w-220U,display_y+40U,
            calculator_error==NEXUS_CALCULATOR_OK ? calculator_result :
            nexus_calculator_error_name(calculator_error),1U,
            calculator_error==NEXUS_CALCULATOR_OK ? 119U : 255U,
            calculator_error==NEXUS_CALCULATOR_OK ? 229U : 175U,
            calculator_error==NEXUS_CALCULATOR_OK ? 211U : 119U);
        static const char *const labels[5][4]={
            {"C","(",")","/"},{"7","8","9","*"},{"4","5","6","-"},
            {"1","2","3","+"},{"0","%","DEL","="}
        };
        const uint64_t key_x=panel_x+24U, key_y=text_y+132U;
        const uint64_t key_w=(panel_width-72U)/4U;
        for(size_t row=0U;row<5U;++row) for(size_t col=0U;col<4U;++col) {
            const uint64_t x=key_x+col*(key_w+8U), y=key_y+row*50U;
            const bool action=col==3U || row==0U;
            graphics_fill_round_rect(x,y,key_w,42U,8U,
                action?39U:25U,action?70U:44U,action?112U:67U);
            graphics_draw_round_rect(x,y,key_w,42U,8U,
                action?87U:72U,action?133U:106U,action?175U:139U);
            graphics_draw_text(x+key_w/2U-6U,y+18U,labels[row][col],1U,
                row==0U && col==0U ? 255U : 245U,
                row==0U && col==0U ? 184U : 249U,
                row==0U && col==0U ? 141U : 255U);
        }
        graphics_draw_text(text_x,panel_y+panel_height-18U,
            "KEYBOARD OR MOUSE  /  ENTER EVALUATE  /  ESC HOME",
            1U,151U,178U,201U);
    } else if (active_view == DESKTOP_INSTALLER) {
        if (installer_ui_state == INSTALLER_UI_SELECT) {
            graphics_draw_text(text_x, text_y, "NEXUS INSTALLER - DRIVE SELECTION",
                               2, 239, 249, 255);
            graphics_draw_text(text_x, text_y + 28U,
                               "WELCOME TO NEXUS  /  CHOOSE WHERE TO INSTALL",
                               1, 157, 137, 255);
            graphics_fill_round_rect(text_x - 12U, text_y + 44U,
                                      548U, 120U, 12U,
                                      23U, 30U, 52U);
            graphics_draw_round_rect(text_x - 12U, text_y + 44U,
                                     548U, 120U, 12U,
                                     91U, 77U, 145U);
            graphics_fill_rect(text_x - 12U, text_y + 44U,
                               548U, 3U, 153U, 104U, 255U);
            const size_t device_count = blockdev_count();
            if (device_count == 0U) {
                graphics_draw_text(text_x, text_y + 88U,
                                   "NO SUPPORTED STORAGE DEVICES DETECTED",
                                   1, 239, 177, 119);
            } else {
                const struct nexus_block_device *device =
                    blockdev_at(installer_selected_device);
                if (device != NULL) {
                    graphics_draw_text(text_x, text_y + 54U,
                                       "SELECTED DRIVE:", 1, 233, 243, 252);
                    graphics_draw_text(text_x + 140U, text_y + 54U,
                                       device->name, 1, 119, 229, 211);
                    graphics_draw_text(text_x, text_y + 72U,
                                       device->model, 1, 170, 225, 190);
                    graphics_draw_text(text_x, text_y + 90U,
                                       device->stable_id, 1, 170, 191, 211);
                    const uint64_t blocks_per_gib =
                        (1024ULL * 1024ULL * 1024ULL) / device->block_size;
                    char capacity[21], sector[21];
                    format_u64(blocks_per_gib == 0U ? 0U :
                               device->block_count / blocks_per_gib, capacity);
                    format_u64(device->block_size, sector);
                    graphics_draw_text(text_x, text_y + 112U,
                                       "CAPACITY (GiB):", 1, 233, 243, 252);
                    graphics_draw_text(text_x + 140U, text_y + 112U,
                                       capacity, 1, 170, 225, 190);
                    graphics_draw_text(text_x + 250U, text_y + 112U,
                                       blockdev_identity_is_unique(device)
                                           ? "IDENTITY: UNIQUE"
                                           : "IDENTITY: NOT SAFE",
                                       1, blockdev_identity_is_unique(device)
                                           ? 108 : 239,
                                       blockdev_identity_is_unique(device)
                                           ? 216 : 177,
                                       blockdev_identity_is_unique(device)
                                           ? 229 : 119);
                    graphics_draw_text(text_x, text_y + 134U,
                                       "LOGICAL SECTOR:", 1, 233, 243, 252);
                    graphics_draw_text(text_x + 140U, text_y + 134U,
                                       sector, 1, 170, 225, 190);
                    graphics_draw_text(text_x, text_y + 156U,
                                       installer_firmware == NEXUS_FIRMWARE_UEFI
                                           ? "FIRMWARE PLAN: UEFI / GPT"
                                           : (installer_firmware == NEXUS_FIRMWARE_LEGACY_BIOS
                                               ? "FIRMWARE PLAN: LEGACY BIOS + ESP / GPT"
                                               : "FIRMWARE PLAN: UEFI + BIOS / GPT"),
                                       1, 108, 216, 229);
                    if (installer_plan_valid) {
                        graphics_draw_text(text_x, text_y + 180U,
                                           "PROPOSED PARTITIONS (READ ONLY)",
                                           1, 119, 229, 211);
                        for (size_t i = 0U; i < installer_plan.partition_count; ++i) {
                            const struct nexus_planned_partition *part =
                                &installer_plan.partitions[i];
                            const char *label =
                                part->kind == NEXUS_PARTITION_ESP ? "EFI SYSTEM" :
                                (part->kind == NEXUS_PARTITION_BIOS_BOOT
                                    ? "BIOS BOOT" :
                                (part->kind == NEXUS_PARTITION_SYSTEM
                                    ? "NEXUS SYSTEM" :
                                (part->kind == NEXUS_PARTITION_RECOVERY
                                    ? "NEXUS RECOVERY" : "NEXUS DATA")));
                            const uint64_t sectors_per_mib =
                                (1024ULL * 1024ULL) / device->block_size;
                            const uint64_t size_mib =
                                (part->last_lba - part->first_lba + 1U) /
                                sectors_per_mib;
                            char size_text[21];
                            format_u64(size_mib, size_text);
                            const uint64_t row_y = text_y + 202U + i * 16U;
                            graphics_draw_text(text_x, row_y, label,
                                               1, 233, 243, 252);
                            graphics_draw_text(text_x + 190U, row_y,
                                               size_text, 1, 170, 225, 190);
                            graphics_draw_text(text_x + 230U, row_y,
                                               "MiB", 1, 170, 191, 211);
                        }
                    } else {
                        graphics_draw_text(text_x, text_y + 194U,
                                           "DISK TOO SMALL OR LAYOUT UNSUPPORTED",
                                           1, 239, 177, 119);
                    }
                    graphics_draw_text(text_x, text_y + 274U,
                                       installer_target_status(device), 1,
                                       170, 225, 190);
                }
            }
            graphics_draw_text(text_x, text_y + 298U,
                               "UP/DOWN CHOOSE DRIVE  F1 UEFI  F2 BIOS  F3 DUAL",
                               1, 170, 191, 211);
            graphics_draw_text(text_x, text_y + 316U,
                               "ENTER REVIEW  ESC HOME",
                               1, 170, 191, 211);
        } else {
            const struct nexus_block_device *device =
                blockdev_at(installer_selected_device);
            graphics_draw_text(text_x, text_y, "NEXUS INSTALLATION WIZARD",
                               2, 239, 249, 255);
            if (device != NULL) {
                graphics_draw_text(text_x, text_y + 36U, device->model,
                                   1, 170, 225, 190);
                graphics_draw_text(text_x, text_y + 54U, device->stable_id,
                                   1, 119, 229, 211);
            }
            if (installer_ui_state == INSTALLER_UI_CONFIRM_ERASE) {
                graphics_fill_rect(text_x - 12U, text_y + 82U, 548U, 150U,
                                   24U, 29U, 52U);
                graphics_draw_rect(text_x - 12U, text_y + 82U, 548U, 150U,
                                   111U, 76U, 166U);
                graphics_fill_rect(text_x - 12U, text_y + 82U, 548U, 4U,
                                   153U, 104U, 255U);
                graphics_draw_text(text_x, text_y + 100U,
                                   "READY TO INSTALL NEXUS?",
                                   2, 239, 249, 255);
                graphics_draw_text(text_x, text_y + 130U,
                                   "ALL DATA ON THIS DRIVE WILL BE ERASED.",
                                   1, 255, 166, 154);
                graphics_draw_text(text_x, text_y + 150U,
                                   "Check the drive above before continuing.",
                                   1, 190, 199, 222);
                graphics_fill_rect(text_x, text_y + 180U, 300U, 42U,
                                   119U, 78U, 220U);
                graphics_draw_rect(text_x, text_y + 180U, 300U, 42U,
                                   178U, 151U, 255U);
                graphics_draw_text(text_x + 24U, text_y + 197U,
                                   "CONFIRM & INSTALL",
                                   1, 255, 255, 255);
                graphics_fill_rect(text_x + 312U, text_y + 180U, 112U, 42U,
                                   48U, 57U, 78U);
                graphics_draw_rect(text_x + 312U, text_y + 180U, 112U, 42U,
                                   91U, 105U, 137U);
                graphics_draw_text(text_x + 337U, text_y + 197U,
                                   "CANCEL",
                                   1, 232, 237, 248);
            } else if (installer_ui_state == INSTALLER_UI_RUNNING) {
                graphics_draw_text(text_x, text_y + 116U,
                                   "INSTALLATION IN PROGRESS - DO NOT POWER OFF",
                                   1, 239, 177, 119);
            } else if (installer_ui_state == INSTALLER_UI_COMPLETE) {
                graphics_draw_text(text_x, text_y + 116U,
                                   "INSTALLATION DEPLOYMENT COMPLETED",
                                   1, 108, 216, 229);
                graphics_fill_rect(text_x, text_y + 194U, 244U, 36U,
                                   0U, 120U, 215U);
                graphics_draw_rect(text_x, text_y + 194U, 244U, 36U,
                                   65U, 165U, 230U);
                graphics_draw_text(text_x + 12U, text_y + 208U,
                                   "RESTART INTO NEXUS OS",
                                   1, 255, 255, 255);
                graphics_draw_text(text_x, text_y + 248U,
                                   "REMOVE INSTALL USB, THEN PRESS ENTER OR CLICK RESTART",
                                   1, 239, 177, 119);
            } else if (installer_ui_state == INSTALLER_UI_FAILED) {
                graphics_draw_text(text_x, text_y + 116U,
                                   "INSTALL STOPPED - READ THE MESSAGE BELOW, THEN RETRY",
                                   1, 239, 177, 119);
            }
            graphics_draw_text(text_x, text_y + 160U, installer_status_text,
                               1, 170, 225, 190);
            if (installer_ui_state == INSTALLER_UI_CONFIRM_ERASE) {
                graphics_draw_text(text_x, text_y + 244U,
                                   "Click Confirm & Install or press ENTER.",
                                   1, 190, 199, 222);
                graphics_draw_text(text_x, text_y + 262U,
                                   "Nothing is changed until you confirm.",
                                   1, 170, 191, 211);
            } else if (installer_ui_state == INSTALLER_UI_RUNNING) {
                graphics_fill_rect(text_x, text_y + 194U, 420U, 8U,
                                   48U, 57U, 78U);
                graphics_fill_rect(text_x, text_y + 194U, 110U, 8U,
                                   153U, 104U, 255U);
                graphics_draw_text(text_x, text_y + 218U,
                                   "Please keep the computer powered on.",
                                   1, 190, 199, 222);
            }
        }
    } else if (active_view == DESKTOP_FILES) {
        graphics_draw_text(text_x, text_y, "NEXUS FILES",
                           2, 239, 249, 255);
        if (files_editor_state != FILES_EDITOR_CLOSED) {
            graphics_draw_text(text_x, text_y + 48U,
                files_editor_state == FILES_EDITOR_NAME
                    ? "NEW FILE - ENTER A NAME"
                    : files_editor_state == FILES_EDITOR_RENAME
                        ? "RENAME FILE - ENTER A NEW NAME"
                        : files_editor_state == FILES_EDITOR_COPY
                            ? "COPY FILE - ENTER DESTINATION NAME"
                            : "TEXT EDITOR - 1MB LIMIT",
                1, 108, 216, 229);
            graphics_draw_text(text_x, text_y + 82U, "NAME:",
                               1, 233, 243, 252);
            graphics_draw_text(text_x + 48U, text_y + 82U, files_edit_name,
                               1, 119, 229, 211);
            if (files_editor_state == FILES_EDITOR_CONTENT) {
                for (size_t line = 0U; line < 4U; ++line) {
                    char display[65];
                    const size_t offset =
                        files_edit_view_offset + line * 64U;
                    size_t amount = 0U;
                    while (amount < 64U &&
                           offset + amount < files_edit_content_length) {
                        display[amount] = files_edit_content[offset + amount];
                        ++amount;
                    }
                    display[amount] = '\0';
                    graphics_draw_text(text_x, text_y + 124U + line * 28U,
                                       display, 1, 239, 249, 255);
                }
                graphics_draw_text(text_x, text_y + 260U,
                    vfs_nxfs_writable()
                        ? "SAVE TARGET: PERSISTENT NXFS"
                        : "SAVE TARGET: VOLATILE RAM ONLY",
                    1, 170, 191, 211);
                graphics_draw_text(text_x, text_y + 292U,
                    "PGUP/PGDN SCROLL  HOME/END  F10 SAVE  ENTER NEW LINE",
                    1, 170, 191, 211);
            } else if (files_editor_state == FILES_EDITOR_RENAME ||
                       files_editor_state == FILES_EDITOR_COPY) {
                graphics_draw_text(text_x, text_y + 124U,
                    "UP TO 31 CHARS: LETTERS, NUMBERS, _ - AND ONE DOT",
                    1, 170, 191, 211);
                graphics_draw_text(text_x, text_y + 156U,
                    files_editor_state == FILES_EDITOR_RENAME
                        ? "ENTER RENAME  ESC CANCEL"
                        : "ENTER COPY  ESC CANCEL",
                    1, 170, 191, 211);
            } else {
                graphics_draw_text(text_x, text_y + 124U,
                    "UP TO 31 CHARS: LETTERS, NUMBERS, _ - AND ONE DOT",
                    1, 170, 191, 211);
                graphics_draw_text(text_x, text_y + 156U,
                    "ENTER CONTINUE  ESC CANCEL",
                    1, 170, 191, 211);
            }
            graphics_draw_text(text_x, text_y + 340U, files_edit_status,
                               1, 239, 177, 119);
        } else {
        graphics_draw_text(text_x, text_y + 48U,
                           "STORAGE DEVICE DISCOVERY IS ACTIVE",
                           1, 108, 216, 229);
        graphics_draw_text(text_x, text_y + 78U,
                           "DISKS ARE CURRENTLY READ ONLY",
                           1, 233, 243, 252);
        if (!files_scanned) {
            graphics_draw_text(text_x, text_y + 108U,
                               "PRESS F1 TO SCAN A READ ONLY DISK",
                               1, 170, 191, 211);
        } else if (!files_scan_ok && files_vfs_ready) {
            graphics_draw_text(text_x, text_y + 108U,
                               "VOLATILE RAM FILESYSTEM",
                               1, 108, 216, 229);
            graphics_draw_text(text_x, text_y + 138U,
                               "NO DISK VOLUME IS MOUNTED",
                               1, 170, 191, 211);
        } else if (!files_scan_ok) {
            graphics_draw_text(text_x, text_y + 108U,
                               "NO SUPPORTED FAT32 GPT PARTITION FOUND",
                               1, 239, 177, 119);
        } else {
            graphics_draw_text(text_x, text_y + 108U,
                               "GPT PARTITIONS DISCOVERED",
                               1, 108, 216, 229);
            char count_text[21];
            char first_text[21];
            char last_text[21];
            format_u64((uint64_t)files_partition_count, count_text);
            format_u64(files_first_lba, first_text);
            format_u64(files_last_lba, last_text);
            graphics_draw_text(text_x, text_y + 138U,
                               "PARTITIONS:", 1, 233, 243, 252);
            graphics_draw_text(text_x + 100U, text_y + 138U,
                               count_text, 1, 170, 191, 211);
            graphics_draw_text(text_x, text_y + 168U,
                               "FIRST LBA:", 1, 233, 243, 252);
            graphics_draw_text(text_x + 100U, text_y + 168U,
                               first_text, 1, 170, 191, 211);
            graphics_draw_text(text_x, text_y + 198U,
                               "LAST LBA:", 1, 233, 243, 252);
            graphics_draw_text(text_x + 100U, text_y + 198U,
                               last_text, 1, 170, 191, 211);
            if (files_vfs_ready) {
                graphics_draw_text(text_x, text_y + 204U,
                    vfs_directory_depth() == 0U
                        ? "LOCATION: /  (ROOT)"
                        : "LOCATION: SUBDIRECTORY  /  ESC TO GO UP",
                    1U, 108U, 216U, 229U);
                graphics_draw_text(text_x, text_y + 228U,
                                   "FIRST ITEM:", 1, 233, 243, 252);
                graphics_draw_text(text_x + 100U, text_y + 228U,
                                   files_first_name, 1, 119, 229, 211);
                graphics_draw_text(text_x, text_y + 258U,
                                   "READ ONLY PREVIEW:", 1,
                                   233, 243, 252);
                graphics_draw_text(text_x + 150U, text_y + 258U,
                                   files_preview, 1, 170, 191, 211);
            } else {
                graphics_draw_text(text_x, text_y + 228U,
                                   "NO SUPPORTED FAT32 VOLUME",
                                   1, 239, 177, 119);
            }
        }
        if (files_vfs_ready) {
            if (files_delete_pending) {
                graphics_draw_text(text_x, text_y + 340U,
                    "DELETE FILE? Y CONFIRM  N / ESC CANCEL",
                    1U, 255U, 177U, 119U);
                graphics_draw_text(text_x, text_y + 360U,
                    files_delete_name, 1U, 239U, 249U, 255U);
            }
            static const char *const wide_actions[6] = {
                "NEW", "OPEN", "COPY", "RENAME", "DELETE", "UP / REFRESH"
            };
            static const char *const compact_actions[4] = {
                "NEW", "OPEN", "DELETE", "UP"
            };
            static const uint8_t action_colors[6][3] = {
                {37U, 139U, 165U}, {60U, 116U, 170U},
                {81U, 135U, 170U}, {128U, 111U, 184U},
                {153U, 69U, 78U}, {67U, 133U, 111U}
            };
            const size_t action_count = panel_width >= 560U ? 6U : 4U;
            const uint64_t action_gap = 6U;
            const uint64_t action_width =
                (panel_width - 64U - (action_count - 1U) * action_gap) /
                action_count;
            for (size_t action = 0U; action < action_count; ++action) {
                const uint64_t action_x = text_x +
                    (uint64_t)action * (action_width + action_gap);
                const uint8_t color_index = action_count == 6U
                    ? (uint8_t)action
                    : (uint8_t)(action < 2U ? action : action + 2U);
                const char *label = action_count == 6U
                    ? wide_actions[action] : compact_actions[action];
                graphics_fill_round_rect(action_x, text_y + 270U,
                                         action_width, 25U, 6U,
                                         25U, 44U, 65U);
                graphics_draw_round_rect(action_x, text_y + 270U,
                                         action_width, 25U, 6U,
                                         action_colors[color_index][0],
                                         action_colors[color_index][1],
                                         action_colors[color_index][2]);
                graphics_fill_round_rect(action_x + 1U, text_y + 276U,
                                         3U, 13U, 1U,
                                         action_colors[color_index][0],
                                         action_colors[color_index][1],
                                         action_colors[color_index][2]);
                graphics_draw_text(action_x + 9U, text_y + 279U,
                                   label, 1U, 238U, 246U, 252U);
            }
            char files_path_display[128];
            if (vfs_current_path(files_path_display,
                                 sizeof(files_path_display))) {
                graphics_draw_text(text_x, text_y + 304U,
                                   "LOCATION:", 1U, 108U, 216U, 229U);
                graphics_draw_text(text_x + 82U, text_y + 304U,
                                   files_path_display, 1U,
                                   239U, 249U, 255U);
            } else {
                graphics_draw_text(text_x, text_y + 304U,
                                   "LOCATION UNAVAILABLE", 1U,
                                   255U, 177U, 119U);
            }
            for (size_t slot = 0U; slot < FILES_LISTING_VISIBLE; ++slot) {
                const size_t index = files_listing_first_index + slot;
                if (index >= files_listing_count) break;
                const uint64_t column = slot % 2U;
                const uint64_t line = slot / 2U;
                const bool selected = index == files_selected_index;
                if (selected)
                    graphics_fill_rect(text_x + column * 260U - 6U,
                                       text_y + 321U + line * 24U,
                                       240U, 20U, 34U, 78U, 105U);
                if (files_listing_types[index] == NEXUS_VFS_DIRECTORY)
                    graphics_draw_text(text_x + column * 260U,
                                       text_y + 326U + line * 24U,
                                       "DIR", 1U, 255U, 190U, 112U);
                graphics_draw_text(text_x + column * 260U +
                                       (files_listing_types[index] == NEXUS_VFS_DIRECTORY ? 28U : 0U),
                                   text_y + 326U + line * 24U,
                                   files_listing[index], 1U,
                                   selected ? 119U : 233U,
                                   selected ? 229U : 243U,
                                   selected ? 211U : 252U);
            }
            if (files_listing_count == 0U) {
                graphics_draw_text(text_x, text_y + 326U, "NO FILES",
                                   1U, 170U, 191U, 211U);
            }
            graphics_draw_text(text_x, text_y + 390U,
                               "UP/DOWN SELECT  ENTER OPEN  C COPY  R RENAME / SEARCH",
                               1, 170, 191, 211);
            if (files_search_active) {
                graphics_draw_text(text_x, text_y + 414U,
                                   "SEARCH (ENTER APPLY / ESC CANCEL):",
                                   1U, 255U, 190U, 112U);
                graphics_draw_text(text_x + 220U, text_y + 414U,
                                   files_search_input, 1U,
                                   239U, 249U, 255U);
            } else if (files_search_query[0] != '\0') {
                graphics_draw_text(text_x, text_y + 414U,
                                   "FILTER:", 1U, 170U, 191U, 211U);
                graphics_draw_text(text_x + 55U, text_y + 414U,
                                   files_search_query, 1U,
                                   119U, 229U, 211U);
            } else if (files_listing_truncated) {
                graphics_draw_text(text_x, text_y + 414U,
                                   "LISTING LIMITED TO 128 ENTRIES",
                                   1, 239, 177, 119);
            }
        }
        }
    } else if (active_view == DESKTOP_TERMINAL) {
        graphics_draw_text(text_x, text_y, "NEXUS COMMAND SHELL",
                           2, 239, 249, 255);
        graphics_draw_text(text_x, text_y + 42U,
                           "RAM FILE WRITES ARE VOLATILE",
                           1, 108, 216, 229);
        graphics_draw_text(text_x, text_y + 82U, terminal_output,
                           1, 170, 225, 190);
        graphics_draw_text(text_x, text_y + 128U, "nexus> ",
                           1, 119, 229, 211);
        graphics_draw_text(text_x + 56U, text_y + 128U, terminal_input,
                           1, 239, 249, 255);
        graphics_draw_text(text_x, text_y + 180U,
                           "HELP  ABOUT  MEM  NET  DNS  HTTP  FILES  RAM",
                           1, 170, 191, 211);
        graphics_draw_text(text_x, text_y + 210U,
                           "PRESS ESC TO RETURN HOME",
                           1, 119, 229, 211);
    } else if (active_view == DESKTOP_BROWSER) {
        /* Modern browser chrome with a real query/address input path. */
        graphics_fill_gradient_rect(panel_x + 18U, text_y - 8U,
            panel_width - 36U, 30U, 29U, 58U, 83U, 18U, 35U, 55U);
        graphics_fill_round_rect(panel_x + 18U, text_y - 8U,
            panel_width - 36U, 30U, 8U, 23U, 44U, 65U);
        graphics_fill_round_rect(panel_x + 18U, text_y - 8U,
            4U, 30U, 2U, 79U, 204U, 202U);
        desktop_draw_logo(panel_x + 28U, text_y - 3U, 20U);
        graphics_draw_text(panel_x + 56U, text_y + 1U,
                           "NEXUS BROWSER", 1U, 239U, 249U, 255U);
        graphics_draw_text(panel_x + panel_width - 170U, text_y + 1U,
                           "WEB AND SEARCH", 1U, 120U, 210U, 205U);

        const uint64_t toolbar_y = text_y + 28U;
        graphics_fill_round_rect(panel_x + 18U, toolbar_y,
            panel_width - 36U, 42U, 10U, 12U, 21U, 36U);
        graphics_draw_round_rect(panel_x + 18U, toolbar_y,
            panel_width - 36U, 42U, 10U, 51U, 81U, 105U);
        graphics_fill_round_rect(panel_x + 29U, toolbar_y + 8U,
            27U, 26U, 7U, 29U, 48U, 68U);
        graphics_draw_text(panel_x + 39U, toolbar_y + 17U,
                           "<", 1U, 170U, 194U, 213U);
        graphics_fill_round_rect(panel_x + 64U, toolbar_y + 8U,
            27U, 26U, 7U, 29U, 48U, 68U);
        graphics_draw_text(panel_x + 74U, toolbar_y + 17U,
                           ">", 1U, 170U, 194U, 213U);
        const uint64_t address_x = panel_x + 101U;
        const uint64_t address_width = panel_width > 180U
            ? panel_width - 119U : panel_width - 110U;
        graphics_fill_round_rect(address_x, toolbar_y + 6U,
            address_width, 30U, 8U, 29U, 48U, 68U);
        graphics_draw_round_rect(address_x, toolbar_y + 6U,
            address_width, 30U, 8U, 72U, 111U, 133U);
        graphics_fill_round_rect(address_x + 8U, toolbar_y + 14U,
            6U, 6U, 3U, browser_search_mode ? 83U : 98U,
            browser_search_mode ? 213U : 170U,
            browser_search_mode ? 203U : 208U);
        graphics_draw_text(address_x + 22U, toolbar_y + 18U,
            browser_address[0] ? browser_address :
            "Search web or enter address", 1U, 230U, 240U, 248U);

        const uint64_t content_y = toolbar_y + 54U;
        const uint64_t content_height = panel_height > 230U
            ? panel_height - 150U : 80U;
        graphics_fill_round_rect(panel_x + 18U, content_y,
            panel_width - 36U, content_height, 13U, 244U, 247U, 251U);
        if (!browser_has_page) {
            const uint64_t center_x = panel_x + panel_width / 2U;
            desktop_draw_logo(center_x - 24U, content_y + 20U, 48U);
            graphics_draw_text(center_x - 74U, content_y + 86U,
                "NEXUS BROWSER", 2U, 29U, 48U, 67U);
            graphics_draw_text(center_x - 145U, content_y + 111U,
                "SEARCH THE WEB OR OPEN A WEBSITE", 1U,
                76U, 104U, 125U);
            const uint64_t search_x = panel_x + 48U;
            const uint64_t search_y = content_y + 138U;
            const uint64_t search_w = panel_width - 96U;
            graphics_fill_round_rect(search_x, search_y,
                search_w, 42U, 13U, 255U, 255U, 255U);
            graphics_draw_round_rect(search_x, search_y,
                search_w, 42U, 13U, 115U, 151U, 176U);
            graphics_fill_round_rect(search_x + 15U, search_y + 13U,
                12U, 12U, 6U, 60U, 150U, 171U);
            graphics_draw_text(search_x + 39U, search_y + 25U,
                "TYPE A QUERY ABOVE AND PRESS ENTER", 1U,
                73U, 100U, 121U);
            const uint64_t chip_y = search_y + 62U;
            static const char *const chips[3] = {
                "WEB SEARCH", "DIRECT HTTP", "NEXUS HELP"
            };
            const uint64_t chip_w = (search_w - 24U) / 3U;
            for (size_t i = 0U; i < 3U; ++i) {
                const uint64_t chip_x = search_x + i * (chip_w + 12U);
                graphics_fill_round_rect(chip_x, chip_y, chip_w, 27U,
                    8U, 230U, 237U, 244U);
                graphics_draw_text(chip_x + 8U, chip_y + 17U,
                    chips[i], 1U, 61U, 91U, 116U);
            }
            graphics_draw_text(search_x, content_y + content_height - 18U,
                "HTTPS SEARCH IS BLOCKED UNTIL TLS AND CERTIFICATES ARE READY",
                1U, 177U, 104U, 73U);
        } else {
            graphics_fill_round_rect(panel_x + 32U, content_y + 14U,
                panel_width - 64U, 27U, 8U, 225U, 234U, 243U);
            graphics_draw_text(panel_x + 46U, content_y + 31U,
                browser_search_mode ? "SEARCH RESPONSE" :
                    "PAGE CONTENT  |  ALT+1..5 SOURCES",
                1U, 48U, 88U, 112U);
            const uint64_t row_space = content_height > 64U
                ? content_height - 64U : 0U;
            const size_t available_rows = (size_t)((row_space + 19U) / 20U);
            size_t visible_sources = browser_source_count;
            const size_t max_source_rows = available_rows > 2U
                ? available_rows - 2U : 0U;
            if (visible_sources > max_source_rows)
                visible_sources = max_source_rows;
            size_t visible_text_rows = browser_line_count;
            const size_t text_capacity = available_rows - visible_sources;
            if (visible_text_rows > text_capacity)
                visible_text_rows = text_capacity;
            const size_t max_scroll = browser_line_count > visible_text_rows
                ? browser_line_count - visible_text_rows : 0U;
            if (browser_scroll_offset > max_scroll)
                browser_scroll_offset = max_scroll;
            for (size_t row = 0U; row < visible_text_rows; ++row) {
                const uint64_t row_y = content_y + 48U + row * 20U;
                if (row_y + 16U >= content_y + content_height) break;
                graphics_fill_round_rect(panel_x + 32U, row_y,
                    panel_width - 64U, 18U, 4U,
                    (row & 1U) ? 247U : 236U,
                    (row & 1U) ? 249U : 242U,
                    (row & 1U) ? 251U : 247U);
                graphics_draw_text(panel_x + 44U, row_y + 12U,
                    browser_lines[browser_scroll_offset + row],
                    1U, 37U, 57U, 75U);
            }
            for (size_t source = 0U; source < visible_sources; ++source) {
                const size_t row = visible_text_rows + source;
                const uint64_t row_y = content_y + 48U + row * 20U;
                if (row_y + 16U >= content_y + content_height) break;
                graphics_fill_round_rect(panel_x + 32U, row_y,
                    panel_width - 64U, 18U, 4U,
                    228U, 239U, 248U);
                char source_line[NEXUS_BROWSER_LINE_WIDTH] = {0};
                static const char prefix[] = "ALT+";
                size_t used = 0U;
                for (size_t n = 0U; n < sizeof(prefix) - 1U; ++n)
                    source_line[used++] = prefix[n];
                source_line[used++] = (char)('1' + source);
                source_line[used++] = ' ';
                for (size_t n = 0U; browser_sources[source][n] != '\0' &&
                     used + 1U < sizeof(source_line); ++n)
                    source_line[used++] = browser_sources[source][n];
                source_line[used] = '\0';
                graphics_draw_text(panel_x + 44U, row_y + 12U,
                    source_line, 1U, 31U, 104U, 166U);
            }
        }
        graphics_fill_round_rect(panel_x + 18U,
            panel_y + panel_height - 34U, panel_width - 36U, 22U,
            7U, 17U, 29U, 46U);
        graphics_draw_text(panel_x + 30U, panel_y + panel_height - 19U,
            browser_status, 1U, 160U, 207U, 196U);
        graphics_draw_text(panel_x + panel_width - 176U,
            panel_y + panel_height - 19U,
            browser_source_count != 0U ? "ALT+1..5 LINKS" :
            (browser_search_mode ? "SEARCH" : "HTTP"), 1U,
            111U, 207U, 231U);
    } else {
        graphics_draw_text(text_x, text_y, "SYSTEM STATUS",
                           2, 239, 249, 255);

        char line[64];
        format_labeled_u64("MEMORY TOTAL: ", memory_total_bytes(), line);
        graphics_draw_text(text_x, text_y + 42U, line, 1,
                           233, 243, 252);
        format_labeled_u64("MEMORY FREE: ", memory_free_bytes(), line);
        graphics_draw_text(text_x, text_y + 66U, line, 1,
                           170, 225, 190);
        format_labeled_u64("UPTIME SECONDS: ", timer_ticks() / 100U, line);
        graphics_draw_text(text_x, text_y + 90U, line, 1,
                           233, 243, 252);

        struct nexus_net_info info;
        if (net_get_info(&info) && info.interface_ready) {
            graphics_draw_text(text_x, text_y + 126U,
                               info.ipv4_configured
                                   ? "NETWORK: ETHERNET / DHCP BOUND"
                                   : "NETWORK: ETHERNET / DHCP WAITING",
                               1, 108, 216, 229);
            if (info.ipv4_configured) {
                char address[16];
                format_ipv4_address(info.ipv4, address);
                graphics_draw_text(text_x, text_y + 150U,
                                   "IP ADDRESS:", 1, 233, 243, 252);
                graphics_draw_text(text_x + 100U, text_y + 150U,
                                   address, 1, 170, 225, 190);
                format_ipv4_address(info.gateway, address);
                graphics_draw_text(text_x, text_y + 174U,
                                   "GATEWAY:", 1, 233, 243, 252);
                graphics_draw_text(text_x + 100U, text_y + 174U,
                                   address, 1, 170, 225, 190);
                format_ipv4_address(info.dns_server, address);
                graphics_draw_text(text_x, text_y + 198U,
                                   "DNS SERVER:", 1, 233, 243, 252);
                graphics_draw_text(text_x + 100U, text_y + 198U,
                                   address, 1, 170, 225, 190);
            }
        } else {
            graphics_draw_text(text_x, text_y + 126U,
                               "NETWORK: NO ETHERNET INTERFACE",
                               1, 239, 177, 119);
        }

        if (vfs_root_mounted() && vfs_nxfs_mounted()) {
            graphics_draw_text(text_x, text_y + 234U,
                               vfs_nxfs_writable()
                                   ? "STORAGE: FAT32 + WRITABLE NXFS + RAM"
                                   : "STORAGE: FAT32 + NXFS READ ONLY + RAM",
                               1, 108, 216, 229);
        } else if (vfs_root_mounted()) {
            graphics_draw_text(text_x, text_y + 234U,
                               "STORAGE: FAT32 READ-ONLY + RAM",
                               1, 108, 216, 229);
        } else if (vfs_nxfs_mounted()) {
            graphics_draw_text(text_x, text_y + 234U,
                               vfs_nxfs_writable()
                                   ? "STORAGE: WRITABLE NEXUSFS"
                                   : "STORAGE: NEXUSFS READ ONLY",
                               1, 108, 216, 229);
        } else if (vfs_has_filesystem()) {
            graphics_draw_text(text_x, text_y + 234U,
                               "STORAGE: VOLATILE RAM FILESYSTEM",
                               1, 239, 177, 119);
        } else {
            graphics_draw_text(text_x, text_y + 234U,
                               "STORAGE: NO FILESYSTEM MOUNTED",
                               1, 239, 177, 119);
        }
        graphics_draw_text(text_x, text_y + 258U,
                           "WI-FI CHIPSET DRIVER: NOT INSTALLED",
                           1, 239, 177, 119);
        format_labeled_u64("PCI WLAN CANDIDATES: ",
                           pci_wireless_lan_candidate_count(), line);
        graphics_draw_text(text_x, text_y + 282U, line,
                           1, 233, 243, 252);
        format_labeled_u64("USB DEVICES ENUMERATED: ",
                           nexus_usb_inventory_count(), line);
        graphics_draw_text(text_x, text_y + 306U, line,
                           1, 233, 243, 252);
        size_t inventory_row = 0U;
        for (uint32_t i = 0U; i < pci_device_count() &&
             inventory_row < 3U; ++i) {
            const struct nexus_pci_device *device = pci_device_at(i);
            if (!pci_is_wireless_lan_candidate(device)) continue;
            const char *chipset = wifi_pci_chipset_name(
                device->vendor_id, device->device_id);
            graphics_draw_text(text_x, text_y + 354U + inventory_row * 20U,
                "PCI WI-FI CANDIDATE:", 1U, 133U, 188U, 211U);
            graphics_draw_text(text_x + 150U,
                text_y + 354U + inventory_row * 20U,
                chipset != NULL ? chipset : "UNKNOWN CHIPSET (SEE LOG)",
                1U, 233U, 243U, 252U);
            ++inventory_row;
        }
        for (size_t i = 0U; i < nexus_usb_inventory_count() &&
             inventory_row < 3U; ++i) {
            struct nexus_usb_device_info usb = {0};
            if (!nexus_usb_inventory_get(i, &usb)) continue;
            const char *chipset = wifi_usb_chipset_name(
                usb.vendor_id, usb.product_id);
            if (chipset == NULL && !usb.has_vendor_specific_interface)
                continue;
            graphics_draw_text(text_x, text_y + 354U + inventory_row * 20U,
                chipset != NULL ? "USB WI-FI ADAPTER:" : "USB VENDOR DEVICE:",
                1U, 133U, 188U, 211U);
            graphics_draw_text(text_x + 190U,
                text_y + 354U + inventory_row * 20U,
                chipset != NULL ? chipset : "UNKNOWN DEVICE (SEE LOG)",
                1U, 233U, 243U, 252U);
            ++inventory_row;
        }
        if (inventory_row == 0U) {
            graphics_draw_text(text_x, text_y + 354U,
                "NO WI-FI ADAPTER CANDIDATE FOUND",
                1U, 170U, 191U, 211U);
        }
        graphics_draw_text(text_x, text_y + 414U,
                           "ESC RETURNS HOME - STATUS REFRESHES ON INPUT",
                           1, 170, 191, 211);
    }
    active_view = saved_view;
    window_maximized = saved_maximized;
}

static bool desktop_draw_managed_windows(uint64_t top_bar) {
    if (!desktop_window_manager_ready) return false;
    const nexus_window_id focused = nexus_wm_focused();
    const size_t count = nexus_wm_count();
    bool drawn = false;

    /* Render every live application window back-to-front. The existing app
     * controls remain connected to the focused view while other apps retain
     * visible, independently focusable windows underneath. */
    for (size_t z = 0U; z < count; ++z) {
        nexus_window_id id = 0U;
        struct nexus_window_info info;
        if (!nexus_wm_z_order(z, &id) || !nexus_wm_get(id, &info) ||
            (info.flags & NEXUS_WINDOW_VISIBLE) == 0U ||
            info.application_id < (uint32_t)DESKTOP_FILES ||
            info.application_id > (uint32_t)DESKTOP_CALCULATOR) continue;
        desktop_draw_window_surface((enum desktop_view)info.application_id,
                                    (uint64_t)info.x,
                                    top_bar + (uint64_t)info.y,
                                    info.width, info.height,
                                    (info.flags & NEXUS_WINDOW_MAXIMIZED) != 0U,
                                    id == focused);
        drawn = true;
    }
    desktop_sync_focused_window();
    return drawn;
}

static void draw_shell(void) {
    /* A full repaint replaces the cursor's saved background. */
    pointer_under_valid = false;
    const uint64_t width = screen_width;
    const uint64_t height = screen_height;
    const uint64_t top_bar = clamp_min(height / 16U, 34U);
    const uint64_t bottom_bar = clamp_min(height / 11U, 48U);

    /* Nexus Aurora wallpaper: a deep vertical gradient with restrained
     * geometric light ribbons. All effects are native framebuffer drawing. */
    const uint8_t palette = settings_theme < 5U ? settings_theme : 0U;
    const uint8_t base_r = desktop_palette_colors[palette][0];
    const uint8_t base_g = desktop_palette_colors[palette][1];
    const uint8_t base_b = desktop_palette_colors[palette][2];
    const uint8_t end_r = desktop_palette_colors[palette][3];
    const uint8_t end_g = desktop_palette_colors[palette][4];
    const uint8_t end_b = desktop_palette_colors[palette][5];
    const uint8_t accent_r = (uint8_t)(60U + palette * 31U);
    const uint8_t accent_g = (uint8_t)(130U + (palette * 13U) % 100U);
    const uint8_t accent_b = (uint8_t)(170U + (palette * 11U) % 80U);
    const uint8_t background = settings_wallpaper < 5U
        ? settings_wallpaper : 0U;
    if (background == 2U) {
        graphics_fill_rect(0U, 0U, width, height, base_r, base_g, base_b);
    } else {
        graphics_fill_gradient_rect(0U, 0U, width, height,
                                    base_r, base_g, base_b,
                                    end_r, end_g, end_b);
        if (background == 0U && width > 180U && height > 180U) {
            /*
             * Layered Aurora artwork: overlapping capsules, soft-tinted
             * discs and thin orbit outlines create depth without a GPU.
             * Colors are blended into the active palette at draw time.
             */
            const uint64_t ribbon_h = height / 8U > 38U
                ? height / 8U : 38U;
            const uint64_t disc_x = width * 3U / 4U;
            const uint64_t disc_y = height / 3U;
            const uint64_t disc_size = height / 2U;
            const uint8_t glow_r = (uint8_t)((base_r * 3U + accent_r) / 4U);
            const uint8_t glow_g = (uint8_t)((base_g * 3U + accent_g) / 4U);
            const uint8_t glow_b = (uint8_t)((base_b * 3U + accent_b) / 4U);
            graphics_fill_round_rect(disc_x - disc_size / 2U,
                disc_y - disc_size / 2U, disc_size, disc_size,
                disc_size / 2U, glow_r, glow_g, glow_b);
            graphics_draw_round_rect(disc_x - disc_size / 2U - 10U,
                disc_y - disc_size / 2U - 10U, disc_size + 20U,
                disc_size + 20U, (disc_size + 20U) / 2U,
                accent_r / 2U, accent_g / 2U, accent_b / 2U);
            graphics_fill_round_rect(width / 14U, height * 3U / 5U,
                width * 2U / 5U, ribbon_h, ribbon_h / 2U,
                (uint8_t)(base_r / 2U + accent_r / 4U),
                (uint8_t)(base_g / 2U + accent_g / 4U),
                (uint8_t)(base_b / 2U + accent_b / 4U));
            graphics_fill_round_rect(width / 3U, height / 4U,
                width / 2U, ribbon_h / 2U, ribbon_h / 4U,
                (uint8_t)(base_r / 2U + accent_r / 5U),
                (uint8_t)(base_g / 2U + accent_g / 5U),
                (uint8_t)(base_b / 2U + accent_b / 5U));
            graphics_draw_round_rect(width / 14U, height * 3U / 5U,
                width * 2U / 5U, ribbon_h, ribbon_h / 2U,
                accent_r, accent_g, accent_b);
        } else if (background == 3U) {
            for (uint64_t x = 48U; x < width; x += 64U)
                graphics_fill_rect(x, 0U, 1U, height,
                                   accent_r / 7U, accent_g / 7U,
                                   accent_b / 7U);
            for (uint64_t y = 48U; y < height; y += 64U)
                graphics_fill_rect(0U, y, width, 1U,
                                   accent_r / 7U, accent_g / 7U,
                                   accent_b / 7U);
        } else if (background == 4U && width > 250U && height > 250U) {
            const uint64_t cx = width / 2U;
            const uint64_t cy = height / 2U;
            const uint64_t diameter =
                (height < width ? height : width) * 3U / 4U;
            for (size_t ring = 0U; ring < 3U; ++ring) {
                const uint64_t inset = (diameter / 8U) * ring;
                const uint64_t d = diameter - inset * 2U;
                const uint64_t radius = d / 2U;
                if (cx >= radius && cy >= radius && d != 0U) {
                    graphics_draw_round_rect(cx - radius, cy - radius,
                        d, d, radius,
                        accent_r / (uint8_t)(ring + 1U),
                        accent_g / (uint8_t)(ring + 1U),
                        accent_b / (uint8_t)(ring + 1U));
                }
            }
        }
    }

    /* Floating system bar: keeps the wallpaper visible and clarifies the
     * home, workspace and live-status hierarchy. */
    graphics_fill_round_rect(10U, 4U, width - 20U, top_bar - 8U,
                             11U, 11U, 20U, 36U);
    graphics_draw_round_rect(10U, 4U, width - 20U, top_bar - 8U,
                             11U, 62U, 91U, 125U);
    graphics_fill_round_rect(18U, 6U, width - 36U, 2U, 1U,
                             accent_r, accent_g, accent_b);
    desktop_draw_logo(18U, (top_bar - 22U) / 2U, 22U);
    graphics_draw_text(54U, top_bar / 2U - 4U,
                       "NEXUS OS", 1U, 245U, 249U, 255U);
    desktop_draw_workspace_switcher(top_bar);
    char top_uptime[21];
    format_u64(timer_ticks() / 100U, top_uptime);
    const uint64_t status_x = width > 270U ? width - 176U : 8U;
    graphics_fill_round_rect(status_x, 7U, width > 270U ? 158U : 90U,
                             top_bar - 14U, 8U, 19U, 42U, 57U);
    graphics_draw_text(status_x + 10U, top_bar / 2U - 4U,
                       "UP", 1U, 111U, 207U, 231U);
    graphics_draw_text(status_x + 34U, top_bar / 2U - 4U,
                       top_uptime, 1U, 242U, 247U, 252U);
    if (width > 270U) {
        const uint32_t adapter_count = netdev_device_count();
        graphics_fill_rect(status_x + 103U, 10U, 1U,
                           top_bar - 20U, 65U, 89U, 111U);
        graphics_draw_text(status_x + 111U, top_bar / 2U - 4U,
                           adapter_count ? "NET" : "OFF", 1U,
                           adapter_count ? 112U : 238U,
                           adapter_count ? 222U : 177U,
                           adapter_count ? 190U : 119U);
    }


    bool managed_windows_drawn = false;
    if (!installer_environment && desktop_window_manager_ready)
        managed_windows_drawn = desktop_draw_managed_windows(top_bar);

    if (!managed_windows_drawn) {
        window_maximized = false;
        if (installer_environment) {
            active_view = DESKTOP_INSTALLER;
            uint64_t panel_x = 0U, panel_y = 0U;
            uint64_t panel_width = 0U, panel_height = 0U;
            desktop_get_current_window_rect(&panel_x, &panel_y,
                                            &panel_width, &panel_height);
            desktop_draw_window_surface(DESKTOP_INSTALLER,
                                        panel_x, panel_y,
                                        panel_width, panel_height,
                                        false, true);
        } else {
            active_view = DESKTOP_HOME;
            desktop_draw_desktop_icons(top_bar, bottom_bar);
        }
    }

    if (!installer_environment) {
        desktop_draw_taskbar(bottom_bar);
        desktop_draw_start_menu(bottom_bar);
        desktop_draw_workspace_overview();
    }
}

bool desktop_show(const struct limine_framebuffer *framebuffer) {
    desktop_ready = false;
    if (framebuffer == NULL || framebuffer->address == NULL ||
        framebuffer->memory_model != LIMINE_FRAMEBUFFER_RGB) {
        serial_write("DESKTOP: no compatible RGB framebuffer; keeping serial console\r\n");
        return false;
    }

    const struct nexus_graphics_surface surface = {
        .address = framebuffer->address,
        .width = framebuffer->width,
        .height = framebuffer->height,
        .pitch = framebuffer->pitch,
        .bits_per_pixel = framebuffer->bpp,
        .red_mask_size = framebuffer->red_mask_size,
        .red_mask_shift = framebuffer->red_mask_shift,
        .green_mask_size = framebuffer->green_mask_size,
        .green_mask_shift = framebuffer->green_mask_shift,
        .blue_mask_size = framebuffer->blue_mask_size,
        .blue_mask_shift = framebuffer->blue_mask_shift
    };
    if (!graphics_init(&surface)) {
        serial_write("DESKTOP: framebuffer validation failed; keeping serial console\r\n");
        return false;
    }

    screen_width = graphics_width();
    screen_height = graphics_height();
    desktop_window_manager_ready = false;
    desktop_window_drag_mode = DESKTOP_WINDOW_DRAG_NONE;
    desktop_dragged_window = 0U;
    for (size_t app = 0U; app < 7U; ++app) desktop_app_windows[app] = 0U;
#if defined(NEXUS_INSTALLED_OS_BUILD)
    /* The installed OS image never enters the live installer, even if the
     * firmware reports that it was started from USB/removable storage. */
    installer_environment = false;
#elif defined(NEXUS_INSTALLER_BUILD)
    installer_environment = blockdev_boot_source_is_external();
#else
    installer_environment = blockdev_boot_source_is_external();
#endif
    workspace_overview_open = false;
    workspace_overview_selected = 0U;
    if (!installer_environment && screen_width <= 0x7FFFFFFFU &&
        screen_height <= 0x7FFFFFFFU) {
        uint32_t work_height = 0U;
        desktop_window_work_area(NULL, NULL, &work_height);
        desktop_window_manager_ready = nexus_wm_init(
            (uint32_t)screen_width, work_height);
        if (desktop_window_manager_ready)
            serial_write("DESKTOP: multi-window workspace ready\r\n");
    }
    active_view = installer_environment ? DESKTOP_INSTALLER : DESKTOP_HOME;
    installer_ui_state = INSTALLER_UI_SELECT;
    installer_firmware = NEXUS_FIRMWARE_UEFI;
    installer_selected_device = 0U;
    installer_refresh_plan();
    if (installer_environment) {
        /* The live-media installer is already the active view on boot.
         * Emit the initially selected disk identity immediately so serial
         * diagnostics and automated install validation observe the same
         * target shown by the graphical selector before any user input. */
        installer_log_selected_target();
    }
    mouse_left_was_down = false;
    files_last_click_valid = false;
    keyboard_left_shift = false;
    keyboard_right_shift = false;
    keyboard_alt = false;
    keyboard_ctrl = false;
    keyboard_caps_lock = false;
    keyboard_caps_lock_down = false;
    pointer_under_valid = false;
    start_menu_open = false;
    workspace_overview_open = false;
    workspace_overview_selected = 0U;
    desktop_launcher_reset();
    window_maximized = false;
    window_animation_offset = 0U;
    settings_persisted = false;
    if (!installer_environment) desktop_load_settings();
    calculator_clear();
    desktop_draw_startup_animation();
    pointer_x = (int64_t)(screen_width / 2U);
    pointer_y = (int64_t)(screen_height / 2U);
    desktop_ready = true;
#ifdef NEXUS_INSTALLER_ENABLE_DESTRUCTIVE_WRITES
    serial_write("NEXUS INSTALLER WRITE GATE ENABLED\r\n");
#else
    serial_write("NEXUS INSTALLER WRITE GATE LOCKED\r\n");
#endif
    draw_shell();
    draw_pointer();
    serial_write("DESKTOP: interactive framebuffer shell rendered\r\n");
    serial_write("NEXUS DESKTOP FRAMEBUFFER READY\r\n");
    return true;
}

static void desktop_reboot_after_install(void) {
    /*
     * The live installer and installed desktop are separate boot images.
     * Once the user confirms the reboot, reset the machine so firmware can
     * load the Nexus System partition. Try the common chipset reset port and
     * legacy keyboard-controller reset, then force a triple-fault reset as a
     * final x86 fallback for machines without a usable 8042 controller.
     */
    serial_write("NEXUS INSTALLER: restart requested; booting installed Nexus OS after live-media removal\r\n");
    __asm__ volatile ("cli" : : : "memory");

    const uint8_t chipset_reset_prepare = 0x02U;
    const uint8_t chipset_reset = 0x06U;
    const uint8_t keyboard_reset = 0xFEU;
    __asm__ volatile ("outb %0, %1" :
                      : "a"(chipset_reset_prepare), "Nd"((uint16_t)0xCF9U));
    __asm__ volatile ("outb %0, %1" :
                      : "a"(chipset_reset), "Nd"((uint16_t)0xCF9U));
    __asm__ volatile ("outb %0, %1" :
                      : "a"(keyboard_reset), "Nd"((uint16_t)0x64U));

    /* With a null IDT, the deliberate exception delivery escalates to a
     * triple fault, which resets x86 platforms if the I/O reset methods did
     * not already take effect. */
    struct __attribute__((packed)) {
        uint16_t limit;
        uint64_t base;
    } invalid_idt = {0U, 0U};
    __asm__ volatile ("lidt %0; int3" : : "m"(invalid_idt) : "memory");
    for (;;) __asm__ volatile ("hlt" ::: "memory");
}

static void erase_pointer(void) {
    if (!pointer_under_valid) return;
    const uint64_t x = (uint64_t)pointer_x;
    const uint64_t y = (uint64_t)pointer_y;
    for (uint64_t row = 0U; row < 15U; ++row)
        for (uint64_t column = 0U; column < 7U; ++column)
            (void)graphics_write_pixel(x + column, y + row,
                                       pointer_under[row][column]);
    pointer_under_valid = false;
}

static void desktop_animate_view(void) {
    if (settings_reduce_motion) {
        window_animation_offset = 0U;
        draw_shell();
        draw_pointer();
        return;
    }
    /* A short upward settle animates the app window without blocking input
     * for long or depending on a compositor that Nexus does not yet have. */
    if (screen_width < 240U || screen_height < 220U) {
        window_animation_offset = 0U;
        draw_shell();
        draw_pointer();
        return;
    }
    for (uint64_t frame = 0U; frame < 5U; ++frame) {
        window_animation_offset = (4U - frame) * 6U;
        draw_shell();
        const uint64_t deadline = timer_ticks() + 1U;
        __asm__ volatile ("sti" : : : "memory");
        while (timer_ticks() < deadline)
            __asm__ volatile ("hlt" : : : "memory");
        __asm__ volatile ("cli" : : : "memory");
    }
    window_animation_offset = 0U;
    draw_shell();
    draw_pointer();
}

static void draw_pointer(void) {
    const uint64_t x = (uint64_t)pointer_x;
    const uint64_t y = (uint64_t)pointer_y;
    for (uint64_t row = 0U; row < 15U; ++row)
        for (uint64_t column = 0U; column < 7U; ++column) {
            pointer_under[row][column] = 0U;
            (void)graphics_read_pixel(x + column, y + row,
                                      &pointer_under[row][column]);
        }
    pointer_under_valid = true;
    graphics_fill_rect(x, y, 2U, 15U, 245, 250, 255);
    graphics_fill_rect(x + 1U, y + 1U, 2U, 11U, 245, 250, 255);
    graphics_fill_rect(x + 2U, y + 5U, 5U, 2U, 245, 250, 255);
}

static char files_ascii_lower(char value) {
    return value >= 'A' && value <= 'Z'
        ? (char)(value + ('a' - 'A')) : value;
}

static bool files_listing_entry_before(size_t left, size_t right) {
    const bool left_directory =
        files_listing_types[left] == NEXUS_VFS_DIRECTORY;
    const bool right_directory =
        files_listing_types[right] == NEXUS_VFS_DIRECTORY;
    if (left_directory != right_directory) return left_directory;
    for (size_t i = 0U; i < NEXUS_VFS_NAME_MAX; ++i) {
        const char a = files_ascii_lower(files_listing[left][i]);
        const char b = files_ascii_lower(files_listing[right][i]);
        if (a != b) return a < b;
        if (a == '\0') return false;
    }
    return false;
}

static void files_sort_listing(void) {
    for (size_t i = 1U; i < files_listing_count; ++i) {
        char name[NEXUS_VFS_NAME_MAX];
        for (size_t n = 0U; n < NEXUS_VFS_NAME_MAX; ++n)
            name[n] = files_listing[i][n];
        const uint8_t type = files_listing_types[i];
        size_t at = i;
        while (at > 0U && files_listing_entry_before(i, at - 1U)) {
            for (size_t n = 0U; n < NEXUS_VFS_NAME_MAX; ++n)
                files_listing[at][n] = files_listing[at - 1U][n];
            files_listing_types[at] = files_listing_types[at - 1U];
            --at;
            /* The insertion key is kept separately while the array shifts. */
            for (size_t n = 0U; n < NEXUS_VFS_NAME_MAX; ++n)
                files_listing[i][n] = name[n];
            files_listing_types[i] = type;
        }
        for (size_t n = 0U; n < NEXUS_VFS_NAME_MAX; ++n)
            files_listing[at][n] = name[n];
        files_listing_types[at] = type;
    }
}

static bool files_name_matches_query(const char *name) {
    if (name == NULL) return false;
    size_t query_length = 0U;
    while (query_length < sizeof(files_search_query) &&
           files_search_query[query_length] != '\0') ++query_length;
    if (query_length == 0U) return true;
    for (size_t start = 0U; name[start] != '\0'; ++start) {
        size_t matched = 0U;
        while (matched < query_length && name[start + matched] != '\0' &&
               files_ascii_lower(name[start + matched]) ==
                   files_ascii_lower(files_search_query[matched])) ++matched;
        if (matched == query_length) return true;
    }
    return false;
}

static void refresh_files_listing(void) {
    files_last_click_valid = false;
    files_vfs_ready = vfs_has_filesystem();
    files_listing_count = 0U;
    files_selected_index = 0U;
    files_listing_first_index = 0U;
    files_listing_truncated = false;
    for (size_t n = 0; n < sizeof(files_first_name); ++n)
        files_first_name[n] = '\0';
    for (size_t n = 0; n < sizeof(files_preview); ++n)
        files_preview[n] = '\0';
    for (size_t n = 0U; n < FILES_LISTING_MAX_ENTRIES; ++n) {
        for (size_t k = 0U; k < sizeof(files_listing[n]); ++k)
            files_listing[n][k] = '\0';
    }
    if (!files_vfs_ready) return;

    for (size_t index = 0; index < vfs_root_entry_count(); ++index) {
        struct nexus_vfs_dirent entry;
        if (!vfs_root_entry(index, &entry)) continue;
        serial_write("FILES: entry ");
        serial_write(entry.name);
        char entry_size_text[21];
        format_u64(entry.size, entry_size_text);
        serial_write(" size=");
        serial_write(entry_size_text);
        serial_write("\r\n");
        if (files_listing_count < FILES_LISTING_MAX_ENTRIES) {
            size_t length = 0U;
            while (length < NEXUS_VFS_NAME_MAX - 1U &&
                   entry.name[length] != '\0') {
                files_listing[files_listing_count][length] = entry.name[length];
                ++length;
            }
            files_listing[files_listing_count][length] = '\0';
            files_listing_types[files_listing_count] = (uint8_t)entry.type;
            ++files_listing_count;
        } else {
            files_listing_truncated = true;
            break;
        }
        if (files_first_name[0] == '\0') {
            size_t length = 0U;
            while (length < sizeof(files_first_name) - 1U &&
                   entry.name[length] != '\0') {
                files_first_name[length] = entry.name[length];
                ++length;
            }
            files_first_name[length] = '\0';
        }
        if (entry.type != NEXUS_VFS_FILE || entry.size == 0U ||
            files_preview[0] != '\0') continue;

        struct nexus_vfs_file file;
        if (!vfs_open_readonly(entry.name, &file)) continue;
        uint8_t preview_data[sizeof(files_preview) - 1U];
        size_t amount = entry.size;
        if (amount > sizeof(preview_data)) amount = sizeof(preview_data);
        size_t bytes_read = 0U;
        const bool read_ok =
            vfs_read(&file, preview_data, amount, &bytes_read);
        vfs_close(&file);
        if (!read_ok) continue;
        for (size_t n = 0; n < bytes_read; ++n) {
            const uint8_t ch = preview_data[n];
            files_preview[n] =
                (ch >= 0x20U && ch <= 0x7EU) ? (char)ch : ' ';
        }
        files_preview[bytes_read] = '\0';
    }

    files_sort_listing();
    if (files_search_query[0] != '\0') {
        size_t kept = 0U;
        for (size_t index = 0U; index < files_listing_count; ++index) {
            if (!files_name_matches_query(files_listing[index])) continue;
            if (kept != index) {
                for (size_t n = 0U; n < NEXUS_VFS_NAME_MAX; ++n)
                    files_listing[kept][n] = files_listing[index][n];
                files_listing_types[kept] = files_listing_types[index];
            }
            ++kept;
        }
        files_listing_count = kept;
    }
    files_first_name[0] = '\0';
    files_preview[0] = '\0';
    for (size_t index = 0U; index < files_listing_count; ++index) {
        if (files_listing_types[index] != NEXUS_VFS_FILE) continue;
        size_t name_length = 0U;
        while (name_length < sizeof(files_first_name) - 1U &&
               files_listing[index][name_length] != '\0') {
            files_first_name[name_length] = files_listing[index][name_length];
            ++name_length;
        }
        files_first_name[name_length] = '\0';

        struct nexus_vfs_file preview_file;
        if (vfs_open_readonly(files_listing[index], &preview_file)) {
            uint8_t preview_data[sizeof(files_preview) - 1U];
            size_t amount = 0U;
            if (vfs_read(&preview_file, preview_data, sizeof(preview_data),
                         &amount)) {
                for (size_t n = 0U; n < amount; ++n) {
                    const uint8_t ch = preview_data[n];
                    files_preview[n] =
                        (ch >= 0x20U && ch <= 0x7EU) ? (char)ch : ' ';
                }
                files_preview[amount] = '\0';
            }
            vfs_close(&preview_file);
        }
        break;
    }
}

static bool files_selected_entry(struct nexus_vfs_dirent *selected) {
    if (selected == NULL || files_selected_index >= files_listing_count)
        return false;
    const char *selected_name = files_listing[files_selected_index];
    for (size_t i = 0U; i < vfs_root_entry_count(); ++i) {
        struct nexus_vfs_dirent candidate;
        if (!vfs_root_entry(i, &candidate)) continue;
        size_t n = 0U;
        bool equal = true;
        while (n < NEXUS_VFS_NAME_MAX) {
            if (files_ascii_lower(candidate.name[n]) !=
                files_ascii_lower(selected_name[n])) {
                equal = false;
                break;
            }
            if (candidate.name[n] == '\0') break;
            ++n;
        }
        if (equal) {
            *selected = candidate;
            return true;
        }
    }
    return false;
}

static size_t files_editor_max_view_offset(void) {
    return files_edit_content_length > FILES_EDITOR_VIEW_BYTES
        ? files_edit_content_length - FILES_EDITOR_VIEW_BYTES : 0U;
}

static void files_editor_scroll_to_end(void) {
    files_edit_view_offset = files_editor_max_view_offset();
    files_edit_follow_end = true;
}

static void files_listing_ensure_selection_visible(void) {
    if (files_listing_count == 0U) {
        files_listing_first_index = 0U;
        files_selected_index = 0U;
        return;
    }
    if (files_selected_index >= files_listing_count)
        files_selected_index = files_listing_count - 1U;
    if (files_selected_index < files_listing_first_index) {
        files_listing_first_index = files_selected_index;
    } else if (files_selected_index >=
               files_listing_first_index + FILES_LISTING_VISIBLE) {
        files_listing_first_index =
            files_selected_index - FILES_LISTING_VISIBLE + 1U;
    }
    const size_t maximum_first = files_listing_count > FILES_LISTING_VISIBLE
        ? files_listing_count - FILES_LISTING_VISIBLE : 0U;
    if (files_listing_first_index > maximum_first)
        files_listing_first_index = maximum_first;
}

static void files_listing_page(int direction) {
    if (files_listing_count == 0U) return;
    const size_t maximum_first = files_listing_count > FILES_LISTING_VISIBLE
        ? files_listing_count - FILES_LISTING_VISIBLE : 0U;
    if (direction < 0) {
        files_listing_first_index =
            files_listing_first_index > FILES_LISTING_VISIBLE
                ? files_listing_first_index - FILES_LISTING_VISIBLE : 0U;
    } else {
        files_listing_first_index += FILES_LISTING_VISIBLE;
        if (files_listing_first_index > maximum_first)
            files_listing_first_index = maximum_first;
    }
    files_selected_index = files_listing_first_index;
}

static void files_editor_begin_new(void) {
    files_edit_name[0] = '\0';
    files_edit_content[0] = '\0';
    files_edit_name_length = 0U;
    files_edit_content_length = 0U;
    files_edit_view_offset = 0U;
    files_edit_follow_end = true;
    files_edit_status[0] = '\0';
    files_editor_state = FILES_EDITOR_NAME;
}

static void files_editor_set_status(const char *message) {
    size_t i = 0U;
    while (message[i] != '\0' && i < sizeof(files_edit_status) - 1U) {
        files_edit_status[i] = message[i];
        ++i;
    }
    files_edit_status[i] = '\0';
}

static bool files_names_equal(const char *left, const char *right) {
    if (left == NULL || right == NULL) return false;
    for (size_t i = 0U; i < NEXUS_VFS_NAME_MAX; ++i) {
        char a = left[i], b = right[i];
        if (a >= 'a' && a <= 'z') a = (char)(a - 'a' + 'A');
        if (b >= 'a' && b <= 'z') b = (char)(b - 'a' + 'A');
        if (a != b) return false;
        if (a == '\0') return true;
    }
    return false;
}

static void files_editor_begin_rename(void) {
    struct nexus_vfs_dirent selected;
    if (files_selected_index >= files_listing_count ||
        !files_selected_entry(&selected) ||
        selected.type != NEXUS_VFS_FILE) {
        files_editor_set_status("SELECT A FILE TO RENAME");
        return;
    }
    size_t length = 0U;
    while (length < NEXUS_VFS_NAME_MAX - 1U &&
           selected.name[length] != '\0') {
        files_rename_source[length] = selected.name[length];
        files_edit_name[length] = selected.name[length];
        ++length;
    }
    files_rename_source[length] = '\0';
    files_edit_name[length] = '\0';
    files_edit_name_length = length;
    files_edit_status[0] = '\0';
    files_editor_state = FILES_EDITOR_RENAME;
}

static void files_editor_begin_copy(void) {
    struct nexus_vfs_dirent selected;
    if (files_selected_index >= files_listing_count ||
        !files_selected_entry(&selected) ||
        selected.type != NEXUS_VFS_FILE) {
        files_editor_set_status("SELECT A FILE TO COPY");
        return;
    }
    size_t length = 0U;
    while (length < NEXUS_VFS_NAME_MAX - 1U &&
           selected.name[length] != '\0') {
        files_copy_source[length] = selected.name[length];
        ++length;
    }
    files_copy_source[length] = '\0';
    files_edit_name[0] = '\0';
    files_edit_name_length = 0U;
    files_edit_status[0] = '\0';
    files_editor_state = FILES_EDITOR_COPY;
}

static void files_editor_begin_selected(void) {
    struct nexus_vfs_dirent selected;
    if (files_selected_index >= files_listing_count ||
        !files_selected_entry(&selected)) {
        files_editor_set_status("SELECT A FILE");
        return;
    }
    if (selected.type != NEXUS_VFS_FILE) {
        files_editor_set_status("SELECTED ITEM IS NOT A FILE");
        return;
    }
    if (selected.size > FILES_EDITOR_MAX_BYTES) {
        files_editor_set_status("FILE EXCEEDS 1MB LIMIT - UNCHANGED");
        return;
    }
    struct nexus_vfs_file file;
    if (!vfs_open_readonly(selected.name, &file)) {
        files_editor_set_status("FILE OPEN FAILED");
        return;
    }
    size_t bytes_read = 0U;
    bool read_ok = true;
    while (bytes_read < selected.size) {
        size_t chunk = (size_t)selected.size - bytes_read;
        if (chunk > NEXUS_VFS_READ_CHUNK) chunk = NEXUS_VFS_READ_CHUNK;
        size_t received = 0U;
        if (!vfs_read(&file, files_edit_content + bytes_read,
                      chunk, &received) || received == 0U) {
            read_ok = false;
            break;
        }
        bytes_read += received;
    }
    vfs_close(&file);
    if (!read_ok || bytes_read != selected.size) {
        files_edit_status[0] = '\0';
        const char *message = "COULD NOT READ COMPLETE FILE";
        size_t i = 0U;
        while (message[i] != '\0' && i < sizeof(files_edit_status) - 1U) {
            files_edit_status[i] = message[i];
            ++i;
        }
        files_edit_status[i] = '\0';
        return;
    }
    for (size_t i = 0U; i < sizeof(files_edit_name); ++i)
        files_edit_name[i] = selected.name[i];
    files_edit_name_length = 0U;
    while (files_edit_name_length < sizeof(files_edit_name) - 1U &&
           files_edit_name[files_edit_name_length] != '\0')
        ++files_edit_name_length;
    files_edit_content_length = bytes_read;
    files_edit_content[files_edit_content_length] = '\0';
    files_editor_scroll_to_end();
    files_edit_status[0] = '\0';
    files_editor_state = FILES_EDITOR_CONTENT;
}

static void files_request_delete_selected(void) {
    struct nexus_vfs_dirent selected;
    if (files_selected_index >= files_listing_count ||
        !files_selected_entry(&selected) ||
        selected.type != NEXUS_VFS_FILE) {
        files_delete_pending = false;
        return;
    }
    size_t i = 0U;
    while (i < sizeof(files_delete_name) - 1U && selected.name[i] != '\0') {
        files_delete_name[i] = selected.name[i];
        ++i;
    }
    files_delete_name[i] = '\0';
    files_delete_pending = true;
}

static void files_confirm_delete(void) {
    if (!files_delete_pending) return;
    if (vfs_remove_file(files_delete_name)) {
        serial_write("FILES: removed writable file ");
        serial_write(files_delete_name);
        serial_write("\r\n");
    } else {
        serial_write("FILES: delete refused (read-only or unsupported file) ");
        serial_write(files_delete_name);
        serial_write("\r\n");
    }
    files_delete_pending = false;
    files_delete_name[0] = '\0';
    refresh_files_listing();
}

static bool files_editor_name_exists(void) {
    for (size_t index = 0U; index < vfs_root_entry_count(); ++index) {
        struct nexus_vfs_dirent entry;
        if (!vfs_root_entry(index, &entry)) continue;
        size_t i = 0U;
        bool equal = true;
        while (files_edit_name[i] != '\0' || entry.name[i] != '\0') {
            char a = files_edit_name[i], b = entry.name[i];
            if (a >= 'a' && a <= 'z') a = (char)(a - 'a' + 'A');
            if (b >= 'a' && b <= 'z') b = (char)(b - 'a' + 'A');
            if (a != b) { equal = false; break; }
            ++i;
            if (i >= NEXUS_VFS_NAME_MAX) { equal = false; break; }
        }
        if (equal) return true;
    }
    return false;
}

static void files_editor_handle_key(const struct nexus_input_event *event) {
    if ((event->scancode & 0x80U) != 0U) return;
    if (event->scancode == 0x01U) {
        files_editor_state = FILES_EDITOR_CLOSED;
        files_rename_source[0] = '\0';
        return;
    }
    if (files_editor_state == FILES_EDITOR_NAME ||
        files_editor_state == FILES_EDITOR_RENAME ||
        files_editor_state == FILES_EDITOR_COPY) {
        if (event->scancode == 0x0EU) {
            if (files_edit_name_length != 0U)
                files_edit_name[--files_edit_name_length] = '\0';
        } else if (event->scancode == 0x1CU) {
            if (files_edit_name_length == 0U) {
                const char *message = "ENTER A FILE NAME";
                for (size_t i = 0U; i < sizeof(files_edit_status); ++i) {
                    files_edit_status[i] = message[i];
                    if (message[i] == '\0') break;
                }
            } else if (files_editor_state == FILES_EDITOR_RENAME) {
                if (files_editor_name_exists() &&
                    !files_names_equal(files_edit_name, files_rename_source)) {
                    files_editor_set_status("DESTINATION ALREADY EXISTS");
                } else if (files_names_equal(files_edit_name, files_rename_source)) {
                    files_editor_set_status("NAME IS UNCHANGED");
                } else if (vfs_move_file(files_rename_source, files_edit_name)) {
                    serial_write("FILES: renamed ");
                    serial_write(files_rename_source);
                    serial_write(" to ");
                    serial_write(files_edit_name);
                    serial_write("\r\n");
                    files_editor_state = FILES_EDITOR_CLOSED;
                    files_rename_source[0] = '\0';
                    refresh_files_listing();
                } else {
                    files_editor_set_status("RENAME FAILED - SOURCE PRESERVED");
                }
            } else if (files_editor_state == FILES_EDITOR_COPY) {
                if (files_editor_name_exists()) {
                    files_editor_set_status("DESTINATION ALREADY EXISTS");
                } else if (vfs_copy_file(files_copy_source, files_edit_name)) {
                    serial_write("FILES: copied ");
                    serial_write(files_copy_source);
                    serial_write(" to ");
                    serial_write(files_edit_name);
                    serial_write("\r\n");
                    files_editor_state = FILES_EDITOR_CLOSED;
                    files_copy_source[0] = '\0';
                    refresh_files_listing();
                } else {
                    files_editor_set_status("COPY FAILED - SOURCE UNCHANGED");
                }
            } else if (files_editor_name_exists()) {
                const char *message = "FILE EXISTS - PRESS E TO EDIT";
                size_t i = 0U;
                while (message[i] != '\0' &&
                       i < sizeof(files_edit_status) - 1U) {
                    files_edit_status[i] = message[i];
                    ++i;
                }
                files_edit_status[i] = '\0';
            } else {
                files_editor_state = FILES_EDITOR_CONTENT;
                files_edit_status[0] = '\0';
            }
        } else {
            const char ch = terminal_scancode_char(event->scancode);
            if (ch != '\0' &&
                files_edit_name_length < sizeof(files_edit_name) - 1U) {
                const char normalized = ch >= 'a' && ch <= 'z'
                    ? (char)(ch - 'a' + 'A') : ch;
                files_edit_name[files_edit_name_length++] = normalized;
                files_edit_name[files_edit_name_length] = '\0';
            }
        }
        return;
    }
    if (event->scancode == 0x49U) {
        files_edit_follow_end = false;
        files_edit_view_offset =
            files_edit_view_offset > FILES_EDITOR_VIEW_BYTES
                ? files_edit_view_offset - FILES_EDITOR_VIEW_BYTES : 0U;
        return;
    } else if (event->scancode == 0x51U) {
        const size_t maximum = files_editor_max_view_offset();
        if (files_edit_view_offset < maximum) {
            files_edit_view_offset += FILES_EDITOR_VIEW_BYTES;
            if (files_edit_view_offset > maximum)
                files_edit_view_offset = maximum;
        }
        files_edit_follow_end = files_edit_view_offset == maximum;
        return;
    } else if (event->scancode == 0x47U) {
        files_edit_view_offset = 0U;
        files_edit_follow_end = files_edit_view_offset ==
                               files_editor_max_view_offset();
        return;
    } else if (event->scancode == 0x4FU) {
        files_editor_scroll_to_end();
        return;
    }
    if (event->scancode == 0x44U) {
        const bool saved = vfs_write_file(files_edit_name, files_edit_content,
                                          files_edit_content_length);
        const char *message = saved
            ? (vfs_nxfs_writable() ? "SAVED TO PERSISTENT NXFS"
                                   : "SAVED TO VOLATILE RAM")
            : "SAVE FAILED - CHECK NAME, SPACE OR STORAGE";
        size_t i = 0U;
        while (message[i] != '\0' && i < sizeof(files_edit_status) - 1U) {
            files_edit_status[i] = message[i];
            ++i;
        }
        files_edit_status[i] = '\0';
        if (saved) {
            serial_write(vfs_nxfs_writable()
                ? "FILES: SAVED PERSISTENT NXFS "
                : "FILES: SAVED VOLATILE RAM ");
            serial_write(files_edit_name);
            serial_write("\r\n");
            files_editor_state = FILES_EDITOR_CLOSED;
            refresh_files_listing();
        } else {
            serial_write("FILES: SAVE FAILED ");
            serial_write(files_edit_name);
            serial_write("\r\n");
        }
    } else if (event->scancode == 0x0EU) {
        if (files_edit_content_length != 0U) {
            files_edit_content[--files_edit_content_length] = '\0';
            if (files_edit_follow_end) files_editor_scroll_to_end();
        }
    } else if (event->scancode == 0x1CU) {
        if (files_edit_content_length < FILES_EDITOR_MAX_BYTES) {
            files_edit_content[files_edit_content_length++] = '\n';
            files_edit_content[files_edit_content_length] = '\0';
            if (files_edit_follow_end) files_editor_scroll_to_end();
        } else {
            const char *message = "EDITOR LIMIT REACHED - SAVE OR REMOVE TEXT";
            size_t i = 0U;
            while (message[i] != '\0' && i < sizeof(files_edit_status) - 1U) {
                files_edit_status[i] = message[i];
                ++i;
            }
            files_edit_status[i] = '\0';
        }
    } else {
        const char ch = terminal_scancode_char(event->scancode);
        if (ch >= 0x20 && ch <= 0x7E &&
            files_edit_content_length < FILES_EDITOR_MAX_BYTES) {
            files_edit_content[files_edit_content_length++] = ch;
            files_edit_content[files_edit_content_length] = '\0';
            if (files_edit_follow_end) files_editor_scroll_to_end();
        } else if (ch >= 0x20 && ch <= 0x7E) {
            const char *message = "EDITOR LIMIT REACHED - SAVE OR REMOVE TEXT";
            size_t i = 0U;
            while (message[i] != '\0' && i < sizeof(files_edit_status) - 1U) {
                files_edit_status[i] = message[i];
                ++i;
            }
            files_edit_status[i] = '\0';
        }
    }
}

static bool files_partition_guid_matches(
    const struct nexus_gpt_partition *partition, const uint8_t guid[16]) {
    if (partition == NULL || guid == NULL) return false;
    for (size_t i = 0U; i < 16U; ++i)
        if (partition->type_guid[i] != guid[i]) return false;
    return true;
}

static bool files_is_efi_system_partition(
    const struct nexus_gpt_partition *partition) {
    static const uint8_t efi_system_guid[16] = {
        0x28U, 0x73U, 0x2AU, 0xC1U, 0x1FU, 0xF8U, 0xD2U, 0x11U,
        0xBAU, 0x4BU, 0x00U, 0xA0U, 0xC9U, 0x3EU, 0xC9U, 0x3BU
    };
    return files_partition_guid_matches(partition, efi_system_guid);
}

/* Nexus System and Data volumes share the NXFS type GUID. The Data
 * volume is the last NXFS partition in the installer-created layout, while
 * System is the preceding NXFS partition. Requiring both prevents the Files
 * app from exposing the installed kernel's System volume as user storage. */
static bool files_is_user_data_partition(
    const struct nexus_gpt_partition *partition) {
    if (!nxfs_partition_type_matches(partition)) return false;
    const struct nexus_gpt_partition *last_nxfs = NULL;
    size_t nxfs_count = 0U;
    for (size_t i = 0U; i < gpt_partition_count(); ++i) {
        const struct nexus_gpt_partition *candidate = gpt_partition_at(i);
        if (!nxfs_partition_type_matches(candidate)) continue;
        last_nxfs = candidate;
        ++nxfs_count;
    }
    return nxfs_count >= 2U && last_nxfs != NULL &&
           last_nxfs->first_lba == partition->first_lba &&
           last_nxfs->last_lba == partition->last_lba;
}

static void scan_files_once(void) {
    if (files_scanned) return;
    files_scanned = true;
    files_scan_ok = false;
    files_vfs_ready = false;
    files_partition_count = 0U;
    files_first_lba = 0U;
    files_last_lba = 0U;
    for (size_t n = 0; n < sizeof(files_first_name); ++n)
        files_first_name[n] = '\0';
    for (size_t n = 0; n < sizeof(files_preview); ++n)
        files_preview[n] = '\0';
    files_listing_count = 0U;
    files_selected_index = 0U;
    for (size_t n = 0U; n < FILES_LISTING_MAX_ENTRIES; ++n)
        for (size_t k = 0U; k < sizeof(files_listing[n]); ++k)
            files_listing[n][k] = '\0';

    for (size_t i = 0U; i < blockdev_count(); ++i) {
        const struct nexus_block_device *device = blockdev_at(i);
        if (device == NULL || !gpt_scan_device(device)) continue;
        const size_t count = gpt_partition_count();
        bool mounted_on_device = false;
        for (size_t p = 0U; p < count; ++p) {
            const struct nexus_gpt_partition *part = gpt_partition_at(p);
            if (part == NULL) continue;
            bool mounted = false;
            if (files_is_user_data_partition(part)) {
                mounted = vfs_mount_root_nxfs(device, part);
                if (mounted) {
                    serial_write("FILES: mounted persistent Nexus user Data volume on ");
                    serial_write(device->name);
                    serial_write("\r\n");
                }
            } else if (!nxfs_partition_type_matches(part) &&
                       !files_is_efi_system_partition(part) &&
                       vfs_mount_root_fat32(device, part)) {
                mounted = true;
                if (files_first_lba == 0U) {
                    files_first_lba = part->first_lba;
                    files_last_lba = part->last_lba;
                }
                serial_write("FILES: mounted FAT32 volume on ");
                serial_write(device->name);
                serial_write("\r\n");
            }
            if (mounted) {
                files_scan_ok = true;
                files_vfs_ready = true;
                files_partition_count = count;
                mounted_on_device = true;
            }
        }
        if (mounted_on_device) break;
    }

    refresh_files_listing();
    if (files_scan_ok) {
        serial_write("FILES: unified VFS volumes ready\r\n");
    } else if (files_vfs_ready) {
        serial_write("FILES: using volatile RAM filesystem\r\n");
    } else {
        serial_write("FILES: no mounted filesystem or RAM files found\r\n");
    }
}


static char terminal_scancode_char(uint8_t code) {
    static const char normal_map[58] = {
        [0x02]='1',[0x03]='2',[0x04]='3',[0x05]='4',[0x06]='5',
        [0x07]='6',[0x08]='7',[0x09]='8',[0x0A]='9',[0x0B]='0',
        [0x0C]='-',[0x0D]='=',[0x10]='q',[0x11]='w',[0x12]='e',
        [0x13]='r',[0x14]='t',[0x15]='y',[0x16]='u',[0x17]='i',
        [0x18]='o',[0x19]='p',[0x1A]='[',[0x1B]=']',[0x1E]='a',
        [0x1F]='s',[0x20]='d',[0x21]='f',[0x22]='g',[0x23]='h',
        [0x24]='j',[0x25]='k',[0x26]='l',[0x27]=';',[0x28]='\'',
        [0x29]=0x60,[0x2B]='\\',[0x2C]='z',[0x2D]='x',[0x2E]='c',
        [0x2F]='v',[0x30]='b',[0x31]='n',[0x32]='m',[0x33]=',',
        [0x34]='.',[0x35]='/',[0x39]=' '
    };
    static const char shifted_map[58] = {
        [0x02]='!',[0x03]='@',[0x04]='#',[0x05]='$',[0x06]='%',
        [0x07]='^',[0x08]='&',[0x09]='*',[0x0A]='(',[0x0B]=')',
        [0x0C]='_',[0x0D]='+',[0x1A]='{',[0x1B]='}',[0x27]=':',
        [0x28]='"',[0x29]='~',[0x2B]='|',[0x33]='<',[0x34]='>',
        [0x35]='?',[0x39]=' '
    };
    if (code >= sizeof(normal_map)) return '\0';

    const bool shifted = keyboard_left_shift || keyboard_right_shift;
    char value = normal_map[code];
    if (value >= 'a' && value <= 'z') {
        if (keyboard_caps_lock != shifted)
            value = (char)(value - 'a' + 'A');
    } else if (shifted) {
        value = shifted_map[code];
    }
    return value;
}

static void browser_set_status(const char *message) {
    size_t i = 0U;
    if (message == NULL) message = "";
    while (message[i] != '\0' && i < sizeof(browser_status) - 1U) {
        browser_status[i] = message[i];
        ++i;
    }
    browser_status[i] = '\0';
}

static void browser_clear_page(void) {
    browser_has_page = false;
    browser_line_count = 1U;
    browser_scroll_offset = 0U;
    for (size_t row = 0; row < NEXUS_BROWSER_LINE_COUNT; ++row) {
        for (size_t column = 0; column < NEXUS_BROWSER_LINE_WIDTH; ++column)
            browser_lines[row][column] = '\0';
    }
}

static void browser_append_page_char(char value) {
    if (browser_line_count == 0U) browser_line_count = 1U;
    if (value == '\r') return;
    if (value == '\n') {
        if (browser_line_count < NEXUS_BROWSER_LINE_COUNT)
            ++browser_line_count;
        return;
    }
    if (value == '\t') value = ' ';
    if ((unsigned char)value < 0x20U || (unsigned char)value > 0x7EU)
        value = ' ';
    char *line = browser_lines[browser_line_count - 1U];
    size_t length = 0U;
    while (length < NEXUS_BROWSER_LINE_WIDTH - 1U && line[length] != '\0')
        ++length;
    if (value == ' ' && (length == 0U || line[length - 1U] == ' '))
        return;
    if (length >= NEXUS_BROWSER_LINE_WIDTH - 1U) {
        if (browser_line_count >= NEXUS_BROWSER_LINE_COUNT) return;
        ++browser_line_count;
        line = browser_lines[browser_line_count - 1U];
        length = 0U;
    }
    line[length] = value;
    line[length + 1U] = '\0';
}

static void browser_render_body(const uint8_t *data, size_t length) {
    browser_clear_page();
    browser_has_page = true;
    if (data == NULL) return;

    size_t body = 0U;
    for (size_t i = 0U; i + 3U < length; ++i) {
        if (data[i] == '\r' && data[i + 1U] == '\n' &&
            data[i + 2U] == '\r' && data[i + 3U] == '\n') {
            body = i + 4U;
            break;
        }
    }

    char text[NEXUS_BROWSER_LINE_COUNT * NEXUS_BROWSER_LINE_WIDTH];
    (void)nexus_browser_html_to_text(data + body, length - body,
                                     text, sizeof(text));
    for (size_t i = 0U; text[i] != '\0'; ++i)
        browser_append_page_char(text[i]);
}

static bool browser_append_text(char *buffer, size_t capacity,
                                size_t *length, const char *text) {
    if (buffer == NULL || length == NULL || text == NULL) return false;
    while (*text != '\0') {
        if (*length >= capacity) return false;
        buffer[(*length)++] = *text++;
    }
    return true;
}

static bool browser_address_is_explicit_url(const char *value,
                                             size_t length) {
    return (length >= 7U && value[0] == 'h' && value[1] == 't' &&
            value[2] == 't' && value[3] == 'p' && value[4] == ':' &&
            value[5] == '/' && value[6] == '/') ||
           (length >= 8U && value[0] == 'h' && value[1] == 't' &&
            value[2] == 't' && value[3] == 'p' && value[4] == 's' &&
            value[5] == ':' && value[6] == '/' && value[7] == '/');
}

static bool browser_input_looks_like_host(const char *value, size_t length) {
    bool has_dot = false;
    if (length == 0U) return false;
    for (size_t i = 0U; i < length; ++i) {
        if (value[i] == ' ' || value[i] == '\t') return false;
        if (value[i] == '.') has_dot = true;
    }
    return has_dot || (length >= 9U &&
        value[0] == 'l' && value[1] == 'o' && value[2] == 'c' &&
        value[3] == 'a' && value[4] == 'l' && value[5] == 'h' &&
        value[6] == 'o' && value[7] == 's' && value[8] == 't');
}

static void browser_show_notice(const char *line_one, const char *line_two) {
    browser_clear_page();
    browser_source_count = 0U;
    browser_has_page = true;
    if (line_one != NULL) {
        for (size_t i = 0U; line_one[i] != '\0'; ++i)
            browser_append_page_char(line_one[i]);
    }
    browser_append_page_char('\n');
    if (line_two != NULL) {
        for (size_t i = 0U; line_two[i] != '\0'; ++i)
            browser_append_page_char(line_two[i]);
    }
}

static void browser_navigate(void) {
    browser_clear_page();
    browser_search_mode = false;
    browser_download_pending = false;
    browser_current_url[0] = '\0';
    browser_source_count = 0U;
    if (browser_address_length == 0U) {
        browser_set_status("Enter a search query or web address");
        return;
    }

    /* Plain text is a web search. Dotted input remains convenient for
     * direct HTTP host navigation; explicit schemes are always respected. */
    char input[NEXUS_BROWSER_SEARCH_URL_MAX];
    size_t input_length = 0U;
    if (!browser_address_is_explicit_url(browser_address,
                                          browser_address_length) &&
        !browser_input_looks_like_host(browser_address,
                                        browser_address_length)) {
        browser_search_mode = true;
        if (!nexus_browser_search_url(browser_address, input, sizeof(input))) {
            browser_set_status("Search query is empty, invalid or too long");
            return;
        }
        while (input[input_length] != '\0') ++input_length;
    }
    if (!browser_search_mode) {
        if (!browser_address_is_explicit_url(browser_address,
                                              browser_address_length)) {
            static const char prefix[] = "http://";
            for (size_t i = 0U; i < sizeof(prefix) - 1U; ++i)
                input[input_length++] = prefix[i];
        }
        if (input_length + browser_address_length >= sizeof(input)) {
            browser_set_status("Address is too long");
            return;
        }
        for (size_t i = 0U; i < browser_address_length; ++i)
            input[input_length++] = browser_address[i];
        input[input_length] = '\0';
    }

    struct nexus_browser_url url;
    if (!nexus_browser_url_parse(input, &url)) {
        browser_set_status("Invalid URL; use http://host/path");
        return;
    }
    if (url.scheme == NEXUS_BROWSER_SCHEME_HTTPS) {
        browser_set_status(browser_search_mode
            ? "Search needs HTTPS; TLS and certificate validation are missing"
            : "HTTPS blocked until TLS and certificate validation are ready");
        browser_show_notice(
            browser_search_mode ? "YOUR SEARCH URL IS READY" :
                                  "THIS WEBSITE REQUIRES HTTPS",
            "NEXUS DOES NOT YET HAVE A VERIFIED TLS CLIENT");
        return;
    }

    browser_set_status(browser_search_mode
        ? "Looking up search provider..." : "Resolving website host...");
    uint8_t address[4];
    if (!net_dns_resolve(url.host, address, 600U)) {
        browser_set_status("DNS failed; check DHCP/network");
        return;
    }
    browser_set_status("Opening TCP connection...");
    if (!net_tcp_connect(address, url.port, 800U)) {
        browser_set_status("TCP connection failed");
        return;
    }

    char request[384];
    size_t request_length = 0U;
    bool request_ok =
        browser_append_text(request, sizeof(request), &request_length,
                            "GET ") &&
        browser_append_text(request, sizeof(request), &request_length,
                            url.path) &&
        browser_append_text(request, sizeof(request), &request_length,
                            " HTTP/1.0\r\nHost: ") &&
        browser_append_text(request, sizeof(request), &request_length,
                            url.host);
    if (request_ok && url.explicit_port) {
        char port_text[21];
        format_u64(url.port, port_text);
        request_ok = browser_append_text(request, sizeof(request),
                                         &request_length, ":") &&
                     browser_append_text(request, sizeof(request),
                                         &request_length, port_text);
    }
    request_ok = request_ok &&
        browser_append_text(request, sizeof(request), &request_length,
                            "\r\nConnection: close\r\nUser-Agent: Nexus/0.1\r\n\r\n");
    if (!request_ok || !net_tcp_send(request, (uint16_t)request_length)) {
        browser_set_status("HTTP request could not be sent");
        net_tcp_close();
        return;
    }

    for (size_t i = 0U; i < sizeof(browser_response); ++i)
        browser_response[i] = 0U;
    size_t response_length = 0U;
    const uint64_t start = timer_ticks();
    uint64_t last_data = start;
    browser_set_status("Receiving plain HTTP response...");
    while (timer_ticks() - start < 600U &&
           (response_length == 0U || timer_ticks() - last_data < 40U) &&
           response_length < sizeof(browser_response)) {
        net_poll();
        const size_t received =
            net_tcp_receive(browser_response + response_length,
                            sizeof(browser_response) - response_length);
        if (received != 0U) {
            response_length += received;
            last_data = timer_ticks();
        }
        __asm__ volatile ("pause");
    }
    net_tcp_close();

    if (response_length == 0U) {
        browser_set_status("No HTTP response received");
        browser_show_notice("NO RESPONSE FROM WEBSITE",
                            "CHECK NETWORK CONNECTION AND TRY AGAIN");
        return;
    }

    struct nexus_browser_http_response parsed;
    if (!nexus_browser_http_parse(browser_response, response_length, &parsed)) {
        browser_set_status("Malformed or incomplete HTTP response");
        browser_show_notice("WEBSITE SENT AN INVALID RESPONSE",
                            "NEXUS REJECTED THE RESPONSE HEADERS");
        return;
    }
    if (parsed.status_code >= 300U && parsed.status_code < 400U) {
        if (parsed.location[0] != '\0' &&
            parsed.location[0] == 'h' && parsed.location[1] == 't' &&
            parsed.location[2] == 't' && parsed.location[3] == 'p' &&
            parsed.location[4] == 's') {
            browser_set_status("Website redirected to HTTPS; TLS is unavailable");
            browser_show_notice("HTTPS REDIRECT TARGET", parsed.location);
        } else {
            browser_set_status("Website redirected; open the target manually");
            browser_show_notice("REDIRECT TARGET", parsed.location[0] != '\0'
                ? parsed.location : "NO LOCATION HEADER");
        }
        return;
    }
    if (parsed.body_offset > response_length) {
        browser_set_status("Invalid HTTP body offset");
        return;
    }
    browser_render_body(browser_response + parsed.body_offset,
                        response_length - parsed.body_offset);
    if (parsed.status_code >= 200U && parsed.status_code < 300U) {
        browser_source_count = nexus_browser_html_extract_links_with_base(
            browser_response + parsed.body_offset,
            response_length - parsed.body_offset, input,
            browser_sources, NEXUS_BROWSER_SOURCE_COUNT);
        size_t url_length = 0U;
        while (input[url_length] != '\0' &&
               url_length + 1U < sizeof(browser_current_url)) {
            browser_current_url[url_length] = input[url_length];
            ++url_length;
        }
        browser_current_url[url_length] = '\0';
        browser_set_status("HTTP page loaded | Ctrl+S saves to Nexus Data");
    } else {
        browser_set_status("HTTP response status is not successful");
    }
}

static void terminal_append(const char *text, size_t *used) {
    if (text == NULL || used == NULL) return;
    while (*text != '\0' && *used < sizeof(terminal_output) - 1U)
        terminal_output[(*used)++] = *text++;
}

static void terminal_append_number(uint64_t value, size_t *used) {
    char number[21];
    format_u64(value, number);
    terminal_append(number, used);
}

static void terminal_execute(void) {
    terminal_output[0] = '\0';
    if (terminal_input_length == 0U) return;
    terminal_input[terminal_input_length] = '\0';
    if (terminal_input[0] == 'h' && terminal_input[1] == 'e' &&
        terminal_input[2] == 'l' && terminal_input[3] == 'p' &&
        terminal_input[4] == '\0') {
        const char *s = "help about info mem uptime storage net dns HOST http HOST ls cd DIR cd .. pwd cp SOURCE DEST mv SOURCE DEST cat FILE run FILE ps touch NAME write NAME TEXT save NAME rm NAME";
        size_t i = 0; while (s[i] != '\0' && i < sizeof(terminal_output)-1U) { terminal_output[i]=s[i]; ++i; }
        terminal_output[i]='\0';
    } else if (terminal_input[0]=='a' && terminal_input[1]=='b' &&
               terminal_input[2]=='o' && terminal_input[3]=='u' &&
               terminal_input[4]=='t' && terminal_input[5]=='\0') {
        const char *s = "Nexus OS original x86-64 kernel";
        size_t i=0; while(s[i]!='\0' && i<sizeof(terminal_output)-1U){terminal_output[i]=s[i];++i;} terminal_output[i]='\0';
    } else if (terminal_input[0]=='i' && terminal_input[1]=='n' &&
                terminal_input[2]=='f' && terminal_input[3]=='o' &&
                terminal_input[4]=='\0') {
        size_t out = 0U;
        terminal_append("NEXUS x86-64 | uptime ", &out);
        terminal_append_number(timer_ticks() / 100U, &out);
        terminal_append(" s | memory ", &out);
        terminal_append_number(memory_free_bytes() / 1024U, &out);
        terminal_append("/", &out);
        terminal_append_number(memory_total_bytes() / 1024U, &out);
        terminal_append(" KiB free/total | disks ", &out);
        terminal_append_number(blockdev_count(), &out);
        terminal_append(" | processes ", &out);
        size_t processes = 0U;
        for (size_t slot = 0U; slot < NEXUS_PROCESS_MAX; ++slot) {
            struct nexus_process_info process_info;
            if (process_get_info(slot, &process_info)) ++processes;
        }
        terminal_append_number(processes, &out);
        terminal_append(" | NXFS ", &out);
        terminal_append(vfs_nxfs_mounted()
            ? (vfs_nxfs_writable() ? "writable" : "read-only")
            : "not mounted", &out);
        terminal_output[out] = '\0';
    } else if (terminal_input[0]=='m' && terminal_input[1]=='e' &&
               terminal_input[2]=='m' && terminal_input[3]=='\0') {
        const char *label = "free bytes: ";
        size_t n = 0;
        while (label[n] != '\0' && n < sizeof(terminal_output)-1U) {
            terminal_output[n] = label[n]; ++n;
        }
        char number[21];
        format_u64(memory_free_bytes(), number);
        for (size_t k = 0; number[k] != '\0' &&
             n < sizeof(terminal_output)-1U; ++k) terminal_output[n++] = number[k];
        terminal_output[n] = '\0';
    } else if (terminal_input[0]=='u' && terminal_input[1]=='p' &&
               terminal_input[2]=='t' && terminal_input[3]=='i' &&
               terminal_input[4]=='m' && terminal_input[5]=='e' &&
               terminal_input[6]=='\0') {
        const uint64_t seconds = timer_ticks() / 100U;
        const char *label = "uptime seconds: ";
        size_t n = 0;
        while (label[n] != '\0' && n < sizeof(terminal_output)-1U) {
            terminal_output[n] = label[n]; ++n;
        }
        char number[21];
        format_u64(seconds, number);
        for (size_t k = 0; number[k] != '\0' &&
             n < sizeof(terminal_output)-1U; ++k) terminal_output[n++] = number[k];
        terminal_output[n] = '\0';
    } else if (terminal_input[0]=='s' && terminal_input[1]=='t' &&
               terminal_input[2]=='o' && terminal_input[3]=='r' &&
               terminal_input[4]=='a' && terminal_input[5]=='g' &&
               terminal_input[6]=='e' && terminal_input[7]=='\0') {
        /* Read-only storage inventory: expose detected device identity,
         * capacity, and safety state without performing any disk writes. */
        size_t out = 0U;
        const size_t count = blockdev_count();
        if (count == 0U) {
            const char *message = "no block devices detected";
            while (message[out] != '\0' &&
                   out < sizeof(terminal_output) - 1U) {
                terminal_output[out] = message[out];
                ++out;
            }
        } else {
            for (size_t slot = 0U; slot < count &&
                 out < sizeof(terminal_output) - 32U; ++slot) {
                const struct nexus_block_device *device = blockdev_at(slot);
                if (device == NULL) continue;
                if (slot != 0U) terminal_output[out++] = ';';
                const char *name = device->name;
                for (size_t i = 0U; name[i] != '\0' &&
                     out < sizeof(terminal_output) - 24U; ++i)
                    terminal_output[out++] = name[i];
                terminal_output[out++] = ' ';
                char gib[21];
                const uint64_t bytes =
                    device->block_count * (uint64_t)device->block_size;
                format_u64(bytes / (1024ULL * 1024ULL * 1024ULL), gib);
                for (size_t i = 0U; gib[i] != '\0' &&
                     out < sizeof(terminal_output) - 16U; ++i)
                    terminal_output[out++] = gib[i];
                const char *suffix = " GiB";
                for (size_t i = 0U; suffix[i] != '\0' &&
                     out < sizeof(terminal_output) - 1U; ++i)
                    terminal_output[out++] = suffix[i];
                if (blockdev_is_boot_media(device)) {
                    const char *boot = " [BOOT]";
                    for (size_t i = 0U; boot[i] != '\0' &&
                         out < sizeof(terminal_output) - 1U; ++i)
                        terminal_output[out++] = boot[i];
                }
            }
        }
        terminal_output[out] = '\0';
    } else if (terminal_input[0]=='n' && terminal_input[1]=='e' &&
               terminal_input[2]=='t' && terminal_input[3]=='\0') {
        struct nexus_net_info info;
        if (!net_get_info(&info)) {
            const char *message = "no Ethernet interface";
            size_t out = 0U;
            while (message[out] != '\0' && out < sizeof(terminal_output) - 1U) {
                terminal_output[out] = message[out]; ++out;
            }
            terminal_output[out] = '\0';
        } else if (!info.ipv4_configured) {
            const char *message = "Ethernet ready; no IPv4 address yet";
            size_t out = 0U;
            while (message[out] != '\0' && out < sizeof(terminal_output) - 1U) {
                terminal_output[out] = message[out]; ++out;
            }
            terminal_output[out] = '\0';
        } else {
            size_t out = 0U;
            const char *label = "IPv4 ";
            for (size_t i = 0; label[i] != '\0' &&
                 out < sizeof(terminal_output) - 1U; ++i) {
                terminal_output[out++] = label[i];
            }
            for (size_t i = 0; i < 4U; ++i) {
                char number[21];
                format_u64(info.ipv4[i], number);
                for (size_t j = 0; number[j] != '\0' &&
                     out < sizeof(terminal_output) - 2U; ++j) {
                    terminal_output[out++] = number[j];
                }
                if (i != 3U && out < sizeof(terminal_output) - 1U)
                    terminal_output[out++] = '.';
            }
            const char *gateway_label = " gw ";
            for (size_t i = 0; gateway_label[i] != '\0' &&
                 out < sizeof(terminal_output) - 1U; ++i) {
                terminal_output[out++] = gateway_label[i];
            }
            for (size_t i = 0; i < 4U; ++i) {
                char number[21];
                format_u64(info.gateway[i], number);
                for (size_t j = 0; number[j] != '\0' &&
                     out < sizeof(terminal_output) - 2U; ++j) {
                    terminal_output[out++] = number[j];
                }
                if (i != 3U && out < sizeof(terminal_output) - 1U)
                    terminal_output[out++] = '.';
            }
            terminal_output[out] = '\0';
        }
    } else if (terminal_input[0]=='d' && terminal_input[1]=='n' &&
               terminal_input[2]=='s' && terminal_input[3]==' ') {
        uint8_t address[4];
        if (net_dns_resolve(terminal_input + 4, address, 600U)) {
            size_t out = 0U;
            for (size_t i = 0; i < 4U; ++i) {
                char number[21];
                format_u64(address[i], number);
                for (size_t j = 0; number[j] != '\0' &&
                     out < sizeof(terminal_output) - 2U; ++j) {
                    terminal_output[out++] = number[j];
                }
                if (i != 3U && out < sizeof(terminal_output) - 1U)
                    terminal_output[out++] = '.';
            }
            terminal_output[out] = '\0';
        } else {
            const char *message = "DNS lookup failed";
            size_t out = 0U;
            while (message[out] != '\0' &&
                   out < sizeof(terminal_output) - 1U) {
                terminal_output[out] = message[out]; ++out;
            }
            terminal_output[out] = '\0';
        }
    } else if (terminal_input[0]=='h' && terminal_input[1]=='t' &&
               terminal_input[2]=='t' && terminal_input[3]=='p' &&
               terminal_input[4]==' ') {
        const char *host = terminal_input + 5;
        size_t host_length = 0U;
        while (host[host_length] != '\0') ++host_length;
        uint8_t address[4];
        if (host_length == 0U ||
            !net_dns_resolve(host, address, 600U)) {
            const char *message = "HTTP: DNS lookup failed";
            size_t out = 0U;
            while (message[out] != '\0' &&
                   out < sizeof(terminal_output) - 1U) {
                terminal_output[out] = message[out]; ++out;
            }
            terminal_output[out] = '\0';
        } else if (!net_tcp_connect(address, 80U, 800U)) {
            const char *message = "HTTP: TCP connection failed";
            size_t out = 0U;
            while (message[out] != '\0' &&
                   out < sizeof(terminal_output) - 1U) {
                terminal_output[out] = message[out]; ++out;
            }
            terminal_output[out] = '\0';
        } else {
            char request[160];
            size_t request_length = 0U;
            const char *prefix = "GET / HTTP/1.0\r\nHost: ";
            const char *suffix = "\r\nConnection: close\r\n\r\n";
            while (prefix[request_length] != '\0') {
                request[request_length] = prefix[request_length];
                ++request_length;
            }
            for (size_t i = 0; i < host_length &&
                 request_length < sizeof(request) - 1U; ++i) {
                request[request_length++] = host[i];
            }
            for (size_t i = 0; suffix[i] != '\0' &&
                 request_length < sizeof(request); ++i) {
                request[request_length++] = suffix[i];
            }
            uint8_t response[sizeof(terminal_output) - 1U];
            size_t response_length = 0U;
            if (request_length > sizeof(request) ||
                !net_tcp_send(request, (uint16_t)request_length)) {
                const char *message = "HTTP: request send failed";
                size_t out = 0U;
                while (message[out] != '\0' &&
                       out < sizeof(terminal_output) - 1U) {
                    terminal_output[out] = message[out]; ++out;
                }
                terminal_output[out] = '\0';
            } else {
                const uint64_t wait_start = timer_ticks();
                uint64_t last_data = wait_start;
                while (timer_ticks() - wait_start < 400U &&
                       (response_length == 0U ||
                        timer_ticks() - last_data < 60U) &&
                       response_length < sizeof(response)) {
                    net_poll();
                    const size_t got =
                        net_tcp_receive(response + response_length,
                                        sizeof(response) - response_length);
                    if (got != 0U) {
                        response_length += got;
                        last_data = timer_ticks();
                    }
                    __asm__ volatile ("pause");
                }
                if (response_length == 0U) {
                    const char *message = "HTTP: no response";
                    size_t out = 0U;
                    while (message[out] != '\0' &&
                           out < sizeof(terminal_output) - 1U) {
                        terminal_output[out] = message[out]; ++out;
                    }
                    terminal_output[out] = '\0';
                } else {
                    for (size_t i = 0; i < response_length; ++i) {
                        const uint8_t ch = response[i];
                        terminal_output[i] =
                            (ch >= 0x20U && ch <= 0x7EU) || ch == '\n'
                                ? (char)ch : ' ';
                    }
                    terminal_output[response_length] = '\0';
                }
            }
            net_tcp_close();
        }
    } else if (terminal_input[0]=='c' && terminal_input[1]=='p' &&
               terminal_input[2]==' ') {
        const char *arguments = terminal_input + 3;
        size_t source_length = 0U;
        while (arguments[source_length] != '\0' &&
               arguments[source_length] != ' ') ++source_length;
        const char *destination = arguments + source_length;
        while (*destination == ' ') ++destination;
        size_t destination_length = 0U;
        while (destination[destination_length] != '\0')
            ++destination_length;
        if (source_length == 0U ||
            source_length >= NEXUS_VFS_NAME_MAX ||
            destination_length == 0U ||
            destination_length >= NEXUS_VFS_NAME_MAX) {
            size_t out = 0U;
            terminal_append("usage: cp SOURCE DEST", &out);
            terminal_output[out] = '\0';
        } else {
            char source_name[NEXUS_VFS_NAME_MAX];
            char destination_name[NEXUS_VFS_NAME_MAX];
            for (size_t i = 0U; i < source_length; ++i)
                source_name[i] = arguments[i];
            source_name[source_length] = '\0';
            for (size_t i = 0U; i < destination_length; ++i)
                destination_name[i] = destination[i];
            destination_name[destination_length] = '\0';
            const bool copied = vfs_copy_file(source_name,
                                              destination_name);
            size_t out = 0U;
            terminal_append(copied ? "copied into Nexus root storage" :
                "copy failed: name exists, source too large, or no writable space",
                &out);
            terminal_output[out] = '\0';
        }
    } else if (terminal_input[0]=='m' && terminal_input[1]=='v' &&
               terminal_input[2]==' ') {
        const char *arguments = terminal_input + 3;
        size_t source_length = 0U;
        while (arguments[source_length] != '\0' &&
               arguments[source_length] != ' ') ++source_length;
        const char *destination = arguments + source_length;
        while (*destination == ' ') ++destination;
        size_t destination_length = 0U;
        while (destination[destination_length] != '\0')
            ++destination_length;
        if (source_length == 0U ||
            source_length >= NEXUS_VFS_NAME_MAX ||
            destination_length == 0U ||
            destination_length >= NEXUS_VFS_NAME_MAX) {
            size_t out = 0U;
            terminal_append("usage: mv SOURCE DEST", &out);
            terminal_output[out] = '\0';
        } else {
            char source_name[NEXUS_VFS_NAME_MAX];
            char destination_name[NEXUS_VFS_NAME_MAX];
            for (size_t i = 0U; i < source_length; ++i)
                source_name[i] = arguments[i];
            source_name[source_length] = '\0';
            for (size_t i = 0U; i < destination_length; ++i)
                destination_name[i] = destination[i];
            destination_name[destination_length] = '\0';
            const bool moved = vfs_move_file(source_name, destination_name);
            size_t out = 0U;
            terminal_append(moved ? "moved and verified file" :
                "move failed: destination exists, source is read-only, or verification failed",
                &out);
            terminal_output[out] = '\0';
        }
    } else if (terminal_input[0]=='s' && terminal_input[1]=='a' &&
               terminal_input[2]=='v' && terminal_input[3]=='e' &&
               terminal_input[4]==' ') {
        const bool ok = vfs_save_ram_file_to_nxfs(terminal_input + 5);
        const char *message = ok
            ? "saved to persistent NexusFS"
            : "save unavailable: mount writable NexusFS and use a RAM file";
        size_t out = 0U;
        while (message[out] != '\0' && out < sizeof(terminal_output) - 1U) {
            terminal_output[out] = message[out];
            ++out;
        }
        terminal_output[out] = '\0';
    } else if (terminal_input[0]=='t' && terminal_input[1]=='o' &&
               terminal_input[2]=='u' && terminal_input[3]=='c' &&
               terminal_input[4]=='h' && terminal_input[5]==' ') {
        const bool ok = vfs_ramfs_create(terminal_input + 6);
        const char *message = ok ? "RAM file created" : "invalid name or RAMFS full";
        size_t out = 0U;
        while (message[out] != '\0' && out < sizeof(terminal_output) - 1U) {
            terminal_output[out] = message[out]; ++out;
        }
        terminal_output[out] = '\0';
    } else if (terminal_input[0]=='w' && terminal_input[1]=='r' &&
               terminal_input[2]=='i' && terminal_input[3]=='t' &&
               terminal_input[4]=='e' && terminal_input[5]==' ') {
        const char *argument = terminal_input + 6;
        size_t name_length = 0U;
        while (argument[name_length] != '\0' && argument[name_length] != ' ')
            ++name_length;
        if (argument[name_length] != ' ' || name_length == 0U ||
            name_length >= NEXUS_VFS_NAME_MAX) {
            const char *message = "usage: write NAME TEXT";
            size_t out = 0U;
            while (message[out] != '\0' && out < sizeof(terminal_output) - 1U) {
                terminal_output[out] = message[out]; ++out;
            }
            terminal_output[out] = '\0';
        } else {
            char filename[NEXUS_VFS_NAME_MAX];
            for (size_t i = 0; i < name_length; ++i) filename[i] = argument[i];
            filename[name_length] = '\0';
            const char *content = argument + name_length + 1U;
            size_t content_length = 0U;
            while (content[content_length] != '\0') ++content_length;
            const bool ok = vfs_ramfs_write(filename, content, content_length);
            const char *message = ok ? "RAM file written" : "write failed (RAM file limit 4096 bytes)";
            size_t out = 0U;
            while (message[out] != '\0' && out < sizeof(terminal_output) - 1U) {
                terminal_output[out] = message[out]; ++out;
            }
            terminal_output[out] = '\0';
        }
    } else if (terminal_input[0]=='r' && terminal_input[1]=='m' &&
               terminal_input[2]==' ') {
        const bool ok = vfs_remove_file(terminal_input + 3);
        const char *message = ok
            ? "writable file removed"
            : "file not found or volume is read-only";
        size_t out = 0U;
        while (message[out] != '\0' && out < sizeof(terminal_output) - 1U) {
            terminal_output[out] = message[out]; ++out;
        }
        terminal_output[out] = '\0';
    } else if (terminal_input[0]=='p' && terminal_input[1]=='w' &&
               terminal_input[2]=='d' && terminal_input[3]=='\0') {
        if (!vfs_root_mounted()) scan_files_once();
        if (!vfs_current_path(terminal_output, sizeof(terminal_output))) {
            size_t out = 0U;
            terminal_append("no FAT32 directory is mounted", &out);
            terminal_output[out] = '\0';
        }
    } else if (terminal_input[0]=='c' && terminal_input[1]=='d' &&
               terminal_input[2]==' ' && terminal_input[3]!='\0') {
        if (!vfs_root_mounted()) scan_files_once();
        const char *target = terminal_input + 3;
        size_t out = 0U;
        if (!vfs_root_mounted()) {
            terminal_append("no FAT32 directory is mounted", &out);
        } else if (target[0]=='.' && target[1]=='.' && target[2]=='\0') {
            if (!vfs_parent_directory()) {
                terminal_append("already at root", &out);
            } else {
                terminal_append("directory: ", &out);
                char path[sizeof(terminal_output)];
                if (vfs_current_path(path, sizeof(path)))
                    terminal_append(path, &out);
            }
        } else if (!vfs_enter_directory(target)) {
            terminal_append("directory not found or depth limit reached", &out);
        } else {
            terminal_append("directory: ", &out);
            char path[sizeof(terminal_output)];
            if (vfs_current_path(path, sizeof(path)))
                terminal_append(path, &out);
        }
        terminal_output[out] = '\0';
    } else if (terminal_input[0]=='l' && terminal_input[1]=='s' &&
               terminal_input[2]=='\0') {
        if (!vfs_has_filesystem()) scan_files_once();
        size_t out = 0U;
        const size_t entries = vfs_root_entry_count();
        if (!vfs_has_filesystem()) {
            terminal_append("no mounted volume", &out);
        } else if (entries == 0U) {
            terminal_append("(empty)", &out);
        } else {
            for (size_t n = 0U; n < entries &&
                 out < sizeof(terminal_output) - 1U; ++n) {
                struct nexus_vfs_dirent entry;
                if (!vfs_root_entry(n, &entry)) continue;
                if (n != 0U) terminal_append("  ", &out);
                if (entry.type == NEXUS_VFS_DIRECTORY) {
                    terminal_append("[DIR] ", &out);
                    terminal_append(entry.name, &out);
                    terminal_append("/", &out);
                } else {
                    terminal_append("[FILE] ", &out);
                    terminal_append(entry.name, &out);
                    terminal_append(" (", &out);
                    terminal_append_number(entry.size, &out);
                    terminal_append(" B)", &out);
                }
            }
        }
        terminal_output[out] = '\0';
    } else if (terminal_input[0]=='p' && terminal_input[1]=='s' &&
               terminal_input[2]=='\0') {
        size_t out = 0U;
        for (size_t slot = 0; slot < NEXUS_PROCESS_MAX; ++slot) {
            struct nexus_process_info info;
            if (!process_get_info(slot, &info)) continue;
            char number[21];
            format_u64(info.pid, number);
            const char *prefix = "pid ";
            for (size_t i = 0; prefix[i] != '\0' &&
                 out < sizeof(terminal_output) - 2U; ++i) {
                terminal_output[out++] = prefix[i];
            }
            for (size_t i = 0; number[i] != '\0' &&
                 out < sizeof(terminal_output) - 2U; ++i) {
                terminal_output[out++] = number[i];
            }
            if (out < sizeof(terminal_output) - 1U)
                terminal_output[out++] = ' ';
            for (size_t i = 0; info.name[i] != '\0' &&
                 out < sizeof(terminal_output) - 2U; ++i) {
                terminal_output[out++] = info.name[i];
            }
            const char *state = process_state_name(info.state);
            if (out < sizeof(terminal_output) - 1U)
                terminal_output[out++] = ' ';
            for (size_t i = 0; state[i] != '\0' &&
                 out < sizeof(terminal_output) - 2U; ++i) {
                terminal_output[out++] = state[i];
            }
            if (info.state == NEXUS_PROCESS_EXITED) {
                if (out < sizeof(terminal_output) - 1U)
                    terminal_output[out++] = ' ';
                format_u64(info.exit_status, number);
                for (size_t i = 0; number[i] != '\0' &&
                     out < sizeof(terminal_output) - 2U; ++i) {
                    terminal_output[out++] = number[i];
                }
            }
            if (out < sizeof(terminal_output) - 1U)
                terminal_output[out++] = ';';
            if (out < sizeof(terminal_output) - 1U)
                terminal_output[out++] = ' ';
        }
        if (out == 0U) {
            const char *message = "no process records";
            for (size_t i = 0; message[i] != '\0' &&
                 out < sizeof(terminal_output) - 1U; ++i) {
                terminal_output[out++] = message[i];
            }
        }
        terminal_output[out] = '\0';
    } else if (terminal_input[0]=='r' && terminal_input[1]=='u' &&
               terminal_input[2]=='n' && terminal_input[3]==' ') {
        if (!vfs_root_mounted()) scan_files_once();
        struct nexus_process_result result;
        enum nexus_process_error error = NEXUS_PROCESS_ERROR_NONE;
        if (process_execute_file(terminal_input + 4, &result, &error)) {
            size_t out = 0U;
            const char *prefix = "pid ";
            for (size_t i = 0; prefix[i] != '\0' &&
                 out < sizeof(terminal_output) - 1U; ++i) {
                terminal_output[out++] = prefix[i];
            }
            char number[21];
            format_u64(result.pid, number);
            for (size_t i = 0; number[i] != '\0' &&
                 out < sizeof(terminal_output) - 1U; ++i) {
                terminal_output[out++] = number[i];
            }
            const char *middle = " exited ";
            for (size_t i = 0; middle[i] != '\0' &&
                 out < sizeof(terminal_output) - 1U; ++i) {
                terminal_output[out++] = middle[i];
            }
            format_u64(result.exit_status, number);
            for (size_t i = 0; number[i] != '\0' &&
                 out < sizeof(terminal_output) - 1U; ++i) {
                terminal_output[out++] = number[i];
            }
            terminal_output[out] = '\0';
        } else {
            const char *message = "program could not run";
            switch (error) {
                case NEXUS_PROCESS_ERROR_NO_VOLUME: message = "no mounted volume"; break;
                case NEXUS_PROCESS_ERROR_NOT_FOUND: message = "program not found"; break;
                case NEXUS_PROCESS_ERROR_IO: message = "program read error"; break;
                case NEXUS_PROCESS_ERROR_TOO_LARGE: message = "program exceeds 128 KiB limit"; break;
                case NEXUS_PROCESS_ERROR_TABLE_FULL: message = "process table full"; break;
                case NEXUS_PROCESS_ERROR_BAD_ELF: message = "invalid or unsupported ELF64 program"; break;
                case NEXUS_PROCESS_ERROR_EXECUTION: message = "program execution failed"; break;
                default: break;
            }
            size_t out = 0U;
            while (message[out] != '\0' && out < sizeof(terminal_output) - 1U) {
                terminal_output[out] = message[out];
                ++out;
            }
            terminal_output[out] = '\0';
        }
    } else if (terminal_input[0]=='c' && terminal_input[1]=='a' &&
               terminal_input[2]=='t' && terminal_input[3]==' ') {
        if (!vfs_root_mounted()) scan_files_once();
        const char *name=terminal_input+4;
        struct nexus_vfs_file file;
        if(vfs_open_readonly(name,&file)) {
            uint8_t data[sizeof(terminal_output)-1U]; size_t got=0;
            if(vfs_read(&file,data,sizeof(data),&got)) {
                for(size_t n=0;n<got;++n) {
                    uint8_t ch=data[n];
                    terminal_output[n]=(ch>=0x20U && ch<=0x7EU)?(char)ch:' ';
                }
                terminal_output[got]='\0';
            } else terminal_output[0]='?';
            vfs_close(&file);
        } else {
            const char *s="file not found";
            size_t n=0; while(s[n]!='\0' && n<sizeof(terminal_output)-1U){terminal_output[n]=s[n];++n;}
            terminal_output[n]='\0';
        }
    } else if (terminal_input[0]=='c' && terminal_input[1]=='l' &&
               terminal_input[2]=='e' && terminal_input[3]=='a' &&
               terminal_input[4]=='r' && terminal_input[5]=='\0') {
        terminal_output[0]='\0';
    } else {
        const char *s="unknown command (type help)";
        size_t n=0; while(s[n]!='\0' && n<sizeof(terminal_output)-1U){terminal_output[n]=s[n];++n;}
        terminal_output[n]='\0';
    }
    if (files_scanned) refresh_files_listing();
    terminal_input_length=0;
    terminal_input[0]='\0';
}

static void installer_set_status(const char *message) {
    size_t i = 0U;
    if (message == NULL) message = "";
    while (message[i] != '\0' && i < sizeof(installer_status_text) - 1U) {
        installer_status_text[i] = message[i];
        ++i;
    }
    installer_status_text[i] = '\0';
}

#ifdef NEXUS_INSTALLER_ENABLE_DESTRUCTIVE_WRITES
static void installer_progress_update(
    void *context, enum nexus_installer_runtime_phase phase,
    const char *message) {
    (void)context;
    installer_set_status(message);
    serial_write("NEXUS INSTALLER PHASE: ");
    serial_write(message != NULL ? message : "");
    serial_write("\r\n");
    installer_ui_state = phase == NEXUS_INSTALL_PHASE_COMPLETE
        ? INSTALLER_UI_COMPLETE : INSTALLER_UI_RUNNING;
    draw_shell();
    draw_pointer();
}

static const char *installer_result_message(
    enum nexus_installer_runtime_result result) {
    switch (result) {
        case NEXUS_INSTALL_RESULT_OK: return "Installation completed";
        case NEXUS_INSTALL_RESULT_EXISTING_RECOVERY:
            return "Saved install state is invalid; retry a clean install";
        case NEXUS_INSTALL_RESULT_PARTITION_WRITE:
            return "GPT write failed; inspect disk before retrying";
        case NEXUS_INSTALL_RESULT_PARTITION_VERIFY:
            return "GPT read-back verification failed";
        case NEXUS_INSTALL_RESULT_JOURNAL:
            return "Could not save install progress to the EFI partition";
        case NEXUS_INSTALL_RESULT_ESP_FORMAT:
            return "EFI partition formatting failed";
        case NEXUS_INSTALL_RESULT_SYSTEM_FORMAT:
            return "Nexus system filesystem formatting failed";
        case NEXUS_INSTALL_RESULT_SYSTEM_PAYLOAD:
            return "Nexus system payload deployment failed";
        case NEXUS_INSTALL_RESULT_BOOT_PAYLOAD:
            return "UEFI/BIOS boot files deployment failed";
        case NEXUS_INSTALL_RESULT_BIOS_BOOTLOADER:
            return "Legacy BIOS bootloader installation failed";
        case NEXUS_INSTALL_RESULT_UNSUPPORTED_LAYOUT:
            return "Selected disk does not support this firmware layout";
        case NEXUS_INSTALL_RESULT_UNSAFE_TARGET:
            return "Target identity or boot-media safety check failed";
        default: return "Installer rejected the selected target or input";
    }
}
#endif

static void installer_start_install(void) {
    const struct nexus_block_device *device =
        blockdev_at(installer_selected_device);
#ifdef NEXUS_INSTALLER_ENABLE_DESTRUCTIVE_WRITES
    installer_ui_state = INSTALLER_UI_RUNNING;
    installer_set_status("Starting guarded installation");
    draw_shell();
    draw_pointer();
    const enum nexus_installer_runtime_result result =
        nexus_installer_runtime_apply(
            device, installer_firmware, installer_confirmed_id,
            "ERASE", installer_progress_update, NULL);
    if (result == NEXUS_INSTALL_RESULT_OK) {
        installer_ui_state = INSTALLER_UI_COMPLETE;
        serial_write("NEXUS INSTALLER RESULT: SUCCESS\r\n");
    } else {
        installer_ui_state = INSTALLER_UI_FAILED;
        installer_set_status(installer_result_message(result));
        serial_write("NEXUS INSTALLER RESULT: FAILURE: ");
        serial_write(installer_result_message(result));
        serial_write("\r\n");
    }
#else
    (void)device;
    installer_ui_state = INSTALLER_UI_FAILED;
    installer_set_status(
        "DISK WRITES LOCKED: END-TO-END VALIDATION NOT PASSED");
    serial_write("NEXUS INSTALLER RESULT: LOCKED\r\n");
#endif
}


void desktop_handle_input(const struct nexus_input_event *event) {
    if (!desktop_ready || event == NULL) return;
    if (event->type == NEXUS_INPUT_MOUSE) {
        /* Mouse motion updates only the cursor damage region. */
        erase_pointer();
        pointer_x += event->mouse_dx;
        pointer_y += event->mouse_dy;
        if (pointer_x < 0) pointer_x = 0;
        if (pointer_y < 0) pointer_y = 0;
        if ((uint64_t)pointer_x >= screen_width)
            pointer_x = (int64_t)(screen_width - 1U);
        if ((uint64_t)pointer_y >= screen_height)
            pointer_y = (int64_t)(screen_height - 1U);
        const bool left_down = (event->mouse_buttons & 1U) != 0;
        const bool clicked = left_down && !mouse_left_was_down;
        mouse_left_was_down = left_down;
        if (desktop_window_drag_mode != DESKTOP_WINDOW_DRAG_NONE) {
            if (left_down && !clicked) {
                desktop_update_window_drag();
                draw_shell();
                draw_pointer();
                return;
            }
            if (!left_down) {
                if (desktop_window_drag_mode == DESKTOP_WINDOW_DRAG_MOVE)
                    desktop_snap_window_at_pointer(desktop_dragged_window);
                desktop_window_drag_mode = DESKTOP_WINDOW_DRAG_NONE;
                desktop_dragged_window = 0U;
                draw_shell();
                draw_pointer();
                return;
            }
        }
        bool shell_click_handled = false;
        if (clicked) {
            if (workspace_overview_open) {
                desktop_workspace_overview_click();
                draw_shell();
                draw_pointer();
                return;
            }
            const uint64_t taskbar_height = clamp_min(screen_height / 11U, 48U);
            const uint64_t taskbar_top = screen_height - taskbar_height;
            if (start_menu_open) {
                const uint64_t menu_width=screen_width>500U?460U:screen_width-16U;
                const uint64_t menu_height=360U;
                const uint64_t menu_x=(screen_width-menu_width)/2U;
                const uint64_t top_limit=clamp_min(screen_height/16U,34U)+8U;
                const uint64_t menu_bottom=screen_height>taskbar_height
                    ?screen_height-taskbar_height:0U;
                uint64_t menu_y=menu_bottom>menu_height
                    ?menu_bottom-menu_height+2U:top_limit;
                if(menu_y<top_limit) menu_y=top_limit;
                desktop_launcher_refresh();
                const uint64_t search_x=menu_x+18U;
                const uint64_t search_y=menu_y+59U;
                const uint64_t search_w=menu_width-36U;
                bool app_clicked=false;
                if((uint64_t)pointer_x>=search_x &&
                   (uint64_t)pointer_x<search_x+search_w &&
                   (uint64_t)pointer_y>=search_y &&
                   (uint64_t)pointer_y<search_y+32U) {
                    /* Keep the launcher open so keyboard input can filter. */
                    app_clicked=true;
                } else if(menu_width>400U) {
                    const uint64_t cw=(menu_width-54U)/2U;
                    const uint64_t row_top=menu_y+113U;
                    if((uint64_t)pointer_y>=row_top &&
                       (uint64_t)pointer_y<row_top+4U*55U &&
                       (uint64_t)pointer_x>=menu_x+18U) {
                        const uint64_t rx=(uint64_t)pointer_x-menu_x-18U;
                        const uint64_t ry=(uint64_t)pointer_y-row_top;
                        const size_t col=(size_t)(rx/(cw+12U));
                        const size_t row=(size_t)(ry/55U);
                        const uint64_t local_x=rx%(cw+12U),local_y=ry%55U;
                        const size_t result=row*2U+col;
                        if(col<2U && row<4U && local_x<cw &&
                           local_y<48U && result<launcher_visible_count) {
                            launcher_selected_index=result;
                            desktop_launcher_open_selected();
                            shell_click_handled=true;
                            app_clicked=true;
                        }
                    }
                } else {
                    const uint64_t row_top=menu_y+112U;
                    if((uint64_t)pointer_x>=menu_x+14U &&
                       (uint64_t)pointer_x<menu_x+menu_width-14U &&
                       (uint64_t)pointer_y>=row_top &&
                       (uint64_t)pointer_y<row_top+launcher_visible_count*32U) {
                        const size_t result=(size_t)((uint64_t)pointer_y-row_top)/32U;
                        const uint64_t local_y=((uint64_t)pointer_y-row_top)%32U;
                        if(result<launcher_visible_count && local_y<29U) {
                            launcher_selected_index=result;
                            desktop_launcher_open_selected();
                            shell_click_handled=true;
                            app_clicked=true;
                        }
                    }
                }
                if(!app_clicked) start_menu_open=false;
            }
            if (!shell_click_handled &&
                (uint64_t)pointer_y < clamp_min(screen_height / 16U, 34U)) {
                const uint64_t topbar_height =
                    clamp_min(screen_height / 16U, 34U);
                const uint64_t x = (uint64_t)pointer_x;
                if (screen_width >= 500U && x >= desktop_workspace_tab_x(0U) &&
                    x < desktop_workspace_tab_x(0U) +
                        NEXUS_WM_MAX_WORKSPACES * 34U) {
                    for (uint8_t workspace = 0U;
                         workspace < NEXUS_WM_MAX_WORKSPACES; ++workspace) {
                        const uint64_t tab_x = desktop_workspace_tab_x(workspace);
                        if (x >= tab_x && x < tab_x + 28U) {
                            desktop_switch_workspace(workspace);
                            shell_click_handled = true;
                            break;
                        }
                    }
                } else if (screen_width >= 500U &&
                           x >= desktop_workspace_overview_x() &&
                           x < desktop_workspace_overview_x() + 82U) {
                    workspace_overview_open = true;
                    workspace_overview_selected = nexus_wm_active_workspace();
                    start_menu_open = false;
                    desktop_launcher_reset();
                    shell_click_handled = true;
                } else if (x < 108U) {
                    desktop_open_view(DESKTOP_HOME);
                    shell_click_handled = true;
                } else if (screen_width > 620U &&
                           x >= screen_width - 176U &&
                           x < screen_width - 18U) {
                    desktop_open_view(DESKTOP_CONTROL_CENTER);
                    shell_click_handled = true;
                }
                (void)topbar_height;
            }
            if(!shell_click_handled && (uint64_t)pointer_y>=taskbar_top) {
                if((uint64_t)pointer_x<56U) {
                    start_menu_open=!start_menu_open;
                    if(start_menu_open) desktop_launcher_reset();
                    shell_click_handled=true;
                } else if((uint64_t)pointer_x>=desktop_taskbar_control_x() &&
                          (uint64_t)pointer_x<desktop_taskbar_control_x()+42U) {
                    desktop_open_view(DESKTOP_CONTROL_CENTER);
                    shell_click_handled=true;
                } else {
                    const uint64_t apps_start=desktop_taskbar_apps_start();
                    for(size_t app=0U;app<7U;++app) {
                        if(app==5U) continue;
                        const size_t visible_index=app<5U?app:app-1U;
                        const uint64_t ax=apps_start+(uint64_t)visible_index*42U;
                        if((uint64_t)pointer_x>=ax && (uint64_t)pointer_x<ax+40U) {
                            desktop_toggle_app(app); shell_click_handled=true; break;
                        }
                    }
                }
            }
            if (!shell_click_handled && active_view == DESKTOP_INSTALLER) {
                uint64_t px = 0U, py = 0U, pw = 0U, ph = 0U;
                desktop_get_current_window_rect(&px, &py, &pw, &ph);
                (void)pw;
                (void)ph;
                if (installer_ui_state == INSTALLER_UI_CONFIRM_ERASE &&
                    (uint64_t)pointer_x >= px + 32U &&
                    (uint64_t)pointer_x < px + 32U + 300U &&
                    (uint64_t)pointer_y >= py + 48U + 180U &&
                    (uint64_t)pointer_y < py + 48U + 222U) {
                    installer_start_install();
                    draw_shell();
                    draw_pointer();
                    return;
                } else if (installer_ui_state == INSTALLER_UI_CONFIRM_ERASE &&
                    (uint64_t)pointer_x >= px + 32U + 312U &&
                    (uint64_t)pointer_x < px + 32U + 424U &&
                    (uint64_t)pointer_y >= py + 48U + 180U &&
                    (uint64_t)pointer_y < py + 48U + 222U) {
                    installer_ui_state = INSTALLER_UI_SELECT;
                    installer_set_status("Select a supported target disk");
                    draw_shell();
                    draw_pointer();
                    return;
                } else if (installer_ui_state == INSTALLER_UI_COMPLETE &&
                    (uint64_t)pointer_x >= px + 32U &&
                    (uint64_t)pointer_x < px + 276U &&
                    (uint64_t)pointer_y >= py + 48U + 194U &&
                    (uint64_t)pointer_y < py + 48U + 230U)
                    desktop_reboot_after_install();
            }
            if (!shell_click_handled &&
                desktop_handle_window_mouse_click())
                shell_click_handled = true;
        }
        if (shell_click_handled) {
            draw_shell();
            draw_pointer();
            return;
        }
        if (clicked && active_view == DESKTOP_FILES && files_vfs_ready) {
            if (files_delete_pending) {
                draw_shell();
                draw_pointer();
                return;
            }
            uint64_t px = 0U, py = 0U, pw = 0U, ph = 0U;
            desktop_get_current_window_rect(&px, &py, &pw, &ph);
            const uint64_t tx = px + 32U;
            const uint64_t ty = py + 82U;
            const int64_t relative_y = pointer_y - (int64_t)(ty + 270U);
            const int64_t relative_x = pointer_x - (int64_t)tx;
            const size_t action_count = pw >= 560U ? 6U : 4U;
            const uint64_t action_gap = 6U;
            const uint64_t action_width =
                (pw - 64U - (action_count - 1U) * action_gap) /
                action_count;
            const uint64_t toolbar_width =
                action_count * action_width + (action_count - 1U) * action_gap;
            if (relative_y >= 0 && relative_y < 25 && relative_x >= 0 &&
                (uint64_t)relative_x < toolbar_width) {
                const uint64_t local_x =
                    (uint64_t)relative_x % (action_width + action_gap);
                const size_t action =
                    (size_t)((uint64_t)relative_x / (action_width + action_gap));
                if (local_x >= action_width) {
                    draw_shell();
                    draw_pointer();
                    return;
                }
                if (action == 0U) {
                    files_editor_begin_new();
                } else if (action == 1U) {
                    files_editor_begin_selected();
                } else if (action_count == 6U && action == 2U) {
                    files_editor_begin_copy();
                } else if (action_count == 6U && action == 3U) {
                    files_editor_begin_rename();
                } else if ((action_count == 6U && action == 4U) ||
                           (action_count == 4U && action == 2U)) {
                    files_request_delete_selected();
                } else if ((action_count == 6U && action == 5U) ||
                           (action_count == 4U && action == 3U)) {
                    if (vfs_directory_depth() != 0U) {
                        if (vfs_parent_directory()) {
                            files_preview[0] = '\0';
                            files_first_name[0] = '\0';
                            refresh_files_listing();
                            serial_write("FILES: parent directory opened by mouse\r\n");
                        }
                    } else {
                        refresh_files_listing();
                        serial_write("FILES: listing refreshed by mouse\r\n");
                    }
                }
                draw_shell();
                draw_pointer();
                return;
            }
            const int64_t relative_list_y =
                pointer_y - (int64_t)(ty + 321U);
            if (relative_list_y >= 0 && relative_list_y < 60 &&
                relative_x >= 0 && relative_x < 500) {
                const size_t row = (size_t)relative_list_y / 24U;
                const size_t column = (size_t)relative_x / 260U;
                const size_t slot = row * 2U + column;
                const size_t index = files_listing_first_index + slot;
                if (column < 2U && slot < FILES_LISTING_VISIBLE &&
                    index < files_listing_count) {
                    files_selected_index = index;
                    files_listing_ensure_selection_visible();
                    const uint64_t now = timer_ticks();
                    if (files_last_click_valid &&
                        files_last_click_index == index &&
                        now - files_last_click_tick <= 50U) {
                        if (files_listing_types[index] == NEXUS_VFS_DIRECTORY) {
                            if (vfs_enter_directory(files_listing[index])) {
                                files_preview[0] = '\0';
                                files_first_name[0] = '\0';
                                refresh_files_listing();
                            }
                        } else {
                            files_editor_begin_selected();
                        }
                        files_last_click_valid = false;
                    } else {
                        files_last_click_valid = true;
                        files_last_click_index = index;
                        files_last_click_tick = now;
                    }
                }
            }
        }
        if (clicked && active_view == DESKTOP_CALCULATOR) {
            uint64_t px=0U,py=0U,pw=0U,ph=0U;
            desktop_get_current_window_rect(&px,&py,&pw,&ph);
            const uint64_t key_x=px+24U, key_y=py+82U+132U;
            const uint64_t key_w=(pw-72U)/4U;
            const int64_t rx=pointer_x-(int64_t)key_x;
            const int64_t ry=pointer_y-(int64_t)key_y;
            if(rx>=0 && ry>=0 && (uint64_t)rx<4U*key_w+24U &&
               (uint64_t)ry<250U) {
                const size_t col=(size_t)rx/(size_t)(key_w+8U);
                const size_t row=(size_t)ry/50U;
                const uint64_t local_x=(uint64_t)rx%(key_w+8U);
                if(col<4U && row<5U && local_x<key_w &&
                   (uint64_t)ry%50U<42U) calculator_button(row,col);
            }
            draw_shell(); draw_pointer(); return;
        }
        if(clicked && active_view==DESKTOP_CONTROL_CENTER) {
            uint64_t px=0U,py=0U,pw=0U,ph=0U;
            desktop_get_current_window_rect(&px,&py,&pw,&ph);
            const uint64_t qx=px+24U,qy=py+82U+198U,qw=(pw-64U)/3U;
            const int64_t rx=pointer_x-(int64_t)qx,ry=pointer_y-(int64_t)qy;
            if(rx>=0 && ry>=0 && (uint64_t)rx<3U*qw+16U &&
               (uint64_t)ry<116U) {
                const size_t col=(size_t)rx/(size_t)(qw+8U);
                const size_t row=(size_t)ry/62U;
                const uint64_t local_x=(uint64_t)rx%(qw+8U);
                if(col<3U && row<2U && local_x<qw && (uint64_t)ry%62U<54U) {
                    desktop_control_center_activate(row*3U+col);
                    shell_click_handled=true;
                }
            }
        }
        if (clicked && active_view == DESKTOP_SETTINGS) {
            uint64_t px=0U,py=0U,pw=0U,ph=0U;
            desktop_get_current_window_rect(&px,&py,&pw,&ph);
            const uint64_t row_x = px + 24U;
            const uint64_t row_y = py + 82U + 54U;
            const uint64_t row_w = pw - 48U;
            if ((uint64_t)pointer_x >= row_x &&
                (uint64_t)pointer_x < row_x + row_w) {
                for (size_t option = 0U; option < 7U; ++option) {
                    const uint64_t y = row_y + (uint64_t)option * 42U;
                    if ((uint64_t)pointer_y >= y &&
                        (uint64_t)pointer_y < y + 36U) {
                        desktop_settings_activate(option);
                        shell_click_handled = true;
                        break;
                    }
                }
            }
        }
        if (clicked && active_view == DESKTOP_HOME) {
            const uint64_t top = clamp_min(screen_height / 16U, 34U);
            const uint64_t bottom = clamp_min(screen_height / 11U, 48U);
            const uint64_t usable_bottom = screen_height > bottom
                ? screen_height - bottom : 0U;
            for (size_t app = 0U; app < 7U; ++app) {
                const uint64_t col = app / 6U;
                const uint64_t row = app % 6U;
                const uint64_t x = 18U + col * 92U;
                const uint64_t y = top + 28U + row * 76U;
                if (y + 70U <= usable_bottom &&
                    (uint64_t)pointer_x >= x &&
                    (uint64_t)pointer_x < x + 76U &&
                    (uint64_t)pointer_y >= y &&
                    (uint64_t)pointer_y < y + 70U) {
                    desktop_open_app(app);
                    serial_write("DESKTOP: desktop shortcut opened by mouse\r\n");
                    break;
                }
            }
        }
        if (!clicked) {
            draw_pointer();
            return;
        }
        draw_shell();
        draw_pointer();
        return;
    }
    if (event->type != NEXUS_INPUT_KEYBOARD) return;
    /*
     * Preserve keyboard modifiers for both PS/2 and USB HID input. USB HID
     * emits the same Set-1 make/break transitions as the PS/2 path.
     */
    const uint8_t key_code = (uint8_t)(event->scancode & 0x7FU);
    const bool key_released = (event->scancode & 0x80U) != 0U;
    if (key_code == 0x2AU) {
        keyboard_left_shift = !key_released;
        return;
    }
    if (key_code == 0x36U) {
        keyboard_right_shift = !key_released;
        return;
    }
    if (key_code == 0x38U) {
        keyboard_alt = !key_released;
        return;
    }
    if (key_code == 0x1DU) {
        keyboard_ctrl = !key_released;
        return;
    }
    if (key_code == 0x3AU) {
        if (key_released) {
            keyboard_caps_lock_down = false;
        } else if (!keyboard_caps_lock_down) {
            keyboard_caps_lock = !keyboard_caps_lock;
            keyboard_caps_lock_down = true;
        }
        return;
    }
    if (key_released) return;

    if (workspace_overview_open && active_view != DESKTOP_INSTALLER) {
        if (key_code == 0x01U) {
            workspace_overview_open = false;
        } else if (key_code == 0x4BU || key_code == 0x48U) {
            workspace_overview_selected =
                (uint8_t)((workspace_overview_selected +
                    NEXUS_WM_MAX_WORKSPACES - 1U) %
                    NEXUS_WM_MAX_WORKSPACES);
        } else if (key_code == 0x4DU || key_code == 0x50U) {
            workspace_overview_selected =
                (uint8_t)((workspace_overview_selected + 1U) %
                          NEXUS_WM_MAX_WORKSPACES);
        } else if (key_code >= 0x02U && key_code <= 0x05U) {
            desktop_switch_workspace((uint8_t)(key_code - 0x02U));
        } else if (key_code == 0x1CU) {
            desktop_switch_workspace(workspace_overview_selected);
        }
        draw_shell();
        draw_pointer();
        return;
    }

    /* Ctrl+Alt+Tab opens Task View, like the Windows workspace overview. */
    if (keyboard_ctrl && keyboard_alt && key_code == 0x0FU &&
        active_view != DESKTOP_INSTALLER && desktop_window_manager_ready) {
        workspace_overview_open = true;
        workspace_overview_selected = nexus_wm_active_workspace();
        start_menu_open = false;
        desktop_launcher_reset();
        draw_shell();
        draw_pointer();
        return;
    }
    if (keyboard_ctrl && keyboard_alt && key_code == 0x49U &&
        active_view != DESKTOP_INSTALLER && desktop_window_manager_ready) {
        desktop_cycle_workspace(-1);
        draw_shell();
        draw_pointer();
        return;
    }
    if (keyboard_ctrl && keyboard_alt && key_code == 0x51U &&
        active_view != DESKTOP_INSTALLER && desktop_window_manager_ready) {
        desktop_cycle_workspace(1);
        draw_shell();
        draw_pointer();
        return;
    }
    if (keyboard_ctrl && (keyboard_left_shift || keyboard_right_shift) &&
        !keyboard_alt && key_code == 0x49U &&
        active_view != DESKTOP_INSTALLER && desktop_window_manager_ready) {
        desktop_move_focused_workspace(-1);
        draw_shell();
        draw_pointer();
        return;
    }
    if (keyboard_ctrl && (keyboard_left_shift || keyboard_right_shift) &&
        !keyboard_alt && key_code == 0x51U &&
        active_view != DESKTOP_INSTALLER && desktop_window_manager_ready) {
        desktop_move_focused_workspace(1);
        draw_shell();
        draw_pointer();
        return;
    }

    /* Global shortcuts run before app text entry, so modifiers never leak
     * shortcut letters into documents, search fields or terminal commands. */
    if (keyboard_ctrl && keyboard_alt &&
        active_view != DESKTOP_INSTALLER && desktop_window_manager_ready) {
        enum desktop_snap_zone zone = DESKTOP_SNAP_RESTORE;
        bool snap = true;
        switch (key_code) {
            case 0x4BU: zone = DESKTOP_SNAP_LEFT; break;
            case 0x4DU: zone = DESKTOP_SNAP_RIGHT; break;
            case 0x48U: zone = DESKTOP_SNAP_MAXIMIZE; break;
            case 0x50U: zone = DESKTOP_SNAP_RESTORE; break;
            case 0x02U: zone = DESKTOP_SNAP_TOP_LEFT; break;
            case 0x03U: zone = DESKTOP_SNAP_TOP_RIGHT; break;
            case 0x04U: zone = DESKTOP_SNAP_BOTTOM_LEFT; break;
            case 0x05U: zone = DESKTOP_SNAP_BOTTOM_RIGHT; break;
            default: snap = false; break;
        }
        if (snap) {
            desktop_snap_window(nexus_wm_focused(), zone);
            start_menu_open = false;
            desktop_animate_view();
            draw_shell();
            draw_pointer();
            return;
        }
    }
    if (keyboard_ctrl && active_view != DESKTOP_INSTALLER) {
        if (key_code == 0x39U) { /* Ctrl+Space: application finder */
            start_menu_open = !start_menu_open;
            if (start_menu_open) desktop_launcher_reset();
            draw_shell();
            draw_pointer();
            return;
        }
        if (key_code >= 0x02U && key_code <= 0x08U) { /* Ctrl+1..7 */
            desktop_open_app((size_t)(key_code - 0x02U));
            draw_shell();
            draw_pointer();
            return;
        }
        if (active_view == DESKTOP_FILES &&
            files_editor_state == FILES_EDITOR_CLOSED && files_vfs_ready) {
            if (key_code == 0x21U) { /* Ctrl+F */
                files_search_active = true;
                for (size_t i = 0U; i < sizeof(files_search_query); ++i)
                    files_search_input[i] = files_search_query[i];
            } else if (key_code == 0x31U) { /* Ctrl+N */
                files_editor_begin_new();
            } else if (key_code == 0x18U) { /* Ctrl+O */
                files_editor_begin_selected();
            } else if (key_code == 0x2EU) { /* Ctrl+C */
                files_editor_begin_copy();
            } else if (key_code == 0x13U) { /* Ctrl+R */
                files_editor_begin_rename();
            } else {
                return;
            }
            draw_shell();
            draw_pointer();
            return;
        }
        if (active_view == DESKTOP_FILES &&
            files_editor_state != FILES_EDITOR_CLOSED) {
            if (files_editor_state == FILES_EDITOR_CONTENT &&
                key_code == 0x1FU) { /* Ctrl+S */
                const struct nexus_input_event save_event = {
                    .type = NEXUS_INPUT_KEYBOARD, .scancode = 0x44U
                };
                files_editor_handle_key(&save_event);
                draw_shell();
                draw_pointer();
                return;
            }
            /* Do not insert shortcut letters into an active edit dialog or
             * close the view while it contains an unsaved draft. */
            return;
        }
        if (key_code == 0x11U) { /* Ctrl+W closes the focused window */
            desktop_close_focused_window();
            start_menu_open = false;
            desktop_animate_view();
            draw_shell();
            draw_pointer();
            return;
        }
    }
    if (keyboard_alt && key_code == 0x3EU &&
        active_view != DESKTOP_INSTALLER &&
        !(active_view == DESKTOP_FILES &&
          files_editor_state != FILES_EDITOR_CLOSED)) { /* Alt+F4 */
        desktop_close_focused_window();
        start_menu_open = false;
        desktop_animate_view();
        draw_shell();
        draw_pointer();
        return;
    }
    /* Alt+Tab now cycles the actual open-window z-order. */
    if (keyboard_alt && key_code == 0x0FU &&
        active_view != DESKTOP_INSTALLER &&
        desktop_window_manager_ready) {
        const size_t count = nexus_wm_count();
        const nexus_window_id current = nexus_wm_focused();
        if (count > 1U) {
            size_t current_position = count - 1U;
            for (size_t i = 0U; i < count; ++i) {
                nexus_window_id id = 0U;
                if (nexus_wm_z_order(i, &id) && id == current) {
                    current_position = i;
                    break;
                }
            }
            const bool reverse = keyboard_left_shift || keyboard_right_shift;
            for (size_t step = 1U; step <= count; ++step) {
                const size_t next_position = reverse
                    ? (current_position + count - (step % count)) % count
                    : (current_position + step) % count;
                nexus_window_id next = 0U;
                struct nexus_window_info info;
                if (nexus_wm_z_order(next_position, &next) &&
                    next != current && nexus_wm_get(next, &info) &&
                    info.workspace == nexus_wm_active_workspace() &&
                    info.application_id >= (uint32_t)DESKTOP_FILES &&
                    info.application_id <= (uint32_t)DESKTOP_CALCULATOR) {
                    (void)nexus_wm_restore(next);
                    (void)nexus_wm_focus(next);
                    desktop_sync_focused_window();
                    if (active_view == DESKTOP_FILES) {
                        scan_files_once();
                        refresh_files_listing();
                    }
                    break;
                }
            }
        }
        start_menu_open = false;
        desktop_animate_view();
        draw_shell();
        draw_pointer();
        return;
    }
    /* F11 toggles the active app between its normal centered frame and
     * the available desktop work area, matching a familiar desktop shortcut. */
    if (active_view != DESKTOP_INSTALLER && event->scancode == 0x57U) {
        const nexus_window_id focused = nexus_wm_focused();
        struct nexus_window_info info;
        if (focused != 0U && nexus_wm_get(focused, &info)) {
            if ((info.flags & NEXUS_WINDOW_MAXIMIZED) != 0U)
                (void)nexus_wm_unmaximize(focused);
            else
                (void)nexus_wm_maximize(focused);
            desktop_sync_focused_window();
            (void)desktop_save_settings();
        }
        start_menu_open = false;
        draw_shell();
        draw_pointer();
        return;
    }
    if (active_view != DESKTOP_INSTALLER && event->scancode == 0x40U) {
        start_menu_open = !start_menu_open;
        if(start_menu_open) desktop_launcher_reset();
        draw_shell();
        draw_pointer();
        return;
    }
    if (start_menu_open && active_view != DESKTOP_INSTALLER) {
        bool changed=false;
        if (event->scancode == 0x01U) {
            start_menu_open = false;
            desktop_launcher_reset();
        } else if (event->scancode == 0x1CU) {
            desktop_launcher_open_selected();
        } else if (event->scancode == 0x48U) {
            if(launcher_visible_count!=0U && launcher_selected_index>0U)
                --launcher_selected_index;
        } else if (event->scancode == 0x50U) {
            if(launcher_visible_count!=0U &&
               launcher_selected_index+1U<launcher_visible_count)
                ++launcher_selected_index;
        } else if (event->scancode == 0x0EU) {
            size_t length=0U;
            while(length<sizeof(launcher_search)-1U &&
                  launcher_search[length]!='\0') ++length;
            if(length!=0U) {
                launcher_search[length-1U]='\0';
                launcher_selected_index=0U;
                desktop_launcher_refresh();
            }
        } else if (event->scancode >= 0x02U && event->scancode <= 0x08U) {
            desktop_open_app((size_t)(event->scancode - 0x02U));
        } else {
            const char ch=terminal_scancode_char(event->scancode);
            size_t length=0U;
            while(length<sizeof(launcher_search)-1U &&
                  launcher_search[length]!='\0') ++length;
            if(ch>=0x20 && ch<=0x7E &&
               length<sizeof(launcher_search)-1U) {
                launcher_search[length]=ch;
                launcher_search[length+1U]='\0';
                launcher_selected_index=0U;
                desktop_launcher_refresh();
                changed=true;
            }
        }
        if(changed) desktop_launcher_refresh();
        draw_shell();
        draw_pointer();
        return;
    }
    if (active_view == DESKTOP_INSTALLER) {
        if ((event->scancode & 0x80U) != 0U ||
            installer_ui_state == INSTALLER_UI_RUNNING) return;
        if (event->scancode == 0x01U) {
            if (installer_ui_state == INSTALLER_UI_SELECT) {
                /* A live installer boot is a dedicated environment, not a
                 * desktop session with an installer window to dismiss. */
                if (!installer_environment) desktop_return_home();
            } else {
                installer_ui_state = INSTALLER_UI_SELECT;
                                installer_set_status("Select a supported target disk");
            }
        } else if (installer_ui_state == INSTALLER_UI_SELECT) {
            if (event->scancode == 0x3BU) {
                installer_firmware = NEXUS_FIRMWARE_UEFI;
                installer_refresh_plan();
            } else if (event->scancode == 0x3CU) {
                installer_firmware = NEXUS_FIRMWARE_LEGACY_BIOS;
                installer_refresh_plan();
            } else if (event->scancode == 0x3DU) {
                installer_firmware = NEXUS_FIRMWARE_DUAL;
                installer_refresh_plan();
            } else if (event->scancode == 0x48U && blockdev_count() > 0U) {
                if (installer_selected_device > 0U) --installer_selected_device;
                installer_refresh_plan();
                installer_log_selected_target();
            } else if (event->scancode == 0x50U && blockdev_count() > 0U) {
                if (installer_selected_device + 1U < blockdev_count())
                    ++installer_selected_device;
                installer_refresh_plan();
                installer_log_selected_target();
            } else if (event->scancode == 0x1CU) {
                const struct nexus_block_device *device =
                    blockdev_at(installer_selected_device);
                size_t id_length = 0U;
                if (device != NULL) {
                    while (id_length < sizeof(installer_confirmed_id) &&
                           device->stable_id[id_length] != '\0')
                        ++id_length;
                }
                if (device != NULL && installer_plan_valid &&
                    id_length > 0U &&
                    id_length < sizeof(installer_confirmed_id) &&
                    device->stable_id[id_length] == '\0' &&
                    blockdev_boot_media_detection_complete() &&
                    !blockdev_is_boot_media(device) &&
                    !blockdev_is_mounted(device) &&
                    blockdev_identity_is_unique(device) &&
                    blockdev_install_write_supported(device)) {
                    for (size_t i = 0U; i <= id_length; ++i)
                        installer_confirmed_id[i] = device->stable_id[i];
                    installer_ui_state = INSTALLER_UI_CONFIRM_ERASE;
                                        installer_set_status(
                        "Review the selected drive; nothing has been written");
                } else {
                    installer_set_status("Target is not eligible for installation");
                }
            } else {
                return;
            }
        } else if (installer_ui_state == INSTALLER_UI_CONFIRM_ERASE) {
            if (event->scancode == 0x1CU) {
                installer_start_install();
            } else {
                return;
            }
        } else if (installer_ui_state == INSTALLER_UI_COMPLETE ||
                   installer_ui_state == INSTALLER_UI_FAILED) {
            if (event->scancode == 0x1CU && installer_ui_state == INSTALLER_UI_COMPLETE) {
                desktop_reboot_after_install();
            } else if (event->scancode == 0x1CU) {
                installer_ui_state = INSTALLER_UI_SELECT;
                                installer_set_status("Select a supported target disk");
                installer_refresh_plan();
            } else {
                return;
            }
        }
        draw_shell();
        draw_pointer();
        return;
    }
    if (active_view != DESKTOP_INSTALLER &&
        event->scancode >= 0x3BU && event->scancode <= 0x3EU) {
        desktop_open_app((size_t)(event->scancode - 0x3BU));
        draw_shell();
        draw_pointer();
        return;
    }
    if (active_view == DESKTOP_FILES &&
        files_editor_state == FILES_EDITOR_CLOSED &&
        !files_search_active && files_vfs_ready &&
        (event->scancode == 0x3FU || event->scancode == 0x53U)) {
        /* F5 refreshes the current folder. Delete opens the existing
         * explicit Y/N confirmation instead of removing data immediately. */
        if (event->scancode == 0x3FU) {
            refresh_files_listing();
            serial_write("FILES: listing refreshed by F5\r\n");
        } else if (!files_delete_pending) {
            files_request_delete_selected();
        }
        draw_shell();
        draw_pointer();
        return;
    }
    if (active_view == DESKTOP_CONTROL_CENTER) {
        if(event->scancode==0x01U) desktop_return_home();
        else if(event->scancode>=0x02U && event->scancode<=0x06U)
            desktop_control_center_activate((size_t)(event->scancode-0x02U));
        else if(event->scancode==0x1CU) desktop_control_center_activate(5U);
        else return;
        draw_shell(); draw_pointer(); return;
    }
    if (active_view == DESKTOP_CALCULATOR) {
        if (event->scancode==0x01U) desktop_return_home();
        else if (event->scancode==0x0EU) calculator_backspace();
        else if (event->scancode==0x1CU) calculator_evaluate();
        else if (event->scancode==0x2EU) calculator_clear();
        else {
            const char ch=terminal_scancode_char(event->scancode);
            if (ch=='=') calculator_evaluate();
            else calculator_append(ch);
        }
        draw_shell(); draw_pointer(); return;
    }
    if (active_view == DESKTOP_MONITOR) {
        if (event->scancode == 0x01U) desktop_return_home();
        draw_shell();
        draw_pointer();
        return;
    }
    if (active_view == DESKTOP_SETTINGS) {
        if (event->scancode == 0x01U) {
            desktop_return_home();
        } else if (event->scancode >= 0x02U &&
                   event->scancode <= 0x08U) {
            desktop_settings_activate((size_t)(event->scancode - 0x02U));
        } else {
            return;
        }
        draw_shell();
        draw_pointer();
        return;
    }
    if (active_view == DESKTOP_BROWSER) {
        if ((event->scancode & 0x80U) != 0U) return;
        if (browser_has_page && !browser_download_pending &&
            event->scancode == 0x49U) { /* Page Up */
            browser_scroll_offset = browser_scroll_offset > 4U
                ? browser_scroll_offset - 4U : 0U;
            draw_shell(); draw_pointer(); return;
        }
        if (browser_has_page && !browser_download_pending &&
            event->scancode == 0x51U) { /* Page Down */
            if (browser_scroll_offset + 4U < browser_line_count)
                browser_scroll_offset += 4U;
            draw_shell(); draw_pointer(); return;
        }
        if (!browser_download_pending && keyboard_alt &&
            event->scancode >= 0x02U &&
            event->scancode <= 0x06U) {
            const size_t source = (size_t)(event->scancode - 0x02U);
            if (source < browser_source_count) {
                size_t length = 0U;
                while (browser_sources[source][length] != '\0' &&
                       length + 1U < sizeof(browser_address)) {
                    browser_address[length] = browser_sources[source][length];
                    ++length;
                }
                browser_address[length] = '\0';
                browser_address_length = length;
                browser_navigate();
            } else {
                browser_set_status("No source link at that shortcut");
            }
            draw_shell(); draw_pointer(); return;
        }
        if (browser_download_pending) {
            if (event->scancode == 0x01U) {
                browser_download_pending = false;
                browser_set_status("Download cancelled");
            } else if (event->scancode == 0x1CU) {
                browser_download_pending = false;
                char saved_name[NEXUS_VFS_NAME_MAX] = {0};
                browser_set_status("Downloading over HTTP...");
                const enum nexus_browser_download_result result =
                    nexus_browser_download_http(browser_current_url,
                        saved_name, sizeof(saved_name));
                switch (result) {
                    case NEXUS_BROWSER_DOWNLOAD_OK:
                        browser_show_notice("DOWNLOAD SAVED TO NEXUS DATA",
                                            saved_name);
                        browser_set_status("Saved file read-back verification passed");
                        break;
                    case NEXUS_BROWSER_DOWNLOAD_HTTPS_BLOCKED:
                        browser_set_status("HTTPS download blocked: verified TLS is not implemented");
                        browser_show_notice("SECURE DOWNLOAD UNAVAILABLE",
                                            "NEXUS REQUIRES VERIFIED TLS");
                        break;
                    case NEXUS_BROWSER_DOWNLOAD_NOT_PERSISTENT:
                        browser_set_status("Persistent Nexus Data storage is unavailable");
                        browser_show_notice("DOWNLOAD NOT SAVED",
                                            "RETURN TO FILES ROOT; MOUNT WRITABLE DATA");
                        break;
                    case NEXUS_BROWSER_DOWNLOAD_FILE_EXISTS:
                        browser_set_status("File already exists; Nexus will not overwrite it");
                        browser_show_notice("FILE ALREADY EXISTS",
                                            "RENAME OR MOVE THE EXISTING FILE FIRST");
                        break;
                    case NEXUS_BROWSER_DOWNLOAD_CHUNKED_UNSUPPORTED:
                        browser_set_status("Server uses chunked transfer; decoder is not ready");
                        browser_show_notice("DOWNLOAD FORMAT NOT SUPPORTED",
                                            "SERVER SENT CHUNKED TRANSFER DATA");
                        break;
                    case NEXUS_BROWSER_DOWNLOAD_LENGTH_REQUIRED:
                        browser_set_status("Server omitted Content-Length");
                        browser_show_notice("DOWNLOAD SIZE UNKNOWN",
                                            "NEXUS NEEDS A VERIFIED FILE LENGTH");
                        break;
                    case NEXUS_BROWSER_DOWNLOAD_TOO_LARGE:
                        browser_set_status("Download exceeds the current 1 MiB limit");
                        browser_show_notice("FILE TOO LARGE",
                                            "CURRENT DOWNLOAD LIMIT IS 1 MIB");
                        break;
                    case NEXUS_BROWSER_DOWNLOAD_INCOMPLETE:
                        browser_set_status("Download ended early or timed out");
                        browser_show_notice("DOWNLOAD INCOMPLETE",
                                            "NO PARTIAL FILE WAS SAVED");
                        break;
                    case NEXUS_BROWSER_DOWNLOAD_REDIRECT:
                        browser_set_status("Download redirected; follow the URL manually");
                        browser_show_notice("REDIRECT NOT FOLLOWED",
                                            "HTTPS REDIRECTS REQUIRE VERIFIED TLS");
                        break;
                    case NEXUS_BROWSER_DOWNLOAD_DNS_FAILED:
                        browser_set_status("Download DNS lookup failed");
                        break;
                    case NEXUS_BROWSER_DOWNLOAD_CONNECT_FAILED:
                        browser_set_status("Download TCP connection failed");
                        break;
                    case NEXUS_BROWSER_DOWNLOAD_VERIFY_FAILED:
                        browser_set_status("Saved bytes failed read-back verification");
                        browser_show_notice("DOWNLOAD VERIFICATION FAILED",
                                            "THE INVALID FILE WAS REMOVED");
                        break;
                    case NEXUS_BROWSER_DOWNLOAD_SAVE_FAILED:
                        browser_set_status("Could not write the downloaded file");
                        break;
                    case NEXUS_BROWSER_DOWNLOAD_REQUEST_FAILED:
                        browser_set_status("Could not send the download request");
                        break;
                    case NEXUS_BROWSER_DOWNLOAD_HEADER_FAILED:
                        browser_set_status("Invalid or unsuccessful download response");
                        break;
                    default:
                        browser_set_status("Invalid download URL");
                        break;
                }
            } else {
                return;
            }
            draw_shell(); draw_pointer(); return;
        }
        if (keyboard_ctrl && event->scancode == 0x1FU) {
            if (browser_current_url[0] == '\0') {
                browser_set_status("Load an HTTP page before saving a file");
            } else {
                browser_download_pending = true;
                browser_show_notice("SAVE THIS FILE TO NEXUS DATA?",
                                    "HTTP IS NOT ENCRYPTED. ENTER SAVES; ESC CANCELS.");
                browser_set_status("Review HTTP warning; Enter to save or Esc to cancel");
            }
            draw_shell(); draw_pointer(); return;
        }
        if (event->scancode == 0x01U) {
            desktop_return_home();
            draw_shell(); draw_pointer(); return;
        }
        if (event->scancode == 0x1CU) {
            browser_navigate();
        } else if (event->scancode == 0x0EU) {
            if (browser_address_length > 0U)
                browser_address[--browser_address_length] = '\0';
        } else {
            const char ch = terminal_scancode_char(event->scancode);
            if (ch != '\0' && browser_address_length < sizeof(browser_address)-1U) {
                browser_address[browser_address_length++] = ch;
                browser_address[browser_address_length] = '\0';
            }
        }
        draw_shell(); draw_pointer(); return;
    }
    if (active_view == DESKTOP_TERMINAL) {
        if ((event->scancode & 0x80U) != 0U) return;
        if (event->scancode == 0x1CU) {
            terminal_execute();
        } else if (event->scancode == 0x0EU) {
            if (terminal_input_length > 0U)
                terminal_input[--terminal_input_length] = '\0';
        } else {
            const char ch = terminal_scancode_char(event->scancode);
            if (ch != '\0' && terminal_input_length < sizeof(terminal_input)-1U) {
                terminal_input[terminal_input_length++] = ch;
                terminal_input[terminal_input_length] = '\0';
            }
        }
        draw_shell(); draw_pointer(); return;
    }
    if (active_view == DESKTOP_FILES && files_editor_state != FILES_EDITOR_CLOSED) {
        files_editor_handle_key(event);
        draw_shell();
        draw_pointer();
        return;
    }
    if (active_view == DESKTOP_FILES &&
        (event->scancode == 0x01U || event->scancode == 0x0EU) &&
        vfs_directory_depth() != 0U && !files_search_active) {
        if (vfs_parent_directory()) refresh_files_listing();
        draw_shell();
        draw_pointer();
        return;
    }
    if (active_view == DESKTOP_FILES && files_vfs_ready) {
        if (files_search_active) {
            if ((event->scancode & 0x80U) != 0U) return;
            if (event->scancode == 0x01U) {
                files_search_active = false;
            } else if (event->scancode == 0x1CU) {
                files_search_active = false;
                for (size_t i = 0U; i < sizeof(files_search_query); ++i)
                    files_search_query[i] = files_search_input[i];
                refresh_files_listing();
            } else if (event->scancode == 0x0EU) {
                size_t length = 0U;
                while (length < sizeof(files_search_input) - 1U &&
                       files_search_input[length] != '\0') ++length;
                if (length != 0U) files_search_input[length - 1U] = '\0';
            } else {
                const char ch = terminal_scancode_char(event->scancode);
                size_t length = 0U;
                while (length < sizeof(files_search_input) - 1U &&
                       files_search_input[length] != '\0') ++length;
                if (ch >= 0x20 && ch <= 0x7E &&
                    length < sizeof(files_search_input) - 1U) {
                    files_search_input[length] = ch >= 'A' && ch <= 'Z'
                        ? (char)(ch - 'A' + 'a') : ch;
                    files_search_input[length + 1U] = '\0';
                }
            }
            draw_shell();
            draw_pointer();
            return;
        }
        if (files_delete_pending) {
            if ((event->scancode & 0x80U) != 0U) return;
            if (event->scancode == 0x15U) {
                files_confirm_delete();
            } else if (event->scancode == 0x31U ||
                       event->scancode == 0x01U) {
                files_delete_pending = false;
                files_delete_name[0] = '\0';
            }
            draw_shell();
            draw_pointer();
            return;
        }
        if ((event->scancode & 0x80U) != 0U) return;
        if (event->scancode == 0x35U) {
            files_search_active = true;
            for (size_t i = 0U; i < sizeof(files_search_query); ++i)
                files_search_input[i] = files_search_query[i];
            draw_shell(); draw_pointer(); return;
        }
        if (event->scancode == 0x31U) {
            files_editor_begin_new();
            draw_shell(); draw_pointer(); return;
        }
        if (event->scancode == 0x12U) {
            files_editor_begin_selected();
            draw_shell(); draw_pointer(); return;
        }
        if (event->scancode == 0x13U || event->scancode == 0x3CU) {
            /* R remains available; F2 matches the familiar File Explorer
             * rename shortcut. Set-1 F2 make code is 0x3C. */
            files_editor_begin_rename();
            draw_shell(); draw_pointer(); return;
        }
        if (event->scancode == 0x2EU) {
            files_editor_begin_copy();
            draw_shell(); draw_pointer(); return;
        }
        if (event->scancode == 0x48U && files_listing_count > 0U) {
            if (files_selected_index > 0U) --files_selected_index;
            files_listing_ensure_selection_visible();
            draw_shell();
            draw_pointer();
            return;
        }
        if (event->scancode == 0x50U && files_listing_count > 0U) {
            if (files_selected_index + 1U < files_listing_count)
                ++files_selected_index;
            files_listing_ensure_selection_visible();
            draw_shell();
            draw_pointer();
            return;
        }
        if (event->scancode == 0x49U && files_listing_count > 0U) {
            files_listing_page(-1);
            draw_shell();
            draw_pointer();
            return;
        }
        if (event->scancode == 0x51U && files_listing_count > 0U) {
            files_listing_page(1);
            draw_shell();
            draw_pointer();
            return;
        }
        if (event->scancode == 0x47U && files_listing_count > 0U) {
            files_selected_index = 0U;
            files_listing_ensure_selection_visible();
            draw_shell();
            draw_pointer();
            return;
        }
        if (event->scancode == 0x4FU && files_listing_count > 0U) {
            files_selected_index = files_listing_count - 1U;
            files_listing_ensure_selection_visible();
            draw_shell();
            draw_pointer();
            return;
        }
        if (event->scancode == 0x53U &&
            files_selected_index < files_listing_count) {
            files_request_delete_selected();
            draw_shell();
            draw_pointer();
            return;
        }
        if (event->scancode == 0x1CU && files_selected_index < files_listing_count) {
            struct nexus_vfs_dirent selected;
            if (files_selected_entry(&selected) &&
                selected.type == NEXUS_VFS_DIRECTORY) {
                if (vfs_enter_directory(selected.name)) {
                    refresh_files_listing();
                    files_preview[0] = '\0';
                    files_first_name[0] = '\0';
                }
            } else if (files_selected_entry(&selected) &&
                       selected.type == NEXUS_VFS_FILE) {
                struct nexus_vfs_file file;
                if (vfs_open_readonly(selected.name, &file)) {
                    uint8_t preview_data[sizeof(files_preview) - 1U];
                    size_t bytes_read = 0;
                    if (vfs_read(&file, preview_data, sizeof(preview_data),
                                 &bytes_read)) {
                        for (size_t n = 0; n < bytes_read; ++n) {
                            const uint8_t ch = preview_data[n];
                            files_preview[n] =
                                (ch >= 0x20U && ch <= 0x7EU) ? (char)ch : ' ';
                        }
                        files_preview[bytes_read] = '\0';
                        size_t name_length = 0;
                        while (name_length < sizeof(files_first_name) - 1U &&
                               selected.name[name_length] != '\0') {
                            files_first_name[name_length] = selected.name[name_length];
                            ++name_length;
                        }
                        files_first_name[name_length] = '\0';
                    }
                    vfs_close(&file);
                }
            }
            draw_shell();
            draw_pointer();
            return;
        }
    }
    enum desktop_view next_view = active_view;
    switch (event->scancode) {
        case 0x3BU: next_view = DESKTOP_FILES; break;
        case 0x3CU: next_view = DESKTOP_BROWSER; break;
        case 0x3DU: next_view = DESKTOP_SETTINGS; break;
        case 0x3EU: next_view = DESKTOP_TERMINAL; break;
        case 0x3FU: next_view = DESKTOP_INSTALLER; break;
        case 0x01U: next_view = DESKTOP_HOME; break;
        default: return;
    }
    /* The installer is a live-media-only environment. Guard this
     * keyboard path as well as desktop_open_view(), so a function-key
     * shortcut can never expose installer UI from an installed system. */
    if (next_view == DESKTOP_INSTALLER && !installer_environment) return;
    if (next_view == DESKTOP_FILES) {
        scan_files_once();
        refresh_files_listing();
    }
    if (next_view == DESKTOP_INSTALLER) {
        installer_refresh_plan();
        const struct nexus_block_device *target =
            blockdev_at(installer_selected_device);
        if (target != NULL) {
            serial_write("NEXUS INSTALLER TARGET: ");
            serial_write(target->stable_id);
            serial_write("\r\n");
            installer_log_selected_target();
        }
    }
    if (next_view == active_view) return;
    active_view = next_view;
    desktop_animate_view();
    serial_write("DESKTOP: view changed by keyboard shortcut\r\n");
}


enum nexus_desktop_process_test_result
desktop_run_process_selftest(void) {
    if (!vfs_root_mounted()) scan_files_once();
    if (!vfs_root_mounted()) {
        serial_write("DESKTOP: HELLO.ELF self-test skipped; no mounted volume\r\n");
        return NEXUS_DESKTOP_PROCESS_TEST_SKIPPED;
    }

    struct nexus_process_result result;
    enum nexus_process_error error = NEXUS_PROCESS_ERROR_NONE;
    if (!process_execute_file("HELLO.ELF", &result, &error)) {
        serial_write("DESKTOP: HELLO.ELF execution failed; error=");
        serial_write_hex((uint64_t)error);
        serial_write("\r\n");
        return NEXUS_DESKTOP_PROCESS_TEST_FAILED;
    }

    serial_write("DESKTOP: HELLO.ELF exit status=");
    serial_write_hex(result.exit_status);
    serial_write("\r\n");
    return result.exit_status == 7U
        ? NEXUS_DESKTOP_PROCESS_TEST_PASSED
        : NEXUS_DESKTOP_PROCESS_TEST_FAILED;
}
