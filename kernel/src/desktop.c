#include "desktop.h"

#include "blockdev.h"
#include "gpt.h"
#include "fat32.h"
#include "vfs.h"
#include "file_list.h"
#include "process.h"
#include "net.h"
#include "http.h"
#include "html_text.h"
#include "nxfs.h"

#include "graphics.h"
#include "input.h"
#include "serial.h"
#include "memory.h"
#include "timer.h"
#include "text_buffer.h"
#include "partition_plan.h"
#include "installer_runtime.h"

#include <stddef.h>
#include <stdint.h>

enum desktop_view {
    DESKTOP_HOME = 0,
    DESKTOP_FILES,
    DESKTOP_BROWSER,
    DESKTOP_SETTINGS,
    DESKTOP_TERMINAL,
    DESKTOP_INSTALLER
};

static enum desktop_view active_view = DESKTOP_HOME;
static uint64_t screen_width;
static uint64_t screen_height;
static bool desktop_ready;
static bool start_menu_open;
static bool window_maximized;
static bool window_positioned;
static uint64_t window_position_x;
static uint64_t window_position_y;
static bool window_dragging;
static uint64_t window_drag_offset_x;
static uint64_t window_drag_offset_y;
static bool mouse_left_was_down;
static bool keyboard_left_shift;
static bool keyboard_right_shift;
static bool keyboard_caps_lock;
static bool keyboard_caps_lock_down;
static void draw_pointer(void);
static const char *installer_transport_label(
    const struct nexus_block_device *device);
static void installer_draw_button(uint64_t x, uint64_t y,
                                  uint64_t width, uint64_t height,
                                  const char *label, bool selected,
                                  bool warning);
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
#define FILES_EDITOR_MAX_BYTES NEXUS_VFS_RAM_MAX_FILE_SIZE
#define FILES_EDITOR_VIEW_BYTES 256U

#define FILES_LISTING_MAX_ENTRIES 128U
#define FILES_LISTING_VISIBLE 6U

static char files_first_name[NEXUS_VFS_NAME_MAX];
static char files_preview[41];
static struct nexus_file_list_item files_listing[FILES_LISTING_MAX_ENTRIES];
static enum nexus_file_sort_mode files_sort_mode = NEXUS_FILE_SORT_NAME;
static size_t files_listing_count;
static size_t files_selected_index;
static size_t files_listing_first_index;
static bool files_listing_truncated;
enum files_editor_state {
    FILES_EDITOR_CLOSED = 0,
    FILES_EDITOR_NAME,
    FILES_EDITOR_DIRECTORY_NAME,
    FILES_EDITOR_RENAME,
    FILES_EDITOR_COPY,
    FILES_EDITOR_MOVE,
    FILES_EDITOR_CONTENT
};
static enum files_editor_state files_editor_state;
static char files_edit_name[NEXUS_VFS_NAME_MAX];
static char files_operation_source[NEXUS_VFS_NAME_MAX];
static char files_edit_content[FILES_EDITOR_MAX_BYTES + 1U];
static size_t files_edit_name_length;
static size_t files_edit_content_length;
static size_t files_edit_cursor;
static size_t files_edit_view_offset;
static bool files_edit_follow_end;
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
    INSTALLER_UI_CONFIRM_ID,
    INSTALLER_UI_CONFIRM_ERASE,
    INSTALLER_UI_RUNNING,
    INSTALLER_UI_COMPLETE,
    INSTALLER_UI_FAILED
};
static enum installer_ui_state installer_ui_state = INSTALLER_UI_SELECT;
static char installer_confirmed_id[NEXUS_BLOCKDEV_ID_MAX];
static char installer_input[64];
static size_t installer_input_length;
static char installer_status_text[96] = "Select a supported target disk";
/* Destructive installation remains compile-time locked until end-to-end
 * disposable-disk validation is recorded. */

#define NEXUS_BROWSER_LINE_COUNT 48U
#define NEXUS_BROWSER_VISIBLE_LINES 9U
#define NEXUS_BROWSER_LINE_WIDTH 72U
static char browser_address[96];
static size_t browser_address_length;
static char browser_status[80] = "Enter host/path; HTTP only, no HTTPS";
static char browser_lines[NEXUS_BROWSER_LINE_COUNT][NEXUS_BROWSER_LINE_WIDTH];
static size_t browser_line_count;
static size_t browser_line_scroll;
static bool browser_content_truncated;

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
        default: return DESKTOP_INSTALLER;
    }
}

static void desktop_open_view(enum desktop_view view) {
    active_view = view;
    start_menu_open = false;
    window_positioned = false;
    window_dragging = false;
    if (view == DESKTOP_FILES) {
        scan_files_once();
        refresh_files_listing();
    } else if (view == DESKTOP_INSTALLER) {
        installer_refresh_plan();
        installer_log_selected_target();
    }
}

static void desktop_open_app(size_t app_index) {
    if (app_index >= 5U) return;
    desktop_open_view(desktop_view_for_app(app_index));
}

static void desktop_draw_taskbar(uint64_t bottom_bar) {
    const uint64_t y = screen_height - bottom_bar;
    graphics_fill_rect(0U, y, screen_width, bottom_bar, 10U, 16U, 31U);
    graphics_fill_rect(0U, y, screen_width, 2U, 47U, 91U, 133U);
    graphics_fill_rect(0U, y + 2U, screen_width, 1U, 24U, 43U, 66U);

    const uint64_t button_y = y + 7U;
    const uint64_t button_h = bottom_bar > 14U ? bottom_bar - 14U : 22U;
    graphics_fill_rect(12U, button_y, 88U, button_h, 28U, 57U, 88U);
    graphics_draw_rect(12U, button_y, 88U, button_h, 57U, 112U, 151U);
    graphics_fill_rect(22U, button_y + 7U, 22U, 22U, 37U, 180U, 204U);
    graphics_draw_text(27U, button_y + 14U, "N", 1U, 8U, 25U, 43U);
    graphics_draw_text(51U, button_y + button_h / 2U - 4U,
                       "START", 1U, 239U, 247U, 255U);

    static const char *const labels[5] = {
        "FILES", "WEB", "SETTINGS", "TERMINAL", "INSTALL"
    };
    static const uint8_t accents[5][3] = {
        {45U, 177U, 202U}, {117U, 136U, 244U}, {82U, 198U, 155U},
        {217U, 164U, 83U}, {239U, 177U, 119U}
    };
    const uint64_t tab_width = 72U;
    const uint64_t tab_step = 78U;
    for (size_t i = 0U; i < 5U; ++i) {
        const uint64_t x = 110U + (uint64_t)i * tab_step;
        if (x + tab_width >= screen_width - 92U) break;
        const bool selected = active_view == desktop_view_for_app(i);
        graphics_fill_rect(x, button_y, tab_width, button_h,
                           selected ? 37U : 19U,
                           selected ? 57U : 31U,
                           selected ? 80U : 49U);
        graphics_draw_rect(x, button_y, tab_width, button_h,
                           selected ? 70U : 43U,
                           selected ? 143U : 70U,
                           selected ? 176U : 99U);
        graphics_fill_rect(x + 8U, button_y + 9U, 5U,
                           button_h > 28U ? button_h - 18U : 10U,
                           accents[i][0], accents[i][1], accents[i][2]);
        graphics_draw_text(x + 18U, button_y + button_h / 2U - 4U,
                           labels[i], 1U, 220U, 235U, 247U);
        if (selected)
            graphics_fill_rect(x + 8U, y + bottom_bar - 3U,
                               tab_width - 16U, 2U,
                               accents[i][0], accents[i][1], accents[i][2]);
    }

    char uptime[21];
    format_u64(timer_ticks() / 100U, uptime);
    const uint64_t status_x = screen_width > 118U ? screen_width - 112U : 8U;
    graphics_draw_text(status_x, button_y + 5U, "UPTIME", 1U,
                       122U, 155U, 183U);
    graphics_draw_text(status_x, button_y + 21U, uptime, 1U,
                       220U, 236U, 248U);
}

static void desktop_draw_start_menu(uint64_t bottom_bar) {
    if (!start_menu_open) return;
    const uint64_t menu_width = screen_width > 340U ? 304U : screen_width - 24U;
    const uint64_t menu_height = 238U;
    uint64_t menu_x = 12U;
    uint64_t menu_y = screen_height - bottom_bar - menu_height + 4U;
    const uint64_t top_limit = clamp_min(screen_height / 16U, 34U) + 8U;
    if (menu_y < top_limit) menu_y = top_limit;

    graphics_fill_rect(menu_x + 5U, menu_y + 6U,
                       menu_width, menu_height, 2U, 5U, 15U);
    graphics_blend_round_rect(menu_x, menu_y,
                              menu_width, menu_height, 12U,
                              19U, 31U, 52U, 224U);
    graphics_round_rect(menu_x, menu_y,
                          menu_width, menu_height, 12U,
                          66U, 111U, 148U);
    graphics_round_rect(menu_x, menu_y, menu_width, 42U, 12U,
                          28U, 47U, 72U);
    graphics_fill_rect(menu_x, menu_y + 12U, menu_width, 30U,
                       28U, 47U, 72U);
    graphics_fill_rect(menu_x + 14U, menu_y + 11U, 20U, 20U,
                       37U, 180U, 204U);
    graphics_draw_text(menu_x + 20U, menu_y + 17U, "N", 1U,
                       8U, 25U, 43U);
    graphics_draw_text(menu_x + 44U, menu_y + 17U,
                       "NEXUS APP LAUNCHER", 1U,
                       239U, 247U, 255U);

    static const char *const names[5] = {
        "FILES - MANAGE YOUR DATA",
        "WEB - NETWORK TEXT BROWSER",
        "SETTINGS - SYSTEM OVERVIEW",
        "TERMINAL - COMMAND LINE",
        "INSTALL - DISK INSTALL WIZARD"
    };
    static const uint8_t accents[5][3] = {
        {45U, 177U, 202U}, {117U, 136U, 244U}, {82U, 198U, 155U},
        {217U, 164U, 83U}, {239U, 177U, 119U}
    };
    for (size_t i = 0U; i < 5U; ++i) {
        const uint64_t row_y = menu_y + 49U + (uint64_t)i * 34U;
        graphics_fill_rect(menu_x + 8U, row_y,
                           menu_width - 16U, 29U, 25U, 41U, 64U);
        graphics_fill_rect(menu_x + 16U, row_y + 7U,
                           14U, 14U, accents[i][0], accents[i][1], accents[i][2]);
        graphics_draw_text(menu_x + 40U, row_y + 11U,
                           names[i], 1U, 225U, 238U, 249U);
    }
    graphics_draw_text(menu_x + 14U, menu_y + menu_height - 12U,
                       "1-5 OPEN APPS   F6 CLOSE MENU", 1U,
                       140U, 170U, 195U);
}

static void draw_shell(void) {
    const uint64_t width = screen_width;
    const uint64_t height = screen_height;
    const uint64_t top_bar = clamp_min(height / 16U, 34U);
    const uint64_t bottom_bar = clamp_min(height / 11U, 48U);

    for (uint64_t y = 0; y < height; ++y) {
        const uint8_t red = (uint8_t)(5U + (y * 8U / height));
        const uint8_t green = (uint8_t)(11U + (y * 18U / height));
        const uint8_t blue = (uint8_t)(25U + (y * 34U / height));
        graphics_fill_rect(0U, y, width, 1U, red, green, blue);
    }
    /* Layered geometric wallpaper: deliberately native framebuffer artwork. */
    graphics_fill_rect(width / 2U, 0U, width / 2U, height,
                       9U, 26U, 49U);
    graphics_fill_rect(width * 3U / 5U, 0U, width / 3U, height,
                       10U, 31U, 58U);
    graphics_fill_rect(width * 4U / 5U, 0U, width / 5U, height,
                       12U, 35U, 63U);
    for (uint64_t band = 0U; band < 5U; ++band) {
        const uint64_t x = width / 2U + band * (width / 14U);
        if (x < width)
            graphics_fill_rect(x, top_bar, 1U, height - top_bar,
                               14U, (uint8_t)(42U + band * 3U),
                               (uint8_t)(70U + band * 4U));
    }
    graphics_fill_rect(0U, 0U, width, top_bar, 10U, 17U, 33U);
    graphics_fill_rect(0U, top_bar - 2U, width, 2U, 38U, 159U, 192U);
    graphics_fill_rect(16U, 7U, 28U, top_bar - 14U, 30U, 154U, 184U);
    graphics_draw_text(25U, top_bar / 2U - 4U, "N", 1U, 7U, 23U, 39U);
    graphics_draw_text(54U, top_bar / 2U - 4U,
                       "NEXUS  /  YOUR WORKSPACE", 1U,
                       232U, 243U, 252U);
    char top_uptime[21];
    format_u64(timer_ticks() / 100U, top_uptime);
    graphics_draw_text(width > 170U ? width - 154U : 8U,
                       top_bar / 2U - 4U, "RUNNING", 1U,
                       112U, 222U, 190U);
    graphics_draw_text(width > 102U ? width - 88U : 8U,
                       top_bar / 2U - 4U, top_uptime, 1U,
                       166U, 194U, 216U);

    uint64_t panel_width = width * 3U / 4U;
    uint64_t panel_height = active_view == DESKTOP_HOME
        ? height * 53U / 100U : 512U;
    if (panel_width > 920U) panel_width = 920U;
    if (panel_height > 512U) panel_height = 512U;
    if (panel_width > width - 16U) panel_width = width - 16U;
    if (panel_height > height - top_bar - bottom_bar - 16U)
        panel_height = height - top_bar - bottom_bar - 16U;
    if (window_maximized && active_view != DESKTOP_HOME) {
        panel_width = width - 32U;
        panel_height = height - top_bar - bottom_bar - 16U;
    }

    uint64_t panel_x = (width - panel_width) / 2U;
    uint64_t panel_y = window_maximized && active_view != DESKTOP_HOME
        ? top_bar + 8U
        : top_bar + (height - top_bar - bottom_bar - panel_height) / 2U;
    if (window_positioned && !window_maximized &&
        active_view != DESKTOP_HOME) {
        panel_x = window_position_x;
        panel_y = window_position_y;
    }

    graphics_fill_rect(panel_x + 6U, panel_y + 8U, panel_width,
                       panel_height, 1U, 4U, 13U);
    graphics_blend_round_rect(panel_x, panel_y, panel_width, panel_height,
                              12U, 17U, 28U, 47U, 224U);
    graphics_round_rect(panel_x, panel_y, panel_width, panel_height,
                          12U, 61U, 99U, 133U);
    graphics_round_rect(panel_x, panel_y, panel_width, 40U, 12U,
                          25U, 42U, 66U);
    graphics_fill_rect(panel_x, panel_y + 12U, panel_width, 28U,
                       25U, 42U, 66U);
    graphics_fill_rect(panel_x, panel_y + 39U, panel_width, 1U,
                       44U, 82U, 115U);
    graphics_fill_rect(panel_x + 9U, panel_y + 12U, 16U, 16U,
                       41U, 174U, 199U);

    const char *window_title = "NEXUS DESKTOP";
    if (active_view == DESKTOP_FILES) window_title = "FILES";
    if (active_view == DESKTOP_BROWSER) window_title = "NEXUS BROWSER";
    if (active_view == DESKTOP_SETTINGS) window_title = "SETTINGS";
    if (active_view == DESKTOP_TERMINAL) window_title = "NEXUS TERMINAL";
    if (active_view == DESKTOP_INSTALLER) window_title = "NEXUS INSTALLER";
    graphics_draw_text(panel_x + 33U, panel_y + 16U, window_title,
                       1U, 234U, 244U, 252U);
    /* Custom window controls; close is wired to the shell input handler. */
    const uint64_t controls_x = panel_x + panel_width - 66U;
    graphics_fill_rect(controls_x, panel_y + 12U, 14U, 12U,
                       49U, 69U, 91U);
    graphics_draw_text(controls_x + 4U, panel_y + 15U, "-", 1U,
                       229U, 239U, 248U);
    graphics_fill_rect(controls_x + 22U, panel_y + 12U, 14U, 12U,
                       49U, 69U, 91U);
    graphics_draw_rect(controls_x + 26U, panel_y + 15U, 6U, 6U,
                       207U, 224U, 239U);
    graphics_fill_rect(controls_x + 44U, panel_y + 12U, 14U, 12U,
                       177U, 67U, 80U);
    graphics_draw_text(controls_x + 47U, panel_y + 16U, "X", 1U,
                       255U, 239U, 241U);

    const uint64_t text_x = panel_x + 32U;
    const uint64_t text_y = panel_y +
        (active_view == DESKTOP_INSTALLER ? 48U : 82U);
    if (active_view == DESKTOP_HOME) {
        graphics_draw_text(text_x, text_y, "YOUR NEXUS WORKSPACE",
                           2U, 241U, 248U, 255U);
        graphics_draw_text(text_x + 2U, text_y + 30U,
                           "A CUSTOM DESKTOP. YOUR TOOLS. YOUR FILES.",
                           1U, 111U, 207U, 231U);

        const uint64_t tile_y = text_y + 76U;
        uint64_t tile_width = (panel_width - 100U) / 5U;
        if (tile_width > 190U) tile_width = 190U;
        static const char *const labels[5] = {
            "FILES", "WEB", "SETTINGS", "TERMINAL", "INSTALL"
        };
        static const uint8_t accent[5][3] = {
            {45U, 177U, 202U}, {117U, 136U, 244U},
            {82U, 198U, 155U}, {217U, 164U, 83U},
            {239U, 177U, 119U}
        };
        for (uint32_t i = 0U; i < 5U; ++i) {
            const uint64_t tile_x = panel_x + 28U +
                                    (uint64_t)i * (tile_width + 10U);
            graphics_fill_rect(tile_x, tile_y, tile_width, 74U,
                               24U, 41U, 65U);
            graphics_draw_rect(tile_x, tile_y, tile_width, 74U,
                               58U, 91U, 121U);
            graphics_fill_rect(tile_x, tile_y, 4U, 74U,
                               accent[i][0], accent[i][1], accent[i][2]);
            graphics_fill_rect(tile_x + 13U, tile_y + 12U, 25U, 25U,
                               accent[i][0], accent[i][1], accent[i][2]);
            graphics_draw_text(tile_x + 20U, tile_y + 21U, "N", 1U,
                               8U, 25U, 43U);
            graphics_draw_text(tile_x + 13U, tile_y + 51U, labels[i],
                               1U, 235U, 245U, 252U);
        }

        graphics_draw_text(text_x, text_y + (panel_height < 360U ? 146U : 164U), "AT A GLANCE",
                           1U, 133U, 164U, 190U);
        const uint64_t card_gap = 10U;
        const uint64_t card_width = (panel_width - 84U) / 3U;
        const uint64_t card_y = text_y + (panel_height < 360U ? 163U : 181U);
        const uint64_t card_h = panel_height < 360U ? 44U : 54U;
        const char *card_labels[3] = {"FREE MEMORY", "UPTIME", "STORAGE"};
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
        const char *memory_suffix = " MiB FREE";
        for (size_t n = 0U; memory_suffix[n] != '\0' &&
             memory_pos < sizeof(memory_value) - 1U; ++n) {
            memory_value[memory_pos++] = memory_suffix[n];
        }
        memory_value[memory_pos] = '\0';

        char uptime_value[40] = "";
        size_t uptime_pos = 0U;
        while (uptime[uptime_pos] != '\0' &&
               uptime_pos < sizeof(uptime_value) - 1U) {
            uptime_value[uptime_pos] = uptime[uptime_pos];
            ++uptime_pos;
        }
        const char *seconds_suffix = "s";
        for (size_t n = 0U; seconds_suffix[n] != '\0' &&
             uptime_pos < sizeof(uptime_value) - 1U; ++n) {
            uptime_value[uptime_pos++] = seconds_suffix[n];
        }
        uptime_value[uptime_pos] = '\0';
        static const uint8_t card_accent[3][3] = {
            {45U, 177U, 202U}, {117U, 136U, 244U}, {82U, 198U, 155U}
        };
        for (size_t i = 0U; i < 3U; ++i) {
            const uint64_t card_x = panel_x + 28U +
                                    (uint64_t)i * (card_width + card_gap);
            graphics_fill_rect(card_x, card_y, card_width, card_h,
                               22U, 38U, 60U);
            graphics_draw_rect(card_x, card_y, card_width, card_h,
                               50U, 79U, 107U);
            graphics_fill_rect(card_x, card_y, 3U, card_h,
                               card_accent[i][0], card_accent[i][1],
                               card_accent[i][2]);
            graphics_draw_text(card_x + 11U, card_y + 13U,
                               card_labels[i], 1U, 148U, 177U, 198U);
            if (i == 0U) {
                graphics_draw_text(card_x + 11U, card_y + 34U,
                                   memory_value, 1U, 233U, 244U, 252U);
            } else if (i == 1U) {
                graphics_draw_text(card_x + 11U, card_y + 34U,
                                   uptime_value, 1U, 233U, 244U, 252U);
            } else {
                const char *storage_label =
                    vfs_nxfs_mounted() && vfs_nxfs_writable()
                        ? "NXFS READY"
                        : (vfs_root_mounted() ? "MOUNTED" : "RAM ONLY");
                graphics_draw_text(card_x + 11U, card_y + 34U,
                                   storage_label, 1U, 233U, 244U, 252U);
            }
        }
        graphics_draw_text(text_x, panel_y + panel_height - 16U,
                           "F1 FILES   F2 WEB   F3 SETTINGS   F4 TERMINAL   F5 INSTALL",
                           1U, 151U, 178U, 201U);
    } else if (active_view == DESKTOP_INSTALLER) {
        if (installer_ui_state == INSTALLER_UI_SELECT) {
            graphics_draw_text(text_x, text_y, "INSTALL NEXUS - CHOOSE A DRIVE",
                               2U, 239U, 249U, 255U);
            graphics_draw_text(text_x, text_y + 28U,
                               "Nothing is written until you confirm the target twice.",
                               1U, 108U, 216U, 229U);
            const size_t device_count = blockdev_count();
            if (device_count == 0U) {
                graphics_draw_text(text_x, text_y + 88U,
                                   "NO SUPPORTED STORAGE DEVICES DETECTED",
                                   1U, 239U, 177U, 119U);
                graphics_draw_text(text_x, text_y + 118U,
                                   "Connect an internal SSD or supported USB drive.",
                                   1U, 170U, 191U, 211U);
            } else {
                const struct nexus_block_device *device =
                    blockdev_at(installer_selected_device);
                char drive_index[21], drive_total[21];
                format_u64(installer_selected_device + 1U, drive_index);
                format_u64(device_count, drive_total);
                graphics_draw_text(text_x, text_y + 54U,
                                   "SELECTED DRIVE:", 1U, 233U, 243U, 252U);
                graphics_draw_text(text_x + 140U, text_y + 54U,
                                   device != NULL ? device->name : "UNKNOWN",
                                   1U, 119U, 229U, 211U);
                graphics_draw_text(text_x + 260U, text_y + 54U,
                                   "DRIVE", 1U, 170U, 191U, 211U);
                graphics_draw_text(text_x + 300U, text_y + 54U,
                                   drive_index, 1U, 119U, 229U, 211U);
                graphics_draw_text(text_x + 312U, text_y + 54U,
                                   "OF", 1U, 170U, 191U, 211U);
                graphics_draw_text(text_x + 332U, text_y + 54U,
                                   drive_total, 1U, 119U, 229U, 211U);
                if (device != NULL) {
                    graphics_draw_text(text_x, text_y + 76U,
                                       device->model, 1U, 170U, 225U, 190U);
                    graphics_draw_text(text_x, text_y + 94U,
                                       device->stable_id, 1U, 170U, 191U, 211U);
                    graphics_draw_text(text_x + 340U, text_y + 76U,
                                       installer_transport_label(device),
                                       1U, 119U, 229U, 211U);
                    const uint64_t blocks_per_gib =
                        (1024ULL * 1024ULL * 1024ULL) / device->block_size;
                    char capacity[21], sector[21];
                    format_u64(blocks_per_gib == 0U ? 0U :
                               device->block_count / blocks_per_gib, capacity);
                    format_u64(device->block_size, sector);
                    graphics_draw_text(text_x, text_y + 116U,
                                       "CAPACITY:", 1U, 233U, 243U, 252U);
                    graphics_draw_text(text_x + 92U, text_y + 116U,
                                       capacity, 1U, 170U, 225U, 190U);
                    graphics_draw_text(text_x + 124U, text_y + 116U,
                                       "GiB", 1U, 170U, 191U, 211U);
                    graphics_draw_text(text_x + 190U, text_y + 116U,
                                       "SECTOR:", 1U, 233U, 243U, 252U);
                    graphics_draw_text(text_x + 272U, text_y + 116U,
                                       sector, 1U, 170U, 225U, 190U);
                    graphics_draw_text(text_x + 380U, text_y + 116U,
                                       blockdev_identity_is_unique(device)
                                           ? "IDENTITY VERIFIED"
                                           : "IDENTITY NOT SAFE",
                                       1U, blockdev_identity_is_unique(device)
                                           ? 108U : 239U,
                                       blockdev_identity_is_unique(device)
                                           ? 216U : 177U,
                                       blockdev_identity_is_unique(device)
                                           ? 229U : 119U);
                    graphics_draw_text(text_x, text_y + 144U,
                                       "INSTALL FOR:", 1U, 233U, 243U, 252U);
                    installer_draw_button(text_x, text_y + 158U, 104U, 24U,
                        "UEFI", installer_firmware == NEXUS_FIRMWARE_UEFI, false);
                    installer_draw_button(text_x + 112U, text_y + 158U, 104U, 24U,
                        "BIOS", installer_firmware == NEXUS_FIRMWARE_LEGACY_BIOS, false);
                    installer_draw_button(text_x + 224U, text_y + 158U, 112U, 24U,
                        "BOTH", installer_firmware == NEXUS_FIRMWARE_DUAL, false);
                    if (installer_plan_valid) {
                        graphics_draw_text(text_x, text_y + 190U,
                                           "INSTALL LAYOUT - PREVIEW ONLY",
                                           1U, 119U, 229U, 211U);
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
                                    ? "RECOVERY" : "USER DATA")));
                            const uint64_t sectors_per_mib =
                                (1024ULL * 1024ULL) / device->block_size;
                            const uint64_t size_mib =
                                (part->last_lba - part->first_lba + 1U) /
                                sectors_per_mib;
                            char size_text[21];
                            format_u64(size_mib, size_text);
                            const uint64_t row_y = text_y + 210U + i * 16U;
                            graphics_draw_text(text_x, row_y, label,
                                               1U, 233U, 243U, 252U);
                            graphics_draw_text(text_x + 190U, row_y,
                                               size_text, 1U, 170U, 225U, 190U);
                            graphics_draw_text(text_x + 230U, row_y,
                                               "MiB", 1U, 170U, 191U, 211U);
                        }
                    } else {
                        graphics_draw_text(text_x, text_y + 210U,
                                           "THIS DRIVE CANNOT USE THAT LAYOUT",
                                           1U, 239U, 177U, 119U);
                    }
                    graphics_draw_text(text_x, text_y + 292U,
                                       installer_target_status(device), 1U,
                                       170U, 225U, 190U);
                }
                installer_draw_button(text_x, text_y + 310U, 104U, 24U,
                                      "PREVIOUS", installer_selected_device > 0U,
                                      false);
                installer_draw_button(text_x + 112U, text_y + 310U, 104U, 24U,
                                      "NEXT DRIVE",
                                      installer_selected_device + 1U < device_count,
                                      false);
                installer_draw_button(text_x + 228U, text_y + 310U, 220U, 24U,
                                      "CONTINUE", false, false);
            }
            graphics_draw_text(text_x, text_y + 348U,
                               "ARROWS OR DRIVE BUTTONS TO SELECT  |  ESC TO EXIT",
                               1U, 170U, 191U, 211U);
            graphics_draw_text(text_x, text_y + 370U,
                               "Your existing disks are not changed during this preview.",
                               1U, 170U, 191U, 211U);
        } else {
            const struct nexus_block_device *device =
                blockdev_at(installer_selected_device);
            graphics_draw_text(text_x, text_y, "REVIEW BEFORE INSTALLING",
                               2U, 239U, 249U, 255U);
            if (device != NULL) {
                graphics_draw_text(text_x, text_y + 38U, device->model,
                                   1U, 170U, 225U, 190U);
                graphics_draw_text(text_x, text_y + 56U, device->stable_id,
                                   1U, 119U, 229U, 211U);
                graphics_draw_text(text_x, text_y + 78U,
                                   installer_transport_label(device),
                                   1U, 170U, 191U, 211U);
            }
            if (installer_ui_state == INSTALLER_UI_CONFIRM_ID) {
                graphics_draw_text(text_x, text_y + 112U,
                                   "STEP 1 OF 2 - TYPE THE COMPLETE DRIVE ID",
                                   1U, 239U, 177U, 119U);
                graphics_draw_text(text_x, text_y + 140U,
                                   "This confirms that you selected the correct disk.",
                                   1U, 170U, 225U, 190U);
            } else if (installer_ui_state == INSTALLER_UI_CONFIRM_ERASE) {
                graphics_draw_text(text_x, text_y + 112U,
                                   "STEP 2 OF 2 - THIS WILL ERASE THE WHOLE DRIVE",
                                   1U, 239U, 177U, 119U);
                graphics_draw_text(text_x, text_y + 140U,
                                   "Type ERASE to authorize writing the new partition layout.",
                                   1U, 239U, 177U, 119U);
            } else if (installer_ui_state == INSTALLER_UI_RUNNING) {
                graphics_draw_text(text_x, text_y + 116U,
                                   "INSTALLING NEXUS - DO NOT POWER OFF",
                                   1U, 239U, 177U, 119U);
            } else if (installer_ui_state == INSTALLER_UI_COMPLETE) {
                graphics_draw_text(text_x, text_y + 116U,
                                   "NEXUS INSTALLATION COMPLETED",
                                   1U, 108U, 216U, 229U);
                graphics_draw_text(text_x, text_y + 146U,
                                   "Shut down, remove the installer media, then boot Nexus.",
                                   1U, 170U, 225U, 190U);
            } else if (installer_ui_state == INSTALLER_UI_FAILED) {
                graphics_draw_text(text_x, text_y + 116U,
                                   "INSTALLATION STOPPED - CHECK THE MESSAGE BELOW",
                                   1U, 239U, 177U, 119U);
            }
            graphics_draw_text(text_x, text_y + 174U, installer_status_text,
                               1U, 170U, 225U, 190U);
            if (installer_ui_state == INSTALLER_UI_CONFIRM_ID ||
                installer_ui_state == INSTALLER_UI_CONFIRM_ERASE) {
                graphics_fill_rect(text_x, text_y + 206U, 520U, 30U,
                                   11U, 20U, 39U);
                graphics_draw_rect(text_x, text_y + 206U, 520U, 30U,
                                   56U, 88U, 119U);
                graphics_draw_text(text_x + 8U, text_y + 216U,
                                   installer_input, 1U, 239U, 249U, 255U);
                installer_draw_button(text_x, text_y + 264U, 112U, 26U,
                                      "BACK", false, false);
                installer_draw_button(text_x + 128U, text_y + 264U, 172U, 26U,
                    installer_ui_state == INSTALLER_UI_CONFIRM_ID
                        ? "VERIFY DRIVE ID" : "CONFIRM ERASE",
                    false, installer_ui_state == INSTALLER_UI_CONFIRM_ERASE);
                graphics_draw_text(text_x, text_y + 306U,
                                   "Type in the field, then press Enter or click Confirm.",
                                   1U, 170U, 191U, 211U);
            } else if (installer_ui_state == INSTALLER_UI_COMPLETE ||
                       installer_ui_state == INSTALLER_UI_FAILED) {
                installer_draw_button(text_x, text_y + 264U, 210U, 26U,
                                      "RETURN TO DRIVE SELECTION", false, false);
            }
        }
    } else if (active_view == DESKTOP_FILES) {
        graphics_draw_text(text_x, text_y, "NEXUS FILES",
                           2, 239, 249, 255);
        if (files_editor_state != FILES_EDITOR_CLOSED) {
            const char *editor_title =
                files_editor_state == FILES_EDITOR_NAME
                    ? "NEW FILE - ENTER A NAME"
                    : (files_editor_state == FILES_EDITOR_DIRECTORY_NAME
                        ? "NEW FOLDER - ENTER A NAME"
                        : (files_editor_state == FILES_EDITOR_RENAME
                            ? "RENAME FILE - ENTER THE NEW NAME"
                            : (files_editor_state == FILES_EDITOR_COPY
                                ? "COPY FILE - ENTER A NEW NAME OR PATH"
                                : (files_editor_state == FILES_EDITOR_MOVE
                                    ? "MOVE FILE - ENTER A DESTINATION PATH"
                                    : "TEXT EDITOR - 4096 BYTE LIMIT")));
            graphics_draw_text(text_x, text_y + 48U, editor_title,
                               1, 108, 216, 229);
            graphics_draw_text(text_x, text_y + 82U,
                (files_editor_state == FILES_EDITOR_COPY ||
                 files_editor_state == FILES_EDITOR_MOVE)
                    ? "TARGET:"
                    : (files_editor_state == FILES_EDITOR_RENAME
                        ? "NEW NAME:" : "NAME:"),
                1, 233, 243, 252);
            graphics_draw_text(text_x + 48U, text_y + 82U, files_edit_name,
                               1, 119, 229, 211);
            if (files_editor_state == FILES_EDITOR_RENAME ||
                files_editor_state == FILES_EDITOR_COPY ||
                files_editor_state == FILES_EDITOR_MOVE) {
                graphics_draw_text(text_x, text_y + 108U, "SOURCE:",
                                   1, 233, 243, 252);
                graphics_draw_text(text_x + 56U, text_y + 108U,
                                   files_operation_source,
                                   1, 170, 191, 211);
            }
            if (files_editor_state == FILES_EDITOR_CONTENT) {
                for (size_t line = 0U; line < 4U; ++line) {
                    char display[66];
                    const size_t offset =
                        files_edit_view_offset + line * 64U;
                    size_t amount = 0U;
                    while (amount < 64U &&
                           offset + amount < files_edit_content_length) {
                        const char value = files_edit_content[offset + amount];
                        display[amount] = value == '\n' ? ' ' : value;
                        ++amount;
                    }
                    size_t display_length = amount;
                    if (files_edit_cursor >= offset &&
                        (files_edit_cursor < offset + 64U || line == 3U) &&
                        files_edit_cursor <= offset + amount &&
                        display_length < 65U) {
                        const size_t caret = files_edit_cursor - offset;
                        for (size_t i = display_length; i > caret; --i)
                            display[i] = display[i - 1U];
                        display[caret] = '|';
                        ++display_length;
                    }
                    display[display_length] = '\0';
                    graphics_draw_text(text_x, text_y + 124U + line * 28U,
                                       display, 1, 239, 249, 255);
                }
                graphics_draw_text(text_x, text_y + 260U,
                    vfs_nxfs_writable()
                        ? "SAVE TARGET: PERSISTENT NXFS"
                        : "SAVE TARGET: VOLATILE RAM ONLY",
                    1, 170, 191, 211);
                graphics_draw_text(text_x, text_y + 292U,
                    "ARROWS MOVE  HOME/END LINE  PGUP/PGDN PAGE  BKSP/DEL  F10 SAVE",
                    1, 170, 191, 211);
            } else {
                graphics_draw_text(text_x, text_y + 124U,
                    (files_editor_state == FILES_EDITOR_COPY ||
                     files_editor_state == FILES_EDITOR_MOVE)
                        ? "PATHS ARE VOLUME-ROOT RELATIVE; A SIMPLE NAME USES THIS FOLDER"
                        : (files_editor_state == FILES_EDITOR_DIRECTORY_NAME
                            ? "FOLDER NAME: LETTERS, NUMBERS, _ - AND DOT"
                            : "LETTERS, NUMBERS, _ - DOT AND / FOR PATHS"),
                    1, 170, 191, 211);
                graphics_draw_text(text_x, text_y + 156U,
                    files_editor_state == FILES_EDITOR_DIRECTORY_NAME
                        ? "ENTER CREATE FOLDER  ESC CANCEL"
                        : "ENTER CONFIRM  ESC CANCEL",
                    1, 170, 191, 211);
                if (files_editor_state == FILES_EDITOR_COPY ||
                    files_editor_state == FILES_EDITOR_MOVE)
                    graphics_draw_text(text_x, text_y + 184U,
                        "PATHS USE / AS THE DIRECTORY SEPARATOR",
                        1, 170, 191, 211);
            }
            graphics_draw_text(text_x, text_y + 340U, files_edit_status,
                               1, 239, 177, 119);
        } else {
        graphics_draw_text(text_x, text_y + 48U,
                           "STORAGE DEVICE DISCOVERY IS ACTIVE",
                           1, 108, 216, 229);
        graphics_draw_text(text_x, text_y + 78U,
                           vfs_nxfs_writable()
                               ? "NEXUS USER DATA IS PERSISTENT AND WRITABLE"
                               : "FAT32 IS READ ONLY; RAM FILES ARE VOLATILE",
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
                graphics_draw_text(text_x, text_y + 228U,
                                   "ROOT FILE:", 1, 233, 243, 252);
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
            static const char *const actions[5] = {
                "NEW FILE", "NEW FOLDER", "OPEN/EDIT", "DELETE", "REFRESH"
            };
            static const uint8_t action_colors[5][3] = {
                {37U, 139U, 165U}, {80U, 154U, 125U},
                {60U, 116U, 170U}, {153U, 69U, 78U},
                {67U, 133U, 111U}
            };
            for (size_t action = 0U; action < 5U; ++action) {
                const uint64_t action_x = text_x + (uint64_t)action * 76U;
                graphics_fill_rect(action_x, text_y + 270U, 72U, 25U,
                                   29U, 47U, 69U);
                graphics_draw_rect(action_x, text_y + 270U, 72U, 25U,
                                   action_colors[action][0],
                                   action_colors[action][1],
                                   action_colors[action][2]);
                graphics_fill_rect(action_x, text_y + 270U, 3U, 25U,
                                   action_colors[action][0],
                                   action_colors[action][1],
                                   action_colors[action][2]);
                graphics_draw_text(action_x + 5U, text_y + 279U,
                                   actions[action], 1U, 234U, 244U, 252U);
            }
            char current_path[96];
            if (!vfs_current_path(current_path, sizeof(current_path)))
                current_path[0] = '\0';
            graphics_draw_text(text_x, text_y + 304U,
                                current_path[0] != '\0'
                                    ? current_path : "PATH UNAVAILABLE",
                                1U, 108U, 216U, 229U);
            graphics_draw_text(text_x + 520U, text_y + 304U,
                                nexus_file_sort_mode_label(files_sort_mode),
                                1U, 170U, 191U, 211U);
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
                char display_name[NEXUS_VFS_NAME_MAX + 8U];
                size_t display_length = 0U;
                const char *prefix = files_listing[index].directory
                    ? "[DIR] " : "      ";
                while (prefix[display_length] != '\0') {
                    display_name[display_length] = prefix[display_length];
                    ++display_length;
                }
                for (size_t i = 0U;
                     i < NEXUS_VFS_NAME_MAX - 1U &&
                     files_listing[index].name[i] != '\0' &&
                     display_length < sizeof(display_name) - 1U; ++i)
                    display_name[display_length++] = files_listing[index].name[i];
                display_name[display_length] = '\0';
                graphics_draw_text(text_x + column * 260U,
                                   text_y + 326U + line * 24U,
                                   display_name, 1U,
                                   selected ? 119U : 233U,
                                   selected ? 229U : 243U,
                                   selected ? 211U : 252U);
            }
            if (files_listing_count == 0U) {
                graphics_draw_text(text_x, text_y + 326U, "NO FILES",
                                   1U, 170U, 191U, 211U);
            }
            graphics_draw_text(text_x, text_y + 390U,
                               "ENTER OPEN  R RENAME  C COPY  M MOVE  S SORT  BACKSPACE UP",
                               1, 170, 191, 211);
            if (files_listing_truncated) {
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
        graphics_draw_text(text_x, text_y, "NEXUS BROWSER - HTTP TEXT VIEW",
                           2, 239, 249, 255);
        graphics_draw_text(text_x, text_y + 38U, "HOST/PATH:",
                           1, 108, 216, 229);
        graphics_draw_text(text_x + 82U, text_y + 38U, browser_address,
                           1, 239, 249, 255);
        graphics_draw_text(text_x, text_y + 66U, browser_status,
                           1, 170, 225, 190);
        for (size_t row = 0U; row < NEXUS_BROWSER_VISIBLE_LINES; ++row) {
            const size_t line = browser_line_scroll + row;
            if (line >= browser_line_count) break;
            graphics_draw_text(text_x, text_y + 108U + row * 24U,
                               browser_lines[line], 1,
                               233, 243, 252);
        }
        if (browser_content_truncated)
            graphics_draw_text(text_x + 360U, text_y + 66U,
                               "TRUNCATED", 1, 239, 177, 119);
        graphics_draw_text(text_x, text_y + 360U,
                           "PGUP/PGDN SCROLL  HOME/END  ENTER LOADS  ESC HOME",
                           1, 170, 191, 211);
        graphics_draw_text(text_x, text_y + 382U,
                           "PLAIN HTTP ONLY - DO NOT SEND PRIVATE DATA",
                           1, 239, 177, 119);
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
            const char *network_status = !info.link_up
                ? "LINK DOWN"
                : (info.ipv4_configured ? "CONNECTED" : "DHCP WAITING");
            graphics_draw_text(text_x, text_y + 126U,
                               "NETWORK:", 1, 233U, 243U, 252U);
            graphics_draw_text(text_x + 76U, text_y + 126U,
                               info.interface_medium == NEXUS_NETDEV_MEDIUM_WIFI
                                   ? "WI-FI" : "ETHERNET", 1,
                               119U, 229U, 211U);
            graphics_draw_text(text_x + 160U, text_y + 126U,
                               info.interface_name, 1,
                               233U, 243U, 252U);
            graphics_draw_text(text_x + 260U, text_y + 126U,
                               network_status, 1,
                               info.link_up ? 108U : 239U,
                               info.link_up ? 216U : 177U,
                               info.link_up ? 229U : 119U);
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
                               "NETWORK: NO ADAPTER",
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
        graphics_draw_text(text_x, text_y + 282U,
                           "DISK WRITES ARE DISABLED IN THIS BUILD",
                           1, 239, 177, 119);
        graphics_draw_text(text_x, text_y + 330U,
                           "ESC RETURNS HOME - STATUS REFRESHES ON INPUT",
                           1, 170, 191, 211);
    }

    desktop_draw_taskbar(bottom_bar);
    desktop_draw_start_menu(bottom_bar);
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
    active_view = DESKTOP_HOME;
    installer_firmware = NEXUS_FIRMWARE_UEFI;
    installer_selected_device = 0U;
    installer_refresh_plan();
    mouse_left_was_down = false;
    start_menu_open = false;
    window_maximized = false;
    window_positioned = false;
    window_dragging = false;
    window_position_x = 0U;
    window_position_y = 0U;
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

static void draw_pointer(void) {
    const uint64_t x = (uint64_t)pointer_x;
    const uint64_t y = (uint64_t)pointer_y;
    graphics_fill_rect(x, y, 2U, 15U, 245, 250, 255);
    graphics_fill_rect(x + 1U, y + 1U, 2U, 11U, 245, 250, 255);
    graphics_fill_rect(x + 2U, y + 5U, 5U, 2U, 245, 250, 255);
}

static void refresh_files_listing(void) {
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
        files_listing[n].directory = false;
        files_listing[n].size = 0U;
        for (size_t k = 0U; k < sizeof(files_listing[n].name); ++k)
            files_listing[n].name[k] = '\0';
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
                files_listing[files_listing_count].name[length] = entry.name[length];
                ++length;
            }
            files_listing[files_listing_count].name[length] = '\0';
            files_listing[files_listing_count].directory =
                entry.type == NEXUS_VFS_DIRECTORY;
            files_listing[files_listing_count].size = entry.size;
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
    if (!nexus_file_list_sort(files_listing, files_listing_count,
                              files_sort_mode)) {
        files_sort_mode = NEXUS_FILE_SORT_NAME;
        (void)nexus_file_list_sort(files_listing, files_listing_count,
                                   files_sort_mode);
        serial_write("FILES: invalid sort mode; restored name order\r\n");
    }
}

static bool files_listing_get_entry(
    size_t index, struct nexus_vfs_dirent *entry) {
    if (entry == NULL || index >= files_listing_count) return false;
    *entry = (struct nexus_vfs_dirent){0};
    size_t length = 0U;
    while (length < NEXUS_VFS_NAME_MAX - 1U &&
           files_listing[index].name[length] != '\0') {
        entry->name[length] = files_listing[index].name[length];
        ++length;
    }
    entry->name[length] = '\0';
    entry->type = files_listing[index].directory
        ? NEXUS_VFS_DIRECTORY : NEXUS_VFS_FILE;
    entry->size = files_listing[index].size;
    return true;
}

static size_t files_editor_max_view_offset(void) {
    return files_edit_content_length > FILES_EDITOR_VIEW_BYTES
        ? files_edit_content_length - FILES_EDITOR_VIEW_BYTES : 0U;
}

static void files_editor_scroll_to_end(void) {
    files_edit_view_offset = files_editor_max_view_offset();
    files_edit_follow_end = true;
}

static void files_editor_keep_cursor_visible(void) {
    const size_t maximum = files_editor_max_view_offset();
    if (files_edit_cursor < files_edit_view_offset) {
        files_edit_view_offset = files_edit_cursor;
    } else if (files_edit_cursor >
               files_edit_view_offset + FILES_EDITOR_VIEW_BYTES) {
        files_edit_view_offset =
            files_edit_cursor - FILES_EDITOR_VIEW_BYTES;
    }
    if (files_edit_view_offset > maximum)
        files_edit_view_offset = maximum;
    files_edit_follow_end = false;
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

static void files_listing_cycle_sort(void) {
    char selected_name[NEXUS_VFS_NAME_MAX] = {0};
    const bool had_selection = files_selected_index < files_listing_count;
    if (had_selection) {
        for (size_t i = 0U; i < NEXUS_VFS_NAME_MAX; ++i)
            selected_name[i] = files_listing[files_selected_index].name[i];
    }

    files_sort_mode = (enum nexus_file_sort_mode)
        (((unsigned int)files_sort_mode + 1U) % NEXUS_FILE_SORT_MODE_COUNT);
    if (!nexus_file_list_sort(files_listing, files_listing_count,
                              files_sort_mode)) return;

    if (had_selection) {
        for (size_t index = 0U; index < files_listing_count; ++index) {
            bool equal = true;
            for (size_t i = 0U; i < NEXUS_VFS_NAME_MAX; ++i) {
                if (selected_name[i] != files_listing[index].name[i]) {
                    equal = false;
                    break;
                }
                if (selected_name[i] == '\0') break;
            }
            if (equal) {
                files_selected_index = index;
                break;
            }
        }
    }
    files_listing_ensure_selection_visible();
    serial_write("FILES: sort mode ");
    serial_write(nexus_file_sort_mode_label(files_sort_mode));
    serial_write("\r\n");
}

static void files_editor_begin_new(void) {
    files_edit_name[0] = '\0';
    files_edit_content[0] = '\0';
    files_edit_name_length = 0U;
    files_edit_content_length = 0U;
    files_edit_cursor = 0U;
    files_edit_view_offset = 0U;
    files_edit_follow_end = true;
    files_edit_status[0] = '\0';
    files_editor_state = FILES_EDITOR_NAME;
}

static void files_editor_begin_directory(void) {
    if (!vfs_nxfs_writable() || !vfs_current_directory_writable()) {
        const char *message = "PERSISTENT NXFS WRITE ACCESS REQUIRED";
        size_t i = 0U;
        while (message[i] != '\0' && i < sizeof(files_edit_status) - 1U) {
            files_edit_status[i] = message[i];
            ++i;
        }
        files_edit_status[i] = '\0';
        return;
    }
    files_edit_name[0] = '\0';
    files_edit_content[0] = '\0';
    files_edit_name_length = 0U;
    files_edit_content_length = 0U;
    files_edit_cursor = 0U;
    files_edit_view_offset = 0U;
    files_edit_follow_end = true;
    files_edit_status[0] = '\0';
    files_editor_state = FILES_EDITOR_DIRECTORY_NAME;
}

static void files_editor_begin_selected(void) {
    struct nexus_vfs_dirent selected;
    if (files_selected_index >= files_listing_count ||
        !files_listing_get_entry(files_selected_index, &selected) ||
        selected.type != NEXUS_VFS_FILE ||
        selected.size > FILES_EDITOR_MAX_BYTES) {
        files_edit_status[0] = '\0';
        const char *message = "FILE EXCEEDS 4096-BYTE EDITOR LIMIT - UNCHANGED";
        size_t i = 0U;
        while (message[i] != '\0' && i < sizeof(files_edit_status) - 1U) {
            files_edit_status[i] = message[i];
            ++i;
        }
        files_edit_status[i] = '\0';
        return;
    }
    if (!vfs_file_writable(selected.name)) {
        const char *message = "READ ONLY - CREATE A WRITABLE COPY FIRST";
        size_t i = 0U;
        while (message[i] != '\0' && i < sizeof(files_edit_status) - 1U) {
            files_edit_status[i] = message[i];
            ++i;
        }
        files_edit_status[i] = '\0';
        return;
    }
    struct nexus_vfs_file file;
    if (!vfs_open_readonly(selected.name, &file)) return;
    size_t bytes_read = 0U;
    const bool read_ok = vfs_read(&file, files_edit_content,
                                  selected.size, &bytes_read);
    vfs_close(&file);
    if (!read_ok || bytes_read != selected.size) return;
    for (size_t i = 0U; i < sizeof(files_edit_name); ++i)
        files_edit_name[i] = selected.name[i];
    files_edit_name_length = 0U;
    while (files_edit_name_length < sizeof(files_edit_name) - 1U &&
           files_edit_name[files_edit_name_length] != '\0')
        ++files_edit_name_length;
    files_edit_content_length = bytes_read;
    files_edit_cursor = files_edit_content_length;
    files_edit_content[files_edit_content_length] = '\0';
    files_editor_scroll_to_end();
    files_edit_status[0] = '\0';
    files_editor_state = FILES_EDITOR_CONTENT;
}

static void files_open_selected(void) {
    struct nexus_vfs_dirent selected;
    if (files_selected_index >= files_listing_count ||
        !files_listing_get_entry(files_selected_index, &selected)) return;
    if (selected.type == NEXUS_VFS_DIRECTORY) {
        if (vfs_enter_directory(selected.name)) refresh_files_listing();
        return;
    }
    files_editor_begin_selected();
}

static void files_editor_begin_operation(enum files_editor_state state) {
    struct nexus_vfs_dirent selected;
    if (state != FILES_EDITOR_RENAME && state != FILES_EDITOR_COPY &&
        state != FILES_EDITOR_MOVE) return;
    if (files_selected_index >= files_listing_count ||
        !files_listing_get_entry(files_selected_index, &selected) ||
        selected.type != NEXUS_VFS_FILE) {
        serial_write("FILES: select a file before rename, copy or move\r\n");
        return;
    }
    if (!vfs_file_writable(selected.name)) {
        serial_write("FILES: selected file is read-only or on volatile storage\r\n");
        return;
    }
    for (size_t i = 0U; i < NEXUS_VFS_NAME_MAX; ++i)
        files_operation_source[i] = selected.name[i];
    files_edit_name[0] = '\0';
    files_edit_name_length = 0U;
    files_edit_status[0] = '\0';
    files_editor_state = state;
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
        return;
    }
    if (files_editor_state == FILES_EDITOR_NAME ||
        files_editor_state == FILES_EDITOR_DIRECTORY_NAME ||
        files_editor_state == FILES_EDITOR_RENAME ||
        files_editor_state == FILES_EDITOR_COPY ||
        files_editor_state == FILES_EDITOR_MOVE) {
        const bool creating_file = files_editor_state == FILES_EDITOR_NAME;
        const bool creating_directory =
            files_editor_state == FILES_EDITOR_DIRECTORY_NAME;
        const bool file_operation = files_editor_state == FILES_EDITOR_RENAME ||
            files_editor_state == FILES_EDITOR_COPY ||
            files_editor_state == FILES_EDITOR_MOVE;
        if (event->scancode == 0x0EU) {
            if (files_edit_name_length != 0U)
                files_edit_name[--files_edit_name_length] = '\0';
        } else if (event->scancode == 0x1CU) {
            if (files_edit_name_length == 0U) {
                const char *message = creating_directory
                    ? "ENTER A FOLDER NAME"
                    : (file_operation ? "ENTER A NAME OR DESTINATION PATH"
                                      : "ENTER A FILE NAME");
                size_t i = 0U;
                while (message[i] != '\0' &&
                       i < sizeof(files_edit_status) - 1U) {
                    files_edit_status[i] = message[i];
                    ++i;
                }
                files_edit_status[i] = '\0';
            } else if (file_operation) {
                bool succeeded = false;
                const char *operation = "operation";
                if (files_editor_state == FILES_EDITOR_RENAME) {
                    succeeded = vfs_rename_file(files_operation_source,
                                                files_edit_name);
                    operation = "renamed";
                } else if (files_editor_state == FILES_EDITOR_COPY) {
                    succeeded = vfs_copy_file(files_operation_source,
                                              files_edit_name);
                    operation = "copied";
                } else {
                    succeeded = vfs_move_file(files_operation_source,
                                              files_edit_name);
                    operation = "moved";
                }
                if (succeeded) {
                    serial_write("FILES: ");
                    serial_write(operation);
                    serial_write(" ");
                    serial_write(files_operation_source);
                    serial_write(" -> ");
                    serial_write(files_edit_name);
                    serial_write("\r\n");
                    files_editor_state = FILES_EDITOR_CLOSED;
                    files_operation_source[0] = '\0';
                    files_edit_status[0] = '\0';
                    refresh_files_listing();
                } else {
                    const char *message =
                        "OPERATION FAILED - CHECK PATH, COLLISIONS OR STORAGE";
                    size_t i = 0U;
                    while (message[i] != '\0' &&
                           i < sizeof(files_edit_status) - 1U) {
                        files_edit_status[i] = message[i];
                        ++i;
                    }
                    files_edit_status[i] = '\0';
                }
            } else if (files_editor_name_exists()) {
                const char *message = creating_directory
                    ? "NAME ALREADY EXISTS" : "FILE EXISTS - PRESS E TO EDIT";
                size_t i = 0U;
                while (message[i] != '\0' &&
                       i < sizeof(files_edit_status) - 1U) {
                    files_edit_status[i] = message[i];
                    ++i;
                }
                files_edit_status[i] = '\0';
            } else if (creating_directory) {
                if (vfs_create_directory(files_edit_name)) {
                    serial_write("FILES: created persistent folder ");
                    serial_write(files_edit_name);
                    serial_write("\r\n");
                    files_editor_state = FILES_EDITOR_CLOSED;
                    refresh_files_listing();
                    files_edit_status[0] = '\0';
                } else {
                    const char *message =
                        "FOLDER CREATE FAILED - CHECK NAME OR STORAGE";
                    size_t i = 0U;
                    while (message[i] != '\0' &&
                           i < sizeof(files_edit_status) - 1U) {
                        files_edit_status[i] = message[i];
                        ++i;
                    }
                    files_edit_status[i] = '\0';
                }
            } else if (creating_file) {
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
    } else if (event->scancode == 0x4BU) {
        nexus_text_buffer_move_left(files_edit_content_length,&files_edit_cursor);
        files_editor_keep_cursor_visible();
        return;
    } else if (event->scancode == 0x4DU) {
        nexus_text_buffer_move_right(files_edit_content_length,&files_edit_cursor);
        files_editor_keep_cursor_visible();
        return;
    } else if (event->scancode == 0x48U) {
        nexus_text_buffer_move_up(files_edit_content,files_edit_content_length,
                                  &files_edit_cursor);
        files_editor_keep_cursor_visible();
        return;
    } else if (event->scancode == 0x50U) {
        nexus_text_buffer_move_down(files_edit_content,files_edit_content_length,
                                    &files_edit_cursor);
        files_editor_keep_cursor_visible();
        return;
    } else if (event->scancode == 0x47U) {
        nexus_text_buffer_move_home(files_edit_content,files_edit_content_length,
                                    &files_edit_cursor);
        files_editor_keep_cursor_visible();
        return;
    } else if (event->scancode == 0x4FU) {
        nexus_text_buffer_move_end(files_edit_content,files_edit_content_length,
                                   &files_edit_cursor);
        files_editor_keep_cursor_visible();
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
        (void)nexus_text_buffer_backspace(
            files_edit_content,sizeof(files_edit_content),
            &files_edit_content_length,&files_edit_cursor);
        files_editor_keep_cursor_visible();
    } else if (event->scancode == 0x53U) {
        (void)nexus_text_buffer_delete(
            files_edit_content,sizeof(files_edit_content),
            &files_edit_content_length,files_edit_cursor);
        files_editor_keep_cursor_visible();
    } else if (event->scancode == 0x1CU) {
        if (nexus_text_buffer_insert(
                files_edit_content,sizeof(files_edit_content),
                &files_edit_content_length,&files_edit_cursor,'\n')) {
            files_editor_keep_cursor_visible();
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
            nexus_text_buffer_insert(files_edit_content,
                sizeof(files_edit_content),&files_edit_content_length,
                &files_edit_cursor,ch)) {
            files_editor_keep_cursor_visible();
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

/* Nexus System and Data volumes share the NXFS type GUID.  The recovery
 * partition is their stable boundary: System precedes it and user Data
 * follows it.  Never expose the installed kernel's System volume as the
 * user's file store. */
static bool files_is_user_data_partition(
    const struct nexus_gpt_partition *partition) {
    static const uint8_t recovery_guid[16] = {
        0x4EU, 0x58U, 0x52U, 0x43U, 0x01U, 0x00U, 0x58U, 0x4EU,
        0x9AU, 0x11U, 0x4EU, 0x58U, 0x55U, 0x53U, 0x52U, 0x43U
    };
    if (!nxfs_partition_type_matches(partition)) return false;
    for (size_t i = 0U; i < gpt_partition_count(); ++i) {
        const struct nexus_gpt_partition *recovery = gpt_partition_at(i);
        if (!files_partition_guid_matches(recovery, recovery_guid)) continue;
        const struct nexus_gpt_partition *data = gpt_partition_at(i + 1U);
        return data != NULL &&
               data->first_lba == partition->first_lba &&
               data->last_lba == partition->last_lba &&
               nxfs_partition_type_matches(data);
    }
    return false;
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
    for (size_t n = 0U; n < FILES_LISTING_MAX_ENTRIES; ++n) {
        files_listing[n].directory = false;
        files_listing[n].size = 0U;
        for (size_t k = 0U; k < sizeof(files_listing[n].name); ++k)
            files_listing[n].name[k] = '\0';
    }

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
    browser_line_count = 1U;
    browser_line_scroll = 0U;
    browser_content_truncated = false;
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
        else
            browser_content_truncated = true;
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
        if (browser_line_count >= NEXUS_BROWSER_LINE_COUNT) {
            browser_content_truncated = true;
            return;
        }
        ++browser_line_count;
        line = browser_lines[browser_line_count - 1U];
        length = 0U;
    }
    line[length] = value;
    line[length + 1U] = '\0';
}

static void browser_render_body(const uint8_t *data, size_t length) {
    browser_clear_page();
    if (data == NULL) return;

    char text[1536];
    struct nexus_html_text_result result;
    if (!nexus_html_to_text(data, length, text, sizeof(text), &result)) {
        browser_set_status("HTML text conversion rejected invalid input");
        return;
    }
    for (size_t i = 0U; i < result.output_length; ++i)
        browser_append_page_char(text[i]);
    if (result.truncated || result.malformed)
        browser_content_truncated = result.truncated;
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

static bool browser_valid_host(const char *host, size_t length) {
    if (host == NULL || length == 0U || length >= 64U) return false;
    size_t label_length = 0U;
    bool previous_hyphen = false;
    for (size_t i = 0U; i < length; ++i) {
        const char ch = host[i];
        if (ch == '.') {
            if (label_length == 0U || previous_hyphen) return false;
            label_length = 0U;
            previous_hyphen = false;
            continue;
        }
        const bool alphanumeric =
            (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9');
        if (!alphanumeric && ch != '-') return false;
        if (label_length == 0U && ch == '-') return false;
        if (++label_length > 63U) return false;
        previous_hyphen = ch == '-';
    }
    return label_length != 0U && !previous_hyphen;
}

static bool browser_starts_with(const char *value, size_t length,
                                 const char *prefix) {
    size_t i = 0U;
    while (prefix[i] != '\0') {
        if (i >= length || value[i] != prefix[i]) return false;
        ++i;
    }
    return true;
}

static void browser_navigate(void) {
    browser_clear_page();
    if (browser_address_length == 0U) {
        browser_set_status("Enter a host or host/path first");
        return;
    }

    size_t address_start = 0U;
    if (browser_starts_with(browser_address, browser_address_length,
                            "https://")) {
        browser_set_status("HTTPS unavailable: TLS certificate validation is not implemented");
        return;
    }
    if (browser_starts_with(browser_address, browser_address_length,
                            "http://")) {
        address_start = 7U;
    } else {
        for (size_t i = 0U; i + 2U < browser_address_length; ++i) {
            if (browser_address[i] == ':' &&
                browser_address[i + 1U] == '/' &&
                browser_address[i + 2U] == '/') {
                browser_set_status("Unsupported URL scheme");
                return;
            }
        }
    }

    char host[64];
    char path[96];
    size_t host_length = 0U;
    size_t path_length = 0U;
    bool in_path = false;
    path[path_length++] = '/';
    for (size_t i = address_start; i < browser_address_length; ++i) {
        const char ch = browser_address[i];
        if (!in_path && ch == '/') {
            in_path = true;
            path_length = 0U;
            path[path_length++] = '/';
            continue;
        }
        if (!in_path) {
            if (host_length >= sizeof(host) - 1U) {
                browser_set_status("Host name is too long");
                return;
            }
            host[host_length++] = ch;
        } else {
            if (ch == '#') break; /* Fragments are never sent in HTTP requests. */
            if (path_length >= sizeof(path) - 1U ||
                ch < 0x21 || ch > 0x7E) {
                browser_set_status("Invalid or overlong URL path");
                return;
            }
            path[path_length++] = ch;
        }
    }
    if (host_length == 0U) {
        browser_set_status("Enter a host name");
        return;
    }
    host[host_length] = '\0';
    path[path_length] = '\0';
    if (!browser_valid_host(host, host_length)) {
        browser_set_status("Invalid host; ports and special characters are unsupported");
        return;
    }

    browser_set_status("Resolving host...");
    uint8_t address[4];
    if (!net_dns_resolve(host, address, 600U)) {
        browser_set_status("DNS failed; check DHCP/network");
        return;
    }
    browser_set_status("Opening TCP connection...");
    if (!net_tcp_connect(address, 80U, 800U)) {
        browser_set_status("TCP connection to port 80 failed");
        return;
    }

    char request[256];
    size_t request_length = 0U;
    bool request_ok =
        browser_append_text(request, sizeof(request), &request_length,
                            "GET ") &&
        browser_append_text(request, sizeof(request), &request_length, path) &&
        browser_append_text(request, sizeof(request), &request_length,
                            " HTTP/1.0\r\nHost: ") &&
        browser_append_text(request, sizeof(request), &request_length, host) &&
        browser_append_text(request, sizeof(request), &request_length,
                            "\r\nConnection: close\r\nUser-Agent: Nexus/0.1\r\n\r\n");
    if (!request_ok || !net_tcp_send(request, (uint16_t)request_length)) {
        browser_set_status("HTTP request could not be sent");
        net_tcp_close();
        return;
    }

    uint8_t response[1536];
    uint8_t body[1536];
    struct nexus_http_response parsed;
    size_t response_length = 0U;
    const uint64_t start = timer_ticks();
    uint64_t last_data = start;
    browser_set_status("Receiving HTTP response...");
    while (timer_ticks() - start < 600U &&
           (response_length == 0U || timer_ticks() - last_data < 40U) &&
           response_length < sizeof(response)) {
        net_poll();
        const size_t received =
            net_tcp_receive(response + response_length,
                            sizeof(response) - response_length);
        if (received != 0U) {
            response_length += received;
            last_data = timer_ticks();
        }
        __asm__ volatile ("pause");
    }
    net_tcp_close();

    if (response_length == 0U) {
        browser_set_status("No HTTP response received");
        return;
    }
    if (!nexus_http_decode_response(response, response_length, body,
                                    sizeof(body), &parsed)) {
        browser_set_status("Invalid, truncated, or unsupported HTTP framing");
        return;
    }
    if (parsed.status_code < 200U || parsed.status_code >= 300U) {
        char number[21];
        char message[80] = "HTTP status ";
        format_u64(parsed.status_code, number);
        size_t length = 12U;
        for (size_t i = 0U; number[i] != '\0' && length < sizeof(message)-1U; ++i)
            message[length++] = number[i];
        message[length] = '\0';
        browser_set_status(message);
        return;
    }
    browser_render_body(body, parsed.body_length);
    if (browser_content_truncated)
        browser_set_status("HTML text clipped to the bounded browser page");
    else
        browser_set_status("Loaded HTML text; no CSS, JavaScript, or TLS");
}

static void terminal_execute(void) {
    terminal_output[0] = '\0';
    if (terminal_input_length == 0U) return;
    terminal_input[terminal_input_length] = '\0';
    if (terminal_input[0] == 'h' && terminal_input[1] == 'e' &&
        terminal_input[2] == 'l' && terminal_input[3] == 'p' &&
        terminal_input[4] == '\0') {
        const char *s = "help about mem uptime net dns HOST http HOST ls cat FILE run FILE ps touch NAME write NAME TEXT save NAME rm NAME clear";
        size_t i = 0; while (s[i] != '\0' && i < sizeof(terminal_output)-1U) { terminal_output[i]=s[i]; ++i; }
        terminal_output[i]='\0';
    } else if (terminal_input[0]=='a' && terminal_input[1]=='b' &&
               terminal_input[2]=='o' && terminal_input[3]=='u' &&
               terminal_input[4]=='t' && terminal_input[5]=='\0') {
        const char *s = "Nexus OS original x86-64 kernel";
        size_t i=0; while(s[i]!='\0' && i<sizeof(terminal_output)-1U){terminal_output[i]=s[i];++i;} terminal_output[i]='\0';
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
    } else if (terminal_input[0]=='l' && terminal_input[1]=='s' &&
               terminal_input[2]=='\0') {
        if (!vfs_has_filesystem()) scan_files_once();
        size_t out=0;
        if (!vfs_has_filesystem()) {
            const char *s="no mounted volume";
            while(s[out]!='\0' && out<sizeof(terminal_output)-1U){terminal_output[out]=s[out];++out;}
        } else {
            for(size_t n=0;n<vfs_root_entry_count();++n) {
                struct nexus_vfs_dirent e;
                if(!vfs_root_entry(n,&e)) continue;
                for(size_t k=0;e.name[k]!='\0' && out<sizeof(terminal_output)-2U;++k)
                    terminal_output[out++]=e.name[k];
                if(out<sizeof(terminal_output)-1U) terminal_output[out++]=' ';
            }
            if(out==0U) { terminal_output[out++]='('; terminal_output[out++]='e'; terminal_output[out++]=')'; }
        }
        terminal_output[out]='\0';
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

static bool installer_text_equal_ci(const char *a, const char *b) {
    if (a == NULL || b == NULL) return false;
    size_t i = 0U;
    while (a[i] != '\0' && b[i] != '\0') {
        char x = a[i], y = b[i];
        if (x >= 'a' && x <= 'z') x = (char)(x - 'a' + 'A');
        if (y >= 'a' && y <= 'z') y = (char)(y - 'a' + 'A');
        if (x != y) return false;
        ++i;
    }
    return a[i] == '\0' && b[i] == '\0';
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

static const char *installer_transport_label(
    const struct nexus_block_device *device) {
    if (device == NULL) return "UNKNOWN DEVICE";
    switch (device->transport) {
        case NEXUS_BLOCKDEV_TRANSPORT_NVME: return "NVME SOLID STATE DRIVE";
        case NEXUS_BLOCKDEV_TRANSPORT_AHCI: return "SATA / AHCI DRIVE";
        case NEXUS_BLOCKDEV_TRANSPORT_USB: return "USB STORAGE";
        case NEXUS_BLOCKDEV_TRANSPORT_OPTICAL: return "OPTICAL DRIVE";
        case NEXUS_BLOCKDEV_TRANSPORT_NETWORK: return "NETWORK STORAGE";
        default: return "STORAGE DEVICE";
    }
}

static void installer_select_delta(bool next) {
    const size_t count = blockdev_count();
    if (count == 0U) return;
    if (next) {
        if (installer_selected_device + 1U < count)
            ++installer_selected_device;
    } else if (installer_selected_device > 0U) {
        --installer_selected_device;
    }
    installer_refresh_plan();
    installer_log_selected_target();
}

static bool installer_begin_confirmation(void) {
    const struct nexus_block_device *device =
        blockdev_at(installer_selected_device);
    if (device != NULL && installer_plan_valid &&
        blockdev_boot_media_detection_complete() &&
        !blockdev_is_boot_media(device) &&
        !blockdev_is_mounted(device) &&
        blockdev_identity_is_unique(device) &&
        blockdev_install_write_supported(device)) {
        installer_ui_state = INSTALLER_UI_CONFIRM_ID;
        installer_confirmed_id[0] = '\0';
        installer_input_length = 0U;
        installer_input[0] = '\0';
        installer_set_status("No disk writes have occurred");
        return true;
    }
    installer_set_status("Target is not eligible for installation");
    return false;
}

static void installer_confirm_target_id(void) {
    const struct nexus_block_device *device =
        blockdev_at(installer_selected_device);
    if (device != NULL &&
        installer_text_equal_ci(installer_input, device->stable_id)) {
        size_t n = 0U;
        while (device->stable_id[n] != '\0' &&
               n < sizeof(installer_confirmed_id) - 1U) {
            installer_confirmed_id[n] = device->stable_id[n];
            ++n;
        }
        installer_confirmed_id[n] = '\0';
        installer_ui_state = INSTALLER_UI_CONFIRM_ERASE;
        installer_input_length = 0U;
        installer_input[0] = '\0';
        installer_set_status("Target identity verified; final erase phrase required");
    } else {
        installer_set_status("Target ID did not match");
    }
}

static void installer_draw_button(uint64_t x, uint64_t y,
                                  uint64_t width, uint64_t height,
                                  const char *label, bool selected,
                                  bool warning) {
    const uint8_t red = warning ? 139U : (selected ? 34U : 27U);
    const uint8_t green = warning ? 54U : (selected ? 111U : 46U);
    const uint8_t blue = warning ? 62U : (selected ? 139U : 68U);
    graphics_fill_rect(x, y, width, height, red, green, blue);
    graphics_draw_rect(x, y, width, height,
                       warning ? 224U : (selected ? 105U : 69U),
                       warning ? 104U : (selected ? 194U : 102U),
                       warning ? 110U : (selected ? 220U : 131U));
    graphics_draw_text(x + 8U, y + (height / 2U) - 3U,
                       label, 1U, 242U, 248U, 253U);
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
            return "Existing interrupted install or corrupt journal; recovery required";
        case NEXUS_INSTALL_RESULT_PARTITION_WRITE:
            return "GPT write failed; inspect disk before retrying";
        case NEXUS_INSTALL_RESULT_PARTITION_VERIFY:
            return "GPT read-back verification failed";
        case NEXUS_INSTALL_RESULT_JOURNAL:
            return "Recovery journal update failed";
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

#ifdef NEXUS_INSTALLER_ENABLE_DESTRUCTIVE_WRITES
static void installer_apply_typed_erase(void) {
    if (!installer_text_equal_ci(installer_input, "ERASE")) {
        installer_set_status("Type ERASE exactly to continue");
        return;
    }
    const struct nexus_block_device *device =
        blockdev_at(installer_selected_device);
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
}
#else
static void installer_apply_typed_erase(void) {
    if (!installer_text_equal_ci(installer_input, "ERASE")) {
        installer_set_status("Type ERASE exactly to continue");
        return;
    }
    installer_ui_state = INSTALLER_UI_FAILED;
    installer_set_status(
        "DISK WRITES LOCKED: END-TO-END VALIDATION NOT PASSED");
    serial_write("NEXUS INSTALLER RESULT: LOCKED\r\n");
}
#endif

void desktop_handle_input(const struct nexus_input_event *event) {
    if (!desktop_ready || event == NULL) return;
    if (event->type == NEXUS_INPUT_MOUSE) {
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

        if (window_dragging) {
            if (left_down && active_view != DESKTOP_HOME &&
                !window_maximized) {
                const uint64_t top = clamp_min(screen_height / 16U, 34U);
                const uint64_t bottom = clamp_min(screen_height / 11U, 48U);
                uint64_t pw = screen_width * 3U / 4U;
                uint64_t ph = 512U;
                if (pw > 920U) pw = 920U;
                if (pw > screen_width - 16U) pw = screen_width - 16U;
                if (ph > screen_height - top - bottom - 16U)
                    ph = screen_height - top - bottom - 16U;
                int64_t next_x = pointer_x - (int64_t)window_drag_offset_x;
                int64_t next_y = pointer_y - (int64_t)window_drag_offset_y;
                const int64_t max_x = (int64_t)(screen_width - pw);
                const int64_t min_y = (int64_t)top;
                const int64_t max_y = (int64_t)(screen_height - bottom - ph);
                if (next_x < 0) next_x = 0;
                if (next_x > max_x) next_x = max_x;
                if (next_y < min_y) next_y = min_y;
                if (next_y > max_y) next_y = max_y;
                window_position_x = (uint64_t)next_x;
                window_position_y = (uint64_t)next_y;
                draw_shell();
                draw_pointer();
                return;
            }
            window_dragging = false;
        }

        bool shell_click_handled = false;
        if (clicked) {
            const uint64_t taskbar_height = clamp_min(screen_height / 11U, 48U);
            const uint64_t taskbar_top = screen_height - taskbar_height;
            if (start_menu_open) {
                const uint64_t menu_width = screen_width > 340U ? 304U : screen_width - 24U;
                const uint64_t menu_height = 238U;
                uint64_t menu_y = taskbar_top - menu_height + 4U;
                const uint64_t top_limit = clamp_min(screen_height / 16U, 34U) + 8U;
                if (menu_y < top_limit) menu_y = top_limit;
                if ((uint64_t)pointer_x >= 12U &&
                    (uint64_t)pointer_x < 12U + menu_width &&
                    (uint64_t)pointer_y >= menu_y + 49U &&
                    (uint64_t)pointer_y < menu_y + 49U + 5U * 34U) {
                    const size_t app = (size_t)((uint64_t)pointer_y -
                        (menu_y + 49U)) / 34U;
                    desktop_open_app(app);
                    shell_click_handled = true;
                } else {
                    start_menu_open = false;
                }
            }
            if (!shell_click_handled &&
                (uint64_t)pointer_y >= taskbar_top) {
                if ((uint64_t)pointer_x >= 12U &&
                    (uint64_t)pointer_x < 100U) {
                    start_menu_open = !start_menu_open;
                    shell_click_handled = true;
                } else {
                    for (size_t app = 0U; app < 5U; ++app) {
                        const uint64_t tab_x = 110U + (uint64_t)app * 78U;
                        if ((uint64_t)pointer_x >= tab_x &&
                            (uint64_t)pointer_x < tab_x + 72U) {
                            desktop_open_app(app);
                            shell_click_handled = true;
                            break;
                        }
                    }
                }
            }
            if (!shell_click_handled && active_view != DESKTOP_HOME) {
                const uint64_t top = clamp_min(screen_height / 16U, 34U);
                const uint64_t bottom = clamp_min(screen_height / 11U, 48U);
                uint64_t pw = screen_width * 3U / 4U;
                uint64_t ph = 512U;
                if (pw > 920U) pw = 920U;
                if (pw > screen_width - 16U) pw = screen_width - 16U;
                if (ph > screen_height - top - bottom - 16U)
                    ph = screen_height - top - bottom - 16U;
                if (window_maximized) {
                    pw = screen_width - 32U;
                    ph = screen_height - top - bottom - 16U;
                }
                uint64_t px = (screen_width - pw) / 2U;
                uint64_t py = window_maximized ? top + 8U
                    : top + (screen_height - top - bottom - ph) / 2U;
                if (window_positioned && !window_maximized) {
                    px = window_position_x;
                    py = window_position_y;
                }
                const uint64_t controls_x = px + pw - 66U;
                if ((uint64_t)pointer_y >= py + 8U &&
                    (uint64_t)pointer_y < py + 28U &&
                    (uint64_t)pointer_x >= controls_x &&
                    (uint64_t)pointer_x < controls_x + 58U) {
                    const uint64_t control = (uint64_t)pointer_x - controls_x;
                    if (control < 14U) {
                        active_view = DESKTOP_HOME; /* Minimize */
                        window_maximized = false;
                        window_positioned = false;
                        window_dragging = false;
                    } else if (control >= 22U && control < 36U) {
                        window_maximized = !window_maximized; /* Maximize / restore */
                    } else if (control >= 44U) {
                        active_view = DESKTOP_HOME; /* Close */
                        window_maximized = false;
                        window_positioned = false;
                        window_dragging = false;
                    }
                    start_menu_open = false;
                    shell_click_handled = true;
                }
                if (!shell_click_handled &&
                    (uint64_t)pointer_x >= px &&
                    (uint64_t)pointer_x < px + pw &&
                    (uint64_t)pointer_y >= py &&
                    (uint64_t)pointer_y < py + 40U) {
                    window_position_x = px;
                    window_position_y = py;
                    window_positioned = true;
                    window_drag_offset_x = (uint64_t)pointer_x - px;
                    window_drag_offset_y = (uint64_t)pointer_y - py;
                    window_dragging = true;
                    shell_click_handled = true;
                }
            }
        }
        if (shell_click_handled) {
            draw_shell();
            draw_pointer();
            return;
        }
        if (clicked && active_view == DESKTOP_INSTALLER) {
            const uint64_t top = clamp_min(screen_height / 16U, 34U);
            const uint64_t bottom = clamp_min(screen_height / 11U, 48U);
            uint64_t pw = screen_width * 3U / 4U;
            uint64_t ph = 512U;
            if (pw > 920U) pw = 920U;
            if (pw > screen_width - 16U) pw = screen_width - 16U;
            if (ph > screen_height - top - bottom - 16U)
                ph = screen_height - top - bottom - 16U;
            if (window_maximized) {
                pw = screen_width - 32U;
                ph = screen_height - top - bottom - 16U;
            }
            uint64_t px = (screen_width - pw) / 2U;
            uint64_t py = window_maximized ? top + 8U
                : top + (screen_height - top - bottom - ph) / 2U;
            if (window_positioned && !window_maximized) {
                px = window_position_x;
                py = window_position_y;
            }
            const int64_t relative_x = pointer_x - (int64_t)(px + 32U);
            const int64_t relative_y = pointer_y - (int64_t)(py + 48U);
            if (relative_x >= 0 && relative_y >= 0) {
                if (installer_ui_state == INSTALLER_UI_SELECT) {
                    if (relative_y >= 158 && relative_y < 182) {
                        if (relative_x < 104) {
                            installer_firmware = NEXUS_FIRMWARE_UEFI;
                            installer_refresh_plan();
                        } else if (relative_x >= 112 && relative_x < 216) {
                            installer_firmware = NEXUS_FIRMWARE_LEGACY_BIOS;
                            installer_refresh_plan();
                        } else if (relative_x >= 224 && relative_x < 336) {
                            installer_firmware = NEXUS_FIRMWARE_DUAL;
                            installer_refresh_plan();
                        }
                    } else if (relative_y >= 310 && relative_y < 334) {
                        if (relative_x < 104) {
                            installer_select_delta(false);
                        } else if (relative_x >= 112 && relative_x < 216) {
                            installer_select_delta(true);
                        } else if (relative_x >= 228 && relative_x < 448) {
                            (void)installer_begin_confirmation();
                        }
                    }
                } else if (installer_ui_state == INSTALLER_UI_CONFIRM_ID ||
                           installer_ui_state == INSTALLER_UI_CONFIRM_ERASE) {
                    if (relative_y >= 264 && relative_y < 290) {
                        if (relative_x < 112) {
                            if (installer_ui_state == INSTALLER_UI_CONFIRM_ID) {
                                installer_ui_state = INSTALLER_UI_SELECT;
                                installer_input_length = 0U;
                                installer_input[0] = '\0';
                                installer_set_status("Select a supported target disk");
                            } else {
                                installer_ui_state = INSTALLER_UI_CONFIRM_ID;
                                installer_input_length = 0U;
                                installer_input[0] = '\0';
                                installer_set_status("Re-enter the complete selected drive ID");
                            }
                        } else if (relative_x >= 128 && relative_x < 300) {
                            if (installer_ui_state == INSTALLER_UI_CONFIRM_ID)
                                installer_confirm_target_id();
                            else
                                installer_apply_typed_erase();
                        }
                    }
                } else if (installer_ui_state == INSTALLER_UI_COMPLETE ||
                           installer_ui_state == INSTALLER_UI_FAILED) {
                    if (relative_y >= 264 && relative_y < 290 &&
                        relative_x < 210) {
                        installer_ui_state = INSTALLER_UI_SELECT;
                        installer_input_length = 0U;
                        installer_input[0] = '\0';
                        installer_confirmed_id[0] = '\0';
                        installer_set_status("Select a supported target disk");
                        installer_refresh_plan();
                    }
                }
            }
            draw_shell();
            draw_pointer();
            return;
        }
        if (clicked && active_view == DESKTOP_FILES && files_vfs_ready) {
            const uint64_t top = clamp_min(screen_height / 16U, 34U);
            const uint64_t bottom = clamp_min(screen_height / 11U, 48U);
            uint64_t pw = screen_width * 3U / 4U;
            uint64_t ph = 512U;
            if (pw > 920U) pw = 920U;
            if (pw > screen_width - 16U) pw = screen_width - 16U;
            if (ph > screen_height - top - bottom - 16U)
                ph = screen_height - top - bottom - 16U;
            if (window_maximized) {
                pw = screen_width - 32U;
                ph = screen_height - top - bottom - 16U;
            }
            uint64_t px = (screen_width - pw) / 2U;
            uint64_t py = window_maximized ? top + 8U
                : top + (screen_height - top - bottom - ph) / 2U;
            if (window_positioned && !window_maximized) {
                px = window_position_x;
                py = window_position_y;
            }
            const uint64_t tx = px + 32U;
            const uint64_t ty = py + 82U;
            const int64_t relative_y = pointer_y - (int64_t)(ty + 270U);
            const int64_t relative_x = pointer_x - (int64_t)tx;
            if (relative_y >= 0 && relative_y < 25 &&
                relative_x >= 0 && relative_x < 380) {
                const size_t action = (size_t)relative_x / 76U;
                if (action == 0U) {
                    if (vfs_current_directory_writable())
                        files_editor_begin_new();
                } else if (action == 1U) {
                    files_editor_begin_directory();
                } else if (action == 2U) {
                    files_open_selected();
                } else if (action == 3U) {
                    struct nexus_vfs_dirent selected;
                    if (files_selected_index < files_listing_count &&
                        files_listing_get_entry(files_selected_index, &selected)) {
                        const bool removed = selected.type == NEXUS_VFS_DIRECTORY
                            ? vfs_remove_directory(selected.name)
                            : vfs_remove_file(selected.name);
                        if (removed) {
                            serial_write(selected.type == NEXUS_VFS_DIRECTORY
                                ? "FILES: removed empty persistent folder "
                                : "FILES: removed writable file ");
                            serial_write(selected.name);
                            serial_write("\r\n");
                        } else {
                            serial_write("FILES: delete refused (read-only or folder not empty)\r\n");
                        }
                    }
                    refresh_files_listing();
                } else if (action == 4U) {
                    refresh_files_listing();
                    serial_write("FILES: listing refreshed by mouse\r\n");
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
                }
            }
        }
        if (clicked && active_view == DESKTOP_HOME) {
            const uint64_t top = clamp_min(screen_height / 16U, 34U);
            const uint64_t bottom = clamp_min(screen_height / 11U, 48U);
            uint64_t pw = screen_width * 3U / 4U;
            uint64_t ph = screen_height * 53U / 100U;
            if (pw > 920U) pw = 920U;
            if (ph > 480U) ph = 480U;
            if (pw > screen_width - 16U) pw = screen_width - 16U;
            if (ph > screen_height - top - bottom - 16U)
                ph = screen_height - top - bottom - 16U;
            const uint64_t px = (screen_width - pw) / 2U;
            const uint64_t py = top + (screen_height - top - bottom - ph) / 2U;
            const uint64_t ty = py + 82U + 76U;
            uint64_t tw = (pw - 100U) / 5U;
            if (tw > 190U) tw = 190U;
            if ((uint64_t)pointer_y >= ty && (uint64_t)pointer_y < ty + 74U) {
                for (size_t app = 0U; app < 5U; ++app) {
                    const uint64_t tx = px + 28U + (uint64_t)app * (tw + 10U);
                    if ((uint64_t)pointer_x >= tx &&
                        (uint64_t)pointer_x < tx + tw) {
                        desktop_open_app(app);
                        serial_write("DESKTOP: app tile opened by mouse\r\n");
                        break;
                    }
                }
            }
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
    if (active_view != DESKTOP_INSTALLER && event->scancode == 0x40U) {
        start_menu_open = !start_menu_open;
        draw_shell();
        draw_pointer();
        return;
    }
    if (start_menu_open && active_view != DESKTOP_INSTALLER) {
        if (event->scancode == 0x01U) {
            start_menu_open = false;
            draw_shell();
            draw_pointer();
            return;
        }
        if (event->scancode >= 0x02U && event->scancode <= 0x06U) {
            desktop_open_app((size_t)(event->scancode - 0x02U));
            draw_shell();
            draw_pointer();
            return;
        }
        return;
    }
    if (active_view == DESKTOP_INSTALLER) {
        if ((event->scancode & 0x80U) != 0U ||
            installer_ui_state == INSTALLER_UI_RUNNING) return;
        if (event->scancode == 0x01U) {
            if (installer_ui_state == INSTALLER_UI_SELECT) {
                active_view = DESKTOP_HOME;
            } else {
                installer_ui_state = INSTALLER_UI_SELECT;
                installer_input_length = 0U;
                installer_input[0] = '\0';
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
                installer_select_delta(false);
            } else if (event->scancode == 0x50U && blockdev_count() > 0U) {
                installer_select_delta(true);
            } else if (event->scancode == 0x1CU) {
                (void)installer_begin_confirmation();
            } else {
                return;
            }
        } else if (installer_ui_state == INSTALLER_UI_CONFIRM_ID ||
                   installer_ui_state == INSTALLER_UI_CONFIRM_ERASE) {
            if (event->scancode == 0x0EU) {
                if (installer_input_length > 0U)
                    installer_input[--installer_input_length] = '\0';
            } else if (event->scancode == 0x1CU) {
                if (installer_ui_state == INSTALLER_UI_CONFIRM_ID)
                    installer_confirm_target_id();
                else
                    installer_apply_typed_erase();
            } else {
                const char ch = terminal_scancode_char(event->scancode);
                if (ch >= 0x20 && ch <= 0x7E &&
                    installer_input_length < sizeof(installer_input) - 1U) {
                    installer_input[installer_input_length++] = ch;
                    installer_input[installer_input_length] = '\0';
                }
            }
        } else if (installer_ui_state == INSTALLER_UI_COMPLETE ||
                   installer_ui_state == INSTALLER_UI_FAILED) {
            if (event->scancode == 0x1CU) {
                installer_ui_state = INSTALLER_UI_SELECT;
                installer_input_length = 0U;
                installer_input[0] = '\0';
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
        event->scancode >= 0x3BU && event->scancode <= 0x3FU) {
        desktop_open_app((size_t)(event->scancode - 0x3BU));
        draw_shell();
        draw_pointer();
        return;
    }
    if (active_view == DESKTOP_BROWSER) {
        if ((event->scancode & 0x80U) != 0U) return;
        if (event->scancode == 0x01U) {
            active_view = DESKTOP_HOME;
            draw_shell(); draw_pointer(); return;
        }
        if (event->scancode == 0x1CU) {
            browser_navigate();
        } else if (event->scancode == 0x49U) {
            browser_line_scroll = browser_line_scroll > NEXUS_BROWSER_VISIBLE_LINES
                ? browser_line_scroll - NEXUS_BROWSER_VISIBLE_LINES : 0U;
        } else if (event->scancode == 0x51U) {
            const size_t max_scroll = browser_line_count > NEXUS_BROWSER_VISIBLE_LINES
                ? browser_line_count - NEXUS_BROWSER_VISIBLE_LINES : 0U;
            browser_line_scroll += NEXUS_BROWSER_VISIBLE_LINES;
            if (browser_line_scroll > max_scroll) browser_line_scroll = max_scroll;
        } else if (event->scancode == 0x47U) {
            browser_line_scroll = 0U;
        } else if (event->scancode == 0x4FU) {
            browser_line_scroll = browser_line_count > NEXUS_BROWSER_VISIBLE_LINES
                ? browser_line_count - NEXUS_BROWSER_VISIBLE_LINES : 0U;
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
    if (active_view == DESKTOP_FILES && files_vfs_ready) {
        if ((event->scancode & 0x80U) != 0U) return;
        if (event->scancode == 0x31U) {
            if (vfs_current_directory_writable()) files_editor_begin_new();
            draw_shell(); draw_pointer(); return;
        }
        if (event->scancode == 0x1FU) {
            files_listing_cycle_sort();
            draw_shell(); draw_pointer(); return;
        }
        if (event->scancode == 0x13U) {
            files_editor_begin_operation(FILES_EDITOR_RENAME);
            draw_shell(); draw_pointer(); return;
        }
        if (event->scancode == 0x2EU) {
            files_editor_begin_operation(FILES_EDITOR_COPY);
            draw_shell(); draw_pointer(); return;
        }
        if (event->scancode == 0x32U) {
            files_editor_begin_operation(FILES_EDITOR_MOVE);
            draw_shell(); draw_pointer(); return;
        }
        if (event->scancode == 0x20U) {
            files_editor_begin_directory();
            draw_shell(); draw_pointer(); return;
        }
        if (event->scancode == 0x12U) {
            files_open_selected();
            draw_shell(); draw_pointer(); return;
        }
        if (event->scancode == 0x0EU && vfs_directory_depth() > 0U) {
            if (vfs_parent_directory()) refresh_files_listing();
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
            struct nexus_vfs_dirent selected;
            if (files_listing_get_entry(files_selected_index, &selected)) {
                const bool removed = selected.type == NEXUS_VFS_DIRECTORY
                    ? vfs_remove_directory(selected.name)
                    : vfs_remove_file(selected.name);
                if (removed) {
                    serial_write(selected.type == NEXUS_VFS_DIRECTORY
                        ? "FILES: removed empty persistent folder "
                        : "FILES: removed writable file ");
                    serial_write(selected.name);
                    serial_write("\r\n");
                } else {
                    serial_write("FILES: delete refused (read-only or folder not empty)\r\n");
                }
            }
            refresh_files_listing();
            draw_shell();
            draw_pointer();
            return;
        }
        if (event->scancode == 0x1CU && files_selected_index < files_listing_count) {
            struct nexus_vfs_dirent selected;
            if (!files_listing_get_entry(files_selected_index, &selected)) {
                draw_shell();
                draw_pointer();
                return;
            }
            if (selected.type == NEXUS_VFS_DIRECTORY) {
                if (vfs_enter_directory(selected.name))
                    refresh_files_listing();
            } else if (selected.type == NEXUS_VFS_FILE) {
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
    draw_shell();
    draw_pointer();
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
