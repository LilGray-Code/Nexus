#include "desktop.h"

#include "blockdev.h"
#include "gpt.h"
#include "fat32.h"
#include "vfs.h"
#include "file_list.h"
#include "process.h"
#include "usermode.h"
#include "net.h"
#include "net_https.h"
#include "http.h"
#include "browser_url.h"
#include "browser_search.h"
#include "ca_bundle.h"
#include "html_text.h"
#include "browser_history.h"
#include "download.h"
#include "nxfs.h"

#include "graphics.h"
#include "gpu.h"
#include "cpu.h"
#include "pci.h"
#include "wifi.h"
#include "wifi_pci.h"
#include "firmware.h"
#include "virtio_gpu.h"
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
static bool settings_hardware_page;
static uint64_t screen_width;
static uint64_t screen_height;
static struct nexus_graphics_surface desktop_firmware_surface;
static struct nexus_graphics_surface desktop_render_surface;
static bool desktop_virtio_gpu_active;
static bool desktop_ready;
static bool installer_session_mode;
static bool start_menu_open;
static char start_menu_query[32];
static size_t start_menu_query_length;
static size_t start_menu_selection;
static bool window_maximized;
static bool window_positioned;
static uint64_t window_position_x;
static uint64_t window_position_y;
static bool window_dragging;
static uint64_t window_drag_offset_x;
static uint64_t window_drag_offset_y;
enum desktop_snap_state {
    DESKTOP_SNAP_NONE = 0,
    DESKTOP_SNAP_LEFT,
    DESKTOP_SNAP_RIGHT
};
static enum desktop_snap_state window_snap_state;
static bool window_custom_size;
static uint64_t window_custom_width;
static uint64_t window_custom_height;
static bool window_resizing;
static int64_t resize_pointer_origin_x;
static int64_t resize_pointer_origin_y;
static uint64_t resize_width_origin;
static uint64_t resize_height_origin;
static bool mouse_left_was_down;
enum desktop_theme_mode {
    DESKTOP_THEME_DARK = 0,
    DESKTOP_THEME_LIGHT
};
static enum desktop_theme_mode desktop_theme = DESKTOP_THEME_DARK;
static uint8_t desktop_accent_index;
static const uint8_t desktop_accent_palette[4][3] = {
    {45U, 177U, 202U},   /* Cyan */
    {117U, 136U, 244U}, /* Violet */
    {82U, 198U, 155U},  /* Emerald */
    {217U, 164U, 83U}   /* Amber */
};
#define NEXUS_DESKTOP_PREF_BYTES 7U
static bool desktop_preferences_loaded;
static bool desktop_preferences_saved;
static bool desktop_preferences_persistent;
static bool keyboard_left_shift;
static bool keyboard_right_shift;
static bool keyboard_left_control;
static bool keyboard_left_control_down;
static bool keyboard_right_control;
static bool keyboard_alt;
static bool keyboard_left_alt_down;
static bool keyboard_right_alt_down;
static bool keyboard_windows;
static bool keyboard_windows_used;
static bool keyboard_caps_lock;
static bool keyboard_caps_lock_down;
static void draw_pointer(void);
static const char *installer_transport_label(
    const struct nexus_block_device *device);
static void installer_set_status(const char *message);
static void installer_draw_button(uint64_t x, uint64_t y,
                                  uint64_t width, uint64_t height,
                                  const char *label, bool selected,
                                  bool warning);
static char terminal_scancode_char(uint8_t code);
static void scan_files_once(void);
static void refresh_files_listing(void);
static void files_search_rebuild(void);
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
static size_t files_filtered_indices[FILES_LISTING_MAX_ENTRIES];
static size_t files_filtered_count;
static char files_search_query[NEXUS_VFS_NAME_MAX];
static size_t files_search_query_length;
static bool files_search_focus;
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
static char files_app_status[64];
static char files_run_output[96];
static bool files_run_output_available;
static bool files_run_output_truncated;
static char terminal_input[64];
static size_t terminal_input_length;
static enum nexus_firmware_mode installer_firmware = NEXUS_FIRMWARE_UEFI;
static size_t installer_selected_device;
static bool installer_drive_dropdown_open;
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
/* A larger bounded response window makes real HTML pages less likely to be
 * clipped at the old 1.5 KiB limit. Buffers live in BSS, not the small kernel
 * stack. TCP itself remains a single-connection, bounded receive transport.
 */
#define NEXUS_BROWSER_RESPONSE_CAPACITY (NEXUS_NXFS_MAX_FILE_BYTES + 16384U)
static uint8_t browser_response_buffer[NEXUS_BROWSER_RESPONSE_CAPACITY];
static uint8_t browser_body_buffer[NEXUS_NXFS_MAX_FILE_BYTES];
static bool browser_download_ready;
static size_t browser_download_length;
static char browser_download_name[NEXUS_DOWNLOAD_NAME_MAX];
static char browser_text_buffer[NEXUS_BROWSER_RESPONSE_CAPACITY];
static char browser_address[NEXUS_BROWSER_SEARCH_URL_MAX];
static size_t browser_address_length;
static bool browser_address_edited;
static char browser_status[80] = "HTTPS-first browser with live web search";
static char browser_lines[NEXUS_BROWSER_LINE_COUNT][NEXUS_BROWSER_LINE_WIDTH];
static size_t browser_line_count;
static size_t browser_line_scroll;
static bool browser_content_truncated;
static bool browser_search_page;
static size_t browser_search_selected;
static size_t browser_search_result_count;
static struct nexus_browser_search_result
    browser_search_results[NEXUS_BROWSER_SEARCH_MAX_RESULTS];
static struct nexus_browser_history browser_history;
#define NEXUS_BROWSER_CA_FILE "NEXUS.CRT"
#define NEXUS_BROWSER_CA_MAX_BYTES (512U * 1024U)
static unsigned char browser_ca_bundle[NEXUS_BROWSER_CA_MAX_BYTES + 1U];
static const unsigned char *browser_ca_bundle_data;

static uint64_t clamp_min(uint64_t value, uint64_t minimum) {
    return value < minimum ? minimum : value;
}

struct desktop_window_rect {
    uint64_t x;
    uint64_t y;
    uint64_t width;
    uint64_t height;
};

#define DESKTOP_APP_COUNT 5U
struct desktop_app_window_state {
    bool valid;
    bool maximized;
    bool positioned;
    bool custom_size;
    enum desktop_snap_state snap;
    uint64_t x;
    uint64_t y;
    uint64_t width;
    uint64_t height;
};
static struct desktop_app_window_state
    desktop_app_windows[DESKTOP_APP_COUNT];

/* Compute one authoritative app-window rectangle for painting and hit tests.
 * Keeping this in one place prevents controls from drifting away from the
 * visible window after a resize or snap operation.
 */
static struct desktop_window_rect desktop_window_bounds(void) {
    struct desktop_window_rect bounds = {0U, 0U, 1U, 1U};
    const uint64_t top = clamp_min(screen_height / 16U, 34U);
    const uint64_t bottom = clamp_min(screen_height / 11U, 48U);
    const uint64_t usable_width = screen_width > 32U
        ? screen_width - 32U : screen_width;
    const uint64_t usable_height = screen_height > top + bottom + 16U
        ? screen_height - top - bottom - 16U : 1U;

    /* The ISO installer is a separate, undecorated full-screen session. */
    if (installer_session_mode) {
        bounds.x = screen_width > 16U ? 8U : 0U;
        bounds.y = screen_height > 16U ? 8U : 0U;
        bounds.width = screen_width > 16U ? screen_width - 16U : screen_width;
        bounds.height = screen_height > 16U ? screen_height - 16U : screen_height;
        return bounds;
    }

    if (window_maximized) {
        bounds.x = 16U;
        bounds.y = top + 8U;
        bounds.width = usable_width;
        bounds.height = usable_height;
        return bounds;
    }
    if (window_snap_state != DESKTOP_SNAP_NONE) {
        bounds.width = usable_width / 2U;
        bounds.height = usable_height;
        bounds.x = window_snap_state == DESKTOP_SNAP_LEFT
            ? 16U : 16U + bounds.width;
        bounds.y = top + 8U;
        return bounds;
    }

    bounds.width = screen_width * 3U / 4U;
    if (bounds.width > 920U) bounds.width = 920U;
    if (bounds.width > screen_width - 16U)
        bounds.width = screen_width - 16U;
    bounds.height = usable_height < 544U ? usable_height : 544U;

    if (window_custom_size) {
        uint64_t minimum_width = screen_width >= 576U
            ? 560U : (screen_width > 16U ? screen_width - 16U : screen_width);
        uint64_t minimum_height = usable_height >= 420U
            ? 420U : usable_height;
        if (minimum_width > usable_width) minimum_width = usable_width;
        bounds.width = window_custom_width;
        bounds.height = window_custom_height;
        if (bounds.width < minimum_width) bounds.width = minimum_width;
        if (bounds.width > screen_width - 16U)
            bounds.width = screen_width - 16U;
        if (bounds.height < minimum_height)
            bounds.height = minimum_height;
        if (bounds.height > usable_height) bounds.height = usable_height;
    }

    bounds.x = (screen_width - bounds.width) / 2U;
    bounds.y = top +
        (screen_height - top - bottom - bounds.height) / 2U;
    if (window_positioned) {
        const uint64_t maximum_x = screen_width - bounds.width;
        const uint64_t maximum_y = screen_height - bottom - bounds.height;
        bounds.x = window_position_x > maximum_x
            ? maximum_x : window_position_x;
        bounds.y = window_position_y < top ? top : window_position_y;
        if (bounds.y > maximum_y) bounds.y = maximum_y;
    }
    return bounds;
}


static size_t desktop_app_index_for_view(enum desktop_view view) {
    switch (view) {
        case DESKTOP_FILES: return 0U;
        case DESKTOP_BROWSER: return 1U;
        case DESKTOP_SETTINGS: return 2U;
        case DESKTOP_TERMINAL: return 3U;
        case DESKTOP_INSTALLER: return 4U;
        default: return DESKTOP_APP_COUNT;
    }
}

static void desktop_window_reset(void) {
    window_maximized = false;
    window_positioned = false;
    window_dragging = false;
    window_resizing = false;
    window_snap_state = DESKTOP_SNAP_NONE;
    window_custom_size = false;
    window_position_x = 0U;
    window_position_y = 0U;
    window_custom_width = 0U;
    window_custom_height = 0U;
    window_drag_offset_x = 0U;
    window_drag_offset_y = 0U;
}

static void desktop_window_save_for_view(enum desktop_view view,
                                         bool preserve) {
    const size_t app = desktop_app_index_for_view(view);
    if (app >= DESKTOP_APP_COUNT) return;
    struct desktop_app_window_state *saved = &desktop_app_windows[app];
    saved->valid = preserve;
    if (!preserve) return;
    saved->maximized = window_maximized;
    saved->positioned = window_positioned;
    saved->custom_size = window_custom_size;
    saved->snap = window_snap_state;
    saved->x = window_position_x;
    saved->y = window_position_y;
    saved->width = window_custom_width;
    saved->height = window_custom_height;
}

static void desktop_window_restore_for_view(enum desktop_view view) {
    desktop_window_reset();
    const size_t app = desktop_app_index_for_view(view);
    if (app >= DESKTOP_APP_COUNT || !desktop_app_windows[app].valid) return;
    const struct desktop_app_window_state *saved = &desktop_app_windows[app];
    window_maximized = saved->maximized;
    window_positioned = saved->positioned;
    window_custom_size = saved->custom_size;
    window_snap_state = saved->snap;
    window_position_x = saved->x;
    window_position_y = saved->y;
    window_custom_width = saved->width;
    window_custom_height = saved->height;
}

static bool desktop_theme_is_light(void) {
    return desktop_theme == DESKTOP_THEME_LIGHT;
}

static void desktop_preferences_save(void) {
    uint8_t record[NEXUS_DESKTOP_PREF_BYTES] = {
        'N', 'X', 'P', '1', (uint8_t)desktop_theme,
        desktop_accent_index, 0xA7U
    };
    for (size_t i = 0U; i < NEXUS_DESKTOP_PREF_BYTES - 1U; ++i)
        record[NEXUS_DESKTOP_PREF_BYTES - 1U] ^= record[i];

    desktop_preferences_saved = vfs_write_root_file(
        "NEXUS.CFG", record, sizeof(record));
    desktop_preferences_persistent =
        desktop_preferences_saved && vfs_nxfs_writable();
    serial_write(desktop_preferences_persistent
        ? "DESKTOP: personalization saved to persistent NXFS\r\n"
        : (desktop_preferences_saved
            ? "DESKTOP: personalization saved to volatile RAM only\r\n"
            : "DESKTOP: personalization save unavailable\r\n"));
}

static void desktop_preferences_load(void) {
    if (desktop_preferences_loaded) return;
    desktop_preferences_loaded = true;

    struct nexus_vfs_file file;
    if (!vfs_open_root_readonly("NEXUS.CFG", &file)) return;
    uint32_t size = 0U;
    uint8_t record[NEXUS_DESKTOP_PREF_BYTES] = {0};
    size_t bytes = 0U;
    const bool valid_size = vfs_file_size(&file, &size) &&
                            size == sizeof(record);
    const bool read_ok = valid_size &&
        vfs_read(&file, record, sizeof(record), &bytes) &&
        bytes == sizeof(record);
    const bool from_nxfs = file.nxfs_file;
    vfs_close(&file);
    if (!read_ok || record[0] != 'N' || record[1] != 'X' ||
        record[2] != 'P' || record[3] != '1' ||
        record[4] > DESKTOP_THEME_LIGHT ||
        record[5] >= sizeof(desktop_accent_palette) /
                         sizeof(desktop_accent_palette[0])) {
        serial_write("DESKTOP: ignored invalid personalization record\r\n");
        return;
    }
    uint8_t checksum = 0xA7U;
    for (size_t i = 0U; i < sizeof(record) - 1U; ++i)
        checksum ^= record[i];
    if (checksum != record[sizeof(record) - 1U]) {
        serial_write("DESKTOP: personalization checksum mismatch\r\n");
        return;
    }

    desktop_theme = (enum desktop_theme_mode)record[4];
    desktop_accent_index = record[5];
    desktop_preferences_saved = true;
    desktop_preferences_persistent = from_nxfs;
    serial_write("DESKTOP: personalization loaded from volume root\r\n");
}


static void desktop_draw_accent(uint64_t x, uint64_t y,
                                uint64_t width, uint64_t height) {
    graphics_fill_rect(x, y, width, height,
        desktop_accent_palette[desktop_accent_index][0],
        desktop_accent_palette[desktop_accent_index][1],
        desktop_accent_palette[desktop_accent_index][2]);
}

static void desktop_toggle_theme(void) {
    desktop_theme = desktop_theme_is_light()
        ? DESKTOP_THEME_DARK : DESKTOP_THEME_LIGHT;
    desktop_preferences_save();
}

static void desktop_cycle_accent(void) {
    desktop_accent_index =
        (uint8_t)((desktop_accent_index + 1U) %
                  (sizeof(desktop_accent_palette) /
                   sizeof(desktop_accent_palette[0])));
    desktop_preferences_save();
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


static void hardware_append_text(char *output, size_t capacity,
                                 size_t *length, const char *text) {
    if (output == NULL || length == NULL || text == NULL || capacity == 0U)
        return;
    while (*text != '\0' && *length + 1U < capacity)
        output[(*length)++] = *text++;
    output[*length] = '\0';
}

static void hardware_append_hex(char *output, size_t capacity,
                                size_t *length, uint32_t value,
                                unsigned digits) {
    static const char hex[] = "0123456789ABCDEF";
    if (output == NULL || length == NULL || capacity == 0U) return;
    for (unsigned i = 0U; i < digits && *length + 1U < capacity; ++i) {
        const unsigned shift = (digits - i - 1U) * 4U;
        output[(*length)++] = hex[(value >> shift) & 0x0FU];
    }
    output[*length] = '\0';
}

static void format_hardware_pci_identity(
    uint16_t vendor, uint16_t device, uint8_t revision,
    uint16_t subsystem_vendor, uint16_t subsystem_device,
    char output[80]) {
    size_t length = 0U;
    output[0] = '\0';
    hardware_append_text(output, 80U, &length, "PCI ");
    hardware_append_hex(output, 80U, &length, vendor, 4U);
    hardware_append_text(output, 80U, &length, ":");
    hardware_append_hex(output, 80U, &length, device, 4U);
    hardware_append_text(output, 80U, &length, " REV ");
    hardware_append_hex(output, 80U, &length, revision, 2U);
    hardware_append_text(output, 80U, &length, " SUB ");
    hardware_append_hex(output, 80U, &length, subsystem_vendor, 4U);
    hardware_append_text(output, 80U, &length, ":");
    hardware_append_hex(output, 80U, &length, subsystem_device, 4U);
}

static void hardware_append_flag(char *output, size_t capacity,
                                 size_t *length, bool enabled,
                                 const char *name) {
    if (enabled) {
        if (*length != 0U) hardware_append_text(output, capacity, length, " ");
        hardware_append_text(output, capacity, length, name);
    }
}

static void desktop_draw_hardware_overview(uint64_t x, uint64_t y) {
    const struct nexus_cpu_info *cpu = nexus_cpu_get_info();
    char line[96];
    char number[21];

    graphics_draw_text(x, y, "PROCESSOR / X86-64 CAPABILITIES",
                       1U, 133U, 164U, 190U);
    graphics_draw_text(x, y + 18U,
        cpu->brand[0] != '\0' ? cpu->brand : "CPU INVENTORY UNAVAILABLE",
        1U, 239U, 249U, 255U);
    format_labeled_u64("CORES: ", cpu->physical_cores_per_package, line);
    graphics_draw_text(x, y + 36U, line, 1U, 233U, 243U, 252U);
    format_labeled_u64("LOGICAL: ", cpu->logical_processors_per_package, line);
    graphics_draw_text(x + 118U, y + 36U, line, 1U, 233U, 243U, 252U);
    format_labeled_u64("FAMILY: ", cpu->family, line);
    graphics_draw_text(x + 236U, y + 36U, line, 1U, 170U, 225U, 190U);
    format_labeled_u64("MODEL: ", cpu->model, line);
    graphics_draw_text(x + 330U, y + 36U, line, 1U, 170U, 225U, 190U);

    hardware_append_text(line, sizeof(line), &(size_t){0}, "");
    size_t flags_length = 0U;
    line[0] = '\0';
    hardware_append_flag(line, sizeof(line), &flags_length,
        nexus_cpu_has_feature(NEXUS_CPU_FEATURE_NX), "NX");
    hardware_append_flag(line, sizeof(line), &flags_length,
        nexus_cpu_has_feature(NEXUS_CPU_FEATURE_SMEP), "SMEP");
    hardware_append_flag(line, sizeof(line), &flags_length,
        nexus_cpu_has_feature(NEXUS_CPU_FEATURE_SMAP), "SMAP");
    hardware_append_flag(line, sizeof(line), &flags_length,
        nexus_cpu_has_feature(NEXUS_CPU_FEATURE_PCID), "PCID");
    hardware_append_flag(line, sizeof(line), &flags_length,
        nexus_cpu_has_feature(NEXUS_CPU_FEATURE_INVPCID), "INVPCID");
    hardware_append_flag(line, sizeof(line), &flags_length,
        nexus_cpu_has_feature(NEXUS_CPU_FEATURE_PAGE_1GB), "1G-PAGES");
    if (flags_length == 0U) hardware_append_text(line, sizeof(line), &flags_length, "BASELINE ONLY");
    graphics_draw_text(x, y + 54U, "MEMORY / SECURITY:", 1U,
                       133U, 164U, 190U);
    graphics_draw_text(x + 132U, y + 54U, line, 1U,
                       108U, 216U, 229U);

    size_t isa_length = 0U;
    line[0] = '\0';
    hardware_append_flag(line, sizeof(line), &isa_length,
        nexus_cpu_has_feature(NEXUS_CPU_FEATURE_SSE42), "SSE4.2");
    hardware_append_flag(line, sizeof(line), &isa_length,
        nexus_cpu_has_feature(NEXUS_CPU_FEATURE_AES), "AES");
    hardware_append_flag(line, sizeof(line), &isa_length,
        nexus_cpu_has_feature(NEXUS_CPU_FEATURE_AVX), "AVX");
    hardware_append_flag(line, sizeof(line), &isa_length,
        nexus_cpu_has_feature(NEXUS_CPU_FEATURE_AVX2), "AVX2");
    hardware_append_flag(line, sizeof(line), &isa_length,
        nexus_cpu_has_feature(NEXUS_CPU_FEATURE_BMI2), "BMI2");
    hardware_append_flag(line, sizeof(line), &isa_length,
        nexus_cpu_has_feature(NEXUS_CPU_FEATURE_SHA), "SHA");
    if (isa_length == 0U) hardware_append_text(line, sizeof(line), &isa_length, "SCALAR");
    graphics_draw_text(x, y + 72U, "INSTRUCTION SET:", 1U,
                       133U, 164U, 190U);
    graphics_draw_text(x + 132U, y + 72U, line, 1U,
                       233U, 243U, 252U);

    size_t width_length = 0U;
    line[0] = '\0';
    if (cpu->physical_address_bits != 0U) {
        format_u64(cpu->physical_address_bits, number);
        hardware_append_text(line, sizeof(line), &width_length, "PHYSICAL ");
        hardware_append_text(line, sizeof(line), &width_length, number);
        hardware_append_text(line, sizeof(line), &width_length, "-BIT");
    } else {
        hardware_append_text(line, sizeof(line), &width_length, "PHYSICAL WIDTH UNKNOWN");
    }
    hardware_append_text(line, sizeof(line), &width_length, " / ");
    if (cpu->virtual_address_bits != 0U) {
        format_u64(cpu->virtual_address_bits, number);
        hardware_append_text(line, sizeof(line), &width_length, "VIRTUAL ");
        hardware_append_text(line, sizeof(line), &width_length, number);
        hardware_append_text(line, sizeof(line), &width_length, "-BIT");
    } else {
        hardware_append_text(line, sizeof(line), &width_length, "VIRTUAL WIDTH UNKNOWN");
    }
    graphics_draw_text(x + 300U, y + 72U, line, 1U,
                       170U, 225U, 190U);

    uint64_t row = y + 96U;
    graphics_draw_text(x, row, "GRAPHICS ADAPTERS / ENGINE STATUS",
                       1U, 133U, 164U, 190U);
    row += 18U;
    const uint32_t gpu_count = gpu_adapter_count();
    const uint32_t gpu_visible = gpu_count < 2U ? gpu_count : 2U;
    if (gpu_visible == 0U) {
        graphics_draw_text(x, row, "NO PCI DISPLAY ADAPTER RECORDED",
                           1U, 239U, 177U, 119U);
        row += 18U;
    }
    for (uint32_t i = 0U; i < gpu_visible; ++i) {
        const struct nexus_gpu_info *gpu = gpu_adapter_at(i);
        if (gpu == NULL) continue;
        format_u64(i, number);
        line[0] = '\0';
        size_t n = 0U;
        hardware_append_text(line, sizeof(line), &n, "GPU ");
        hardware_append_text(line, sizeof(line), &n, number);
        hardware_append_text(line, sizeof(line), &n, ": ");
        hardware_append_text(line, sizeof(line), &n, gpu->name);
        graphics_draw_text(x, row, line, 1U, 233U, 243U, 252U);
        row += 16U;
        char ids[80];
        format_hardware_pci_identity(gpu->vendor_id, gpu->device_id,
            gpu->revision_id, gpu->subsystem_vendor_id,
            gpu->subsystem_device_id, ids);
        graphics_draw_text(x + 10U, row, ids, 1U,
                           170U, 191U, 211U);
        row += 16U;
        graphics_draw_text(x + 10U, row,
            gpu->hardware_2d_acceleration
                ? "2D ENGINE: ACTIVE" : "2D ENGINE: NOT ENABLED",
            1U, gpu->hardware_2d_acceleration ? 108U : 239U,
            gpu->hardware_2d_acceleration ? 216U : 177U,
            gpu->hardware_2d_acceleration ? 229U : 119U);
        graphics_draw_text(x + 178U, row,
            gpu->hardware_3d_acceleration
                ? "3D ENGINE: ACTIVE" : "3D ENGINE: NOT ENABLED",
            1U, gpu->hardware_3d_acceleration ? 108U : 239U,
            gpu->hardware_3d_acceleration ? 216U : 177U,
            gpu->hardware_3d_acceleration ? 229U : 119U);
        row += 18U;
    }
    if (gpu_count > gpu_visible) {
        format_labeled_u64("ADDITIONAL ADAPTERS: ", gpu_count - gpu_visible, line);
        graphics_draw_text(x, row, line, 1U, 170U, 191U, 211U);
        row += 16U;
    }
    graphics_draw_text(x, row, gpu_render_path_name(), 1U,
                       108U, 216U, 229U);
    row += 22U;

    graphics_draw_text(x, row, "WIRELESS ADAPTER INVENTORY",
                       1U, 133U, 164U, 190U);
    row += 18U;
    const uint32_t wifi_count = wifi_pci_candidate_count();
    const uint32_t wifi_visible = wifi_count < 2U ? wifi_count : 2U;
    if (wifi_count == 0U) {
        graphics_draw_text(x, row, "NO PCI WI-FI CANDIDATES DETECTED",
                           1U, 239U, 177U, 119U);
        row += 16U;
    }
    for (uint32_t i = 0U; i < wifi_visible; ++i) {
        const struct nexus_wifi_pci_candidate *radio =
            wifi_pci_candidate_at(i);
        if (radio == NULL) continue;
        graphics_draw_text(x, row, radio->vendor_name, 1U,
                           233U, 243U, 252U);
        row += 16U;
        char ids[80];
        format_hardware_pci_identity(radio->vendor_id, radio->device_id,
            radio->revision_id, radio->subsystem_vendor_id,
            radio->subsystem_device_id, ids);
        graphics_draw_text(x + 10U, row, ids, 1U,
                           170U, 191U, 211U);
        row += 16U;
    }
    if (wifi_count > wifi_visible) {
        format_labeled_u64("ADDITIONAL RADIOS: ", wifi_count - wifi_visible, line);
        graphics_draw_text(x, row, line, 1U, 170U, 191U, 211U);
        row += 16U;
    }
    graphics_draw_text(x, row,
        wifi_available() ? "WI-FI DRIVER REGISTERED"
                         : "WI-FI: CHIPSET DRIVER / FIRMWARE NOT BOUND",
        1U, wifi_available() ? 108U : 239U,
        wifi_available() ? 216U : 177U,
        wifi_available() ? 229U : 119U);
    row += 20U;

    const struct nexus_firmware_info *firmware = nexus_firmware_get_info();
    graphics_draw_text(x, row, "PLATFORM FIRMWARE / BUS",
                       1U, 133U, 164U, 190U);
    row += 18U;
    graphics_draw_text(x, row, nexus_firmware_type_label(),
                       1U, 233U, 243U, 252U);
    if (firmware->acpi_present) {
        format_labeled_u64("ACPI REVISION: ", firmware->acpi_revision, line);
        graphics_draw_text(x + 150U, row, line, 1U,
                           170U, 225U, 190U);
    } else {
        graphics_draw_text(x + 150U, row, "ACPI NOT REPORTED",
                           1U, 239U, 177U, 119U);
    }
    row += 18U;
    if (firmware->smbios_present) {
        format_u64(firmware->smbios_major, number);
        line[0] = '\0';
        size_t n = 0U;
        hardware_append_text(line, sizeof(line), &n, "SMBIOS ");
        hardware_append_text(line, sizeof(line), &n, number);
        hardware_append_text(line, sizeof(line), &n, ".");
        format_u64(firmware->smbios_minor, number);
        hardware_append_text(line, sizeof(line), &n, number);
    } else {
        line[0] = '\0';
        size_t n = 0U;
        hardware_append_text(line, sizeof(line), &n, "SMBIOS NOT REPORTED");
    }
    graphics_draw_text(x, row, line, 1U, 170U, 191U, 211U);
    format_labeled_u64("PCI FUNCTIONS: ", pci_device_count(), line);
    graphics_draw_text(x + 180U, row, line, 1U, 170U, 191U, 211U);
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


static const char *installer_candidate_status(
    const struct nexus_block_device *device) {
    if (device == NULL) return "MISSING";
    if (!blockdev_boot_media_detection_complete()) return "WAIT";
    if (blockdev_is_boot_media(device)) return "BOOT MEDIA";
    if (blockdev_is_mounted(device)) return "MOUNTED";
    if (!blockdev_identity_is_unique(device)) return "ID ERROR";
    if (!blockdev_install_write_supported(device)) return "NO WRITE";
    return "READY";
}

static const char *installer_transport_short_label(
    const struct nexus_block_device *device) {
    if (device == NULL) return "UNKNOWN";
    switch (device->transport) {
        case NEXUS_BLOCKDEV_TRANSPORT_NVME: return "NVME";
        case NEXUS_BLOCKDEV_TRANSPORT_AHCI: return "SATA";
        case NEXUS_BLOCKDEV_TRANSPORT_USB: return "USB";
        case NEXUS_BLOCKDEV_TRANSPORT_OPTICAL: return "OPTICAL";
        case NEXUS_BLOCKDEV_TRANSPORT_NETWORK: return "NETWORK";
        default: return "DISK";
    }
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

static char desktop_search_upper(char value) {
    return value >= 'a' && value <= 'z'
        ? (char)(value - 'a' + 'A') : value;
}

static bool desktop_search_contains(const char *text, const char *query) {
    if (text == NULL || query == NULL || query[0] == '\0') return true;
    for (size_t start = 0U; text[start] != '\0'; ++start) {
        size_t offset = 0U;
        while (query[offset] != '\0' && text[start + offset] != '\0' &&
               desktop_search_upper(text[start + offset]) ==
                   desktop_search_upper(query[offset])) ++offset;
        if (query[offset] == '\0') return true;
    }
    return false;
}

static bool desktop_start_menu_app_matches(size_t app) {
    static const char *const search_terms[5] = {
        "FILES EXPLORER DOCUMENTS DATA FOLDERS",
        "WEB BROWSER INTERNET NETWORK",
        "SETTINGS PERSONALIZATION SYSTEM DISPLAY",
        "TERMINAL CONSOLE COMMAND",
        "INSTALL INSTALLER DISK DRIVE SETUP"
    };
    return app < 5U &&
        desktop_search_contains(search_terms[app], start_menu_query);
}

static size_t desktop_start_menu_visible_app(size_t row) {
    for (size_t app = 0U; app < 5U; ++app) {
        if (!desktop_start_menu_app_matches(app)) continue;
        if (row == 0U) return app;
        --row;
    }
    return 5U;
}

static size_t desktop_start_menu_visible_count(void) {
    size_t count = 0U;
    for (size_t app = 0U; app < 5U; ++app)
        if (desktop_start_menu_app_matches(app)) ++count;
    return count;
}

static void desktop_start_menu_toggle(void) {
    start_menu_open = !start_menu_open;
    if (start_menu_open) {
        start_menu_query_length = 0U;
        start_menu_query[0] = '\0';
        start_menu_selection = 0U;
    }
}

static void desktop_start_menu_clamp_selection(void) {
    const size_t count = desktop_start_menu_visible_count();
    if (count == 0U) start_menu_selection = 0U;
    else if (start_menu_selection >= count)
        start_menu_selection = count - 1U;
}

static void desktop_transition_view(enum desktop_view view,
                                     bool remember_current) {
    const bool switching = active_view != view;
    if (switching) {
        desktop_window_save_for_view(active_view, remember_current);
        if (active_view == DESKTOP_FILES)
            files_search_focus = false;
    }
    active_view = view;
    start_menu_open = false;
    start_menu_query_length = 0U;
    start_menu_query[0] = '\0';
    if (switching) {
        if (view == DESKTOP_HOME) desktop_window_reset();
        else desktop_window_restore_for_view(view);
    }
    if (view == DESKTOP_FILES) {
        scan_files_once();
        desktop_preferences_load();
        refresh_files_listing();
    } else if (view == DESKTOP_SETTINGS) {
        scan_files_once();
        desktop_preferences_load();
    } else if (view == DESKTOP_INSTALLER) {
        installer_refresh_plan();
        installer_log_selected_target();
    }
}

static void desktop_open_view(enum desktop_view view) {
    desktop_transition_view(view, true);
}

/* Alt+F4 / the close button discard placement; minimize and desktop switching
 * preserve the last window arrangement for the next taskbar activation.
 */
static void desktop_close_view(void) {
    desktop_transition_view(DESKTOP_HOME, false);
}

static void desktop_open_app(size_t app_index) {
    if (app_index >= 5U) return;
    desktop_open_view(desktop_view_for_app(app_index));
}

static void desktop_draw_taskbar(uint64_t bottom_bar) {
    const uint64_t y = screen_height - bottom_bar;
    graphics_fill_rect(0U, y, screen_width, bottom_bar,
        desktop_theme_is_light() ? 232U : 10U,
        desktop_theme_is_light() ? 238U : 16U,
        desktop_theme_is_light() ? 246U : 31U);
    desktop_draw_accent(0U, y, screen_width, 2U);
    graphics_fill_rect(0U, y + 2U, screen_width, 1U,
        desktop_theme_is_light() ? 194U : 24U,
        desktop_theme_is_light() ? 205U : 43U,
        desktop_theme_is_light() ? 219U : 66U);

    const uint64_t button_y = y + 7U;
    const uint64_t button_h = bottom_bar > 14U ? bottom_bar - 14U : 22U;
    graphics_blend_round_rect(12U, button_y, 88U, button_h, 9U,
        desktop_theme_is_light() ? 215U : 28U,
        desktop_theme_is_light() ? 226U : 57U,
        desktop_theme_is_light() ? 239U : 88U, 242U);
    graphics_round_rect(12U, button_y, 88U, button_h, 9U,
        desktop_theme_is_light() ? 165U : 57U,
        desktop_theme_is_light() ? 184U : 112U,
        desktop_theme_is_light() ? 204U : 151U);
    desktop_draw_accent(22U, button_y + 7U, 22U, 22U);
    graphics_draw_text(27U, button_y + 14U, "N", 1U, 8U, 25U, 43U);
    graphics_draw_text(51U, button_y + button_h / 2U - 4U,
                       "START", 1U,
                       desktop_theme_is_light() ? 32U : 239U,
                       desktop_theme_is_light() ? 49U : 247U,
                       desktop_theme_is_light() ? 69U : 255U);

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
        graphics_blend_round_rect(x, button_y, tab_width, button_h, 8U,
                           selected ? desktop_accent_palette[desktop_accent_index][0]
                                    : (desktop_theme_is_light() ? 220U : 19U),
                           selected ? desktop_accent_palette[desktop_accent_index][1]
                                    : (desktop_theme_is_light() ? 228U : 31U),
                           selected ? desktop_accent_palette[desktop_accent_index][2]
                                    : (desktop_theme_is_light() ? 238U : 49U), 238U);
        graphics_round_rect(x, button_y, tab_width, button_h, 8U,
                           selected ? 70U : (desktop_theme_is_light() ? 171U : 43U),
                           selected ? 143U : (desktop_theme_is_light() ? 187U : 70U),
                           selected ? 176U : (desktop_theme_is_light() ? 205U : 99U));
        graphics_fill_rect(x + 8U, button_y + 9U, 5U,
                           button_h > 28U ? button_h - 18U : 10U,
                           accents[i][0], accents[i][1], accents[i][2]);
        graphics_draw_text(x + 18U, button_y + button_h / 2U - 4U,
                           labels[i], 1U,
                           desktop_theme_is_light() ? 43U : 220U,
                           desktop_theme_is_light() ? 59U : 235U,
                           desktop_theme_is_light() ? 79U : 247U);
        if (selected)
            graphics_fill_rect(x + 8U, y + bottom_bar - 3U,
                               tab_width - 16U, 2U,
                               accents[i][0], accents[i][1], accents[i][2]);
        else if (desktop_app_windows[i].valid)
            graphics_fill_circle(x + tab_width / 2U,
                                 y + bottom_bar - 4U, 2U,
                                 accents[i][0], accents[i][1], accents[i][2]);
    }

    char uptime[21];
    format_u64(timer_ticks() / 100U, uptime);
    const uint64_t status_x = screen_width > 118U ? screen_width - 112U : 8U;
    graphics_draw_text(status_x, button_y + 5U, "UPTIME", 1U,
                       desktop_theme_is_light() ? 91U : 122U,
                       desktop_theme_is_light() ? 111U : 155U,
                       desktop_theme_is_light() ? 135U : 183U);
    graphics_draw_text(status_x, button_y + 21U, uptime, 1U,
                       desktop_theme_is_light() ? 40U : 220U,
                       desktop_theme_is_light() ? 56U : 236U,
                       desktop_theme_is_light() ? 78U : 248U);
}

static void desktop_draw_start_menu(uint64_t bottom_bar) {
    if (!start_menu_open) return;
    const uint64_t menu_width = screen_width > 340U ? 304U : screen_width - 24U;
    const uint64_t menu_height = 258U;
    const uint64_t menu_x = 12U;
    uint64_t menu_y = screen_height - bottom_bar - menu_height + 4U;
    const uint64_t top_limit = clamp_min(screen_height / 16U, 34U) + 8U;
    if (menu_y < top_limit) menu_y = top_limit;

    graphics_fill_rect(menu_x + 5U, menu_y + 6U,
                       menu_width, menu_height, 2U, 5U, 15U);
    graphics_blend_round_rect(menu_x, menu_y,
                              menu_width, menu_height, 12U,
        desktop_theme_is_light() ? 245U : 19U,
        desktop_theme_is_light() ? 248U : 31U,
        desktop_theme_is_light() ? 252U : 52U, 246U);
    graphics_round_rect(menu_x, menu_y, menu_width, menu_height, 12U,
        desktop_theme_is_light() ? 166U : 66U,
        desktop_theme_is_light() ? 184U : 111U,
        desktop_theme_is_light() ? 204U : 148U);
    graphics_round_rect(menu_x, menu_y, menu_width, 42U, 12U,
        desktop_theme_is_light() ? 222U : 28U,
        desktop_theme_is_light() ? 230U : 47U,
        desktop_theme_is_light() ? 239U : 72U);
    graphics_fill_rect(menu_x, menu_y + 12U, menu_width, 30U,
        desktop_theme_is_light() ? 222U : 28U,
        desktop_theme_is_light() ? 230U : 47U,
        desktop_theme_is_light() ? 239U : 72U);
    desktop_draw_accent(menu_x + 14U, menu_y + 11U, 20U, 20U);
    graphics_draw_text(menu_x + 20U, menu_y + 17U, "N", 1U,
                       8U, 25U, 43U);
    graphics_draw_text(menu_x + 44U, menu_y + 17U,
                       "NEXUS APP LAUNCHER", 1U,
        desktop_theme_is_light() ? 37U : 239U,
        desktop_theme_is_light() ? 54U : 247U,
        desktop_theme_is_light() ? 76U : 255U);

    graphics_blend_round_rect(menu_x + 8U, menu_y + 48U,
                       menu_width - 16U, 25U, 7U,
        desktop_theme_is_light() ? 255U : 13U,
        desktop_theme_is_light() ? 255U : 22U,
        desktop_theme_is_light() ? 255U : 36U, 244U);
    graphics_round_rect(menu_x + 8U, menu_y + 48U,
                       menu_width - 16U, 25U, 7U,
        desktop_theme_is_light() ? 173U : 65U,
        desktop_theme_is_light() ? 190U : 99U,
        desktop_theme_is_light() ? 208U : 135U);
    graphics_draw_text(menu_x + 18U, menu_y + 57U,
        start_menu_query_length == 0U ? "SEARCH APPS" : start_menu_query,
        1U,
        desktop_theme_is_light() ? 48U : 235U,
        desktop_theme_is_light() ? 65U : 245U,
        desktop_theme_is_light() ? 84U : 252U);

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
    size_t visible_row = 0U;
    for (size_t app = 0U; app < 5U; ++app) {
        if (!desktop_start_menu_app_matches(app)) continue;
        const uint64_t row_y = menu_y + 80U +
                               (uint64_t)visible_row * 33U;
        const bool selected = visible_row == start_menu_selection;
        graphics_blend_round_rect(menu_x + 8U, row_y,
                           menu_width - 16U, 28U, 7U,
            selected ? desktop_accent_palette[desktop_accent_index][0]
                     : (desktop_theme_is_light() ? 233U : 25U),
            selected ? desktop_accent_palette[desktop_accent_index][1]
                     : (desktop_theme_is_light() ? 239U : 41U),
            selected ? desktop_accent_palette[desktop_accent_index][2]
                     : (desktop_theme_is_light() ? 246U : 64U), 244U);
        graphics_fill_rect(menu_x + 16U, row_y + 7U,
                           14U, 14U, accents[app][0],
                           accents[app][1], accents[app][2]);
        graphics_draw_text(menu_x + 40U, row_y + 11U,
                           names[app], 1U,
                           desktop_theme_is_light() ? 42U : 225U,
                           desktop_theme_is_light() ? 58U : 238U,
                           desktop_theme_is_light() ? 77U : 249U);
        ++visible_row;
    }
    if (visible_row == 0U)
        graphics_draw_text(menu_x + 18U, menu_y + 101U,
                           "NO MATCHING APPS", 1U,
                           239U, 177U, 119U);
    graphics_draw_text(menu_x + 14U, menu_y + menu_height - 12U,
                       "TYPE TO SEARCH  UP/DOWN  ENTER OPEN  ESC", 1U,
                       desktop_theme_is_light() ? 91U : 140U,
                       desktop_theme_is_light() ? 111U : 170U,
                       desktop_theme_is_light() ? 137U : 195U);
}

static void draw_shell(void) {
    const uint64_t width = screen_width;
    const uint64_t height = screen_height;
    const uint64_t top_bar = clamp_min(height / 16U, 34U);
    const uint64_t bottom_bar = clamp_min(height / 11U, 48U);

    if (installer_session_mode) {
        /* No workspace wallpaper or OS taskbar in the install environment. */
        for (uint64_t y = 0U; y < height; ++y) {
            const uint8_t red = (uint8_t)(5U + y * 5U / height);
            const uint8_t green = (uint8_t)(11U + y * 10U / height);
            const uint8_t blue = (uint8_t)(24U + y * 22U / height);
            graphics_fill_rect(0U, y, width, 1U, red, green, blue);
        }
        uint64_t radius = height / 3U;
        if (radius > 240U) radius = 240U;
        graphics_blend_circle(width * 4U / 5U, height / 4U,
            (uint32_t)radius, 45U, 177U, 202U, 30U);
        graphics_blend_circle(width / 5U, height * 4U / 5U,
            (uint32_t)(radius / 2U), 117U, 136U, 244U, 20U);
    } else {
        for (uint64_t y = 0; y < height; ++y) {
            const uint8_t red = desktop_theme_is_light()
                ? (uint8_t)(244U - (y * 12U / height))
                : (uint8_t)(5U + (y * 8U / height));
            const uint8_t green = desktop_theme_is_light()
                ? (uint8_t)(247U - (y * 10U / height))
                : (uint8_t)(11U + (y * 18U / height));
            const uint8_t blue = desktop_theme_is_light()
                ? (uint8_t)(252U - (y * 7U / height))
                : (uint8_t)(25U + (y * 34U / height));
            graphics_fill_rect(0U, y, width, 1U, red, green, blue);
        }
        /* Layered geometric wallpaper: deliberately native framebuffer artwork. */
        graphics_fill_rect(width / 2U, 0U, width / 2U, height,
            desktop_theme_is_light() ? 238U : 9U,
            desktop_theme_is_light() ? 244U : 26U,
            desktop_theme_is_light() ? 250U : 49U);
        graphics_fill_rect(width * 3U / 5U, 0U, width / 3U, height,
            desktop_theme_is_light() ? 234U : 10U,
            desktop_theme_is_light() ? 241U : 31U,
            desktop_theme_is_light() ? 249U : 58U);
        graphics_fill_rect(width * 4U / 5U, 0U, width / 5U, height,
            desktop_theme_is_light() ? 230U : 12U,
            desktop_theme_is_light() ? 238U : 35U,
            desktop_theme_is_light() ? 247U : 63U);
        /* Subtle layered accent glows add depth while remaining software-rendered. */
        uint64_t glow_radius = height / 4U;
        if (glow_radius > 220U) glow_radius = 220U;
        if (glow_radius < 32U) glow_radius = 32U;
        graphics_blend_circle(width * 4U / 5U, height / 3U,
            (uint32_t)glow_radius,
            desktop_accent_palette[desktop_accent_index][0],
            desktop_accent_palette[desktop_accent_index][1],
            desktop_accent_palette[desktop_accent_index][2],
            desktop_theme_is_light() ? 22U : 38U);
        graphics_blend_circle(width * 3U / 5U, height * 2U / 3U,
            (uint32_t)(glow_radius / 2U),
            117U, 136U, 244U, desktop_theme_is_light() ? 16U : 28U);
        for (uint64_t band = 0U; band < 5U; ++band) {
            const uint64_t x = width / 2U + band * (width / 14U);
            if (x < width)
                graphics_fill_rect(x, top_bar, 1U, height - top_bar,
                                   14U, (uint8_t)(42U + band * 3U),
                                   (uint8_t)(70U + band * 4U));
        }
        graphics_fill_rect(0U, 0U, width, top_bar,
            desktop_theme_is_light() ? 239U : 10U,
            desktop_theme_is_light() ? 243U : 17U,
            desktop_theme_is_light() ? 249U : 33U);
        desktop_draw_accent(0U, top_bar - 2U, width, 2U);
        desktop_draw_accent(16U, 7U, 28U, top_bar - 14U);
        graphics_draw_text(25U, top_bar / 2U - 4U, "N", 1U, 7U, 23U, 39U);
        graphics_draw_text(54U, top_bar / 2U - 4U,
                           "NEXUS  /  YOUR WORKSPACE", 1U,
            desktop_theme_is_light() ? 39U : 232U,
            desktop_theme_is_light() ? 57U : 243U,
            desktop_theme_is_light() ? 79U : 252U);
        char top_uptime[21];
        format_u64(timer_ticks() / 100U, top_uptime);
        graphics_draw_text(width > 170U ? width - 154U : 8U,
                           top_bar / 2U - 4U, "RUNNING", 1U,
                           desktop_theme_is_light() ? 34U : 112U,
                           desktop_theme_is_light() ? 133U : 222U,
                           desktop_theme_is_light() ? 91U : 190U);
        graphics_draw_text(width > 102U ? width - 88U : 8U,
                           top_bar / 2U - 4U, top_uptime, 1U,
                           desktop_theme_is_light() ? 77U : 166U,
                           desktop_theme_is_light() ? 94U : 194U,
                           desktop_theme_is_light() ? 119U : 216U);
    }

    uint64_t panel_width = width * 3U / 4U;
    uint64_t panel_height = active_view == DESKTOP_HOME
        ? height * 53U / 100U : 512U;
    uint64_t panel_x = 0U;
    uint64_t panel_y = 0U;
    if (installer_session_mode || active_view != DESKTOP_HOME) {
        const struct desktop_window_rect bounds = desktop_window_bounds();
        panel_x = bounds.x;
        panel_y = bounds.y;
        panel_width = bounds.width;
        panel_height = bounds.height;
    } else {
        if (panel_width > 920U) panel_width = 920U;
        if (panel_height > 512U) panel_height = 512U;
        if (panel_width > width - 16U) panel_width = width - 16U;
        if (panel_height > height - top_bar - bottom_bar - 16U)
            panel_height = height - top_bar - bottom_bar - 16U;
        panel_x = (width - panel_width) / 2U;
        panel_y = top_bar +
            (height - top_bar - bottom_bar - panel_height) / 2U;
    }

    graphics_fill_rect(panel_x + 6U, panel_y + 8U, panel_width,
                       panel_height, 1U, 4U, 13U);
    graphics_blend_round_rect(panel_x, panel_y, panel_width, panel_height,
                              12U, 17U, 28U, 47U, 224U);
    graphics_round_rect(panel_x, panel_y, panel_width, panel_height,
                          12U,
        desktop_theme_is_light() ? 164U : 61U,
        desktop_theme_is_light() ? 183U : 99U,
        desktop_theme_is_light() ? 205U : 133U);
    graphics_round_rect(panel_x, panel_y, panel_width, 40U, 12U,
        desktop_theme_is_light() ? 220U : 25U,
        desktop_theme_is_light() ? 229U : 42U,
        desktop_theme_is_light() ? 240U : 66U);
    graphics_fill_rect(panel_x, panel_y + 12U, panel_width, 28U,
        desktop_theme_is_light() ? 220U : 25U,
        desktop_theme_is_light() ? 229U : 42U,
        desktop_theme_is_light() ? 240U : 66U);
    desktop_draw_accent(panel_x, panel_y + 39U, panel_width, 1U);
    desktop_draw_accent(panel_x + 9U, panel_y + 12U, 16U, 16U);

    const char *window_title = "NEXUS DESKTOP";
    if (active_view == DESKTOP_FILES) window_title = "FILES";
    if (active_view == DESKTOP_BROWSER) window_title = "NEXUS BROWSER";
    if (active_view == DESKTOP_SETTINGS) window_title = "SETTINGS";
    if (active_view == DESKTOP_TERMINAL) window_title = "NEXUS TERMINAL";
    if (active_view == DESKTOP_INSTALLER) window_title = "NEXUS OS INSTALLER";
    graphics_draw_text_aa(panel_x + 33U, panel_y + 15U, window_title,
                           98304U,
                       desktop_theme_is_light() ? 41U : 234U,
                       desktop_theme_is_light() ? 59U : 244U,
                       desktop_theme_is_light() ? 80U : 252U);
    /* Custom window controls; close is wired to the shell input handler. */
    if (!installer_session_mode) {
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
    }

    const uint64_t text_x = panel_x + 32U;
    const uint64_t text_y = panel_y +
        (active_view == DESKTOP_INSTALLER ? 48U :
         (active_view == DESKTOP_SETTINGS ? 60U : 82U));
    if (active_view == DESKTOP_HOME) {
        graphics_draw_text_aa(text_x, text_y, "YOUR NEXUS WORKSPACE",
                               163840U, 241U, 248U, 255U);
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
            graphics_blend_round_rect(tile_x, tile_y, tile_width, 74U, 10U,
                desktop_theme_is_light() ? 228U : 24U,
                desktop_theme_is_light() ? 236U : 41U,
                desktop_theme_is_light() ? 246U : 65U, 244U);
            graphics_round_rect(tile_x, tile_y, tile_width, 74U, 10U,
                desktop_theme_is_light() ? 175U : 58U,
                desktop_theme_is_light() ? 192U : 91U,
                desktop_theme_is_light() ? 210U : 121U);
            graphics_fill_rect(tile_x, tile_y, 4U, 74U,
                               accent[i][0], accent[i][1], accent[i][2]);
            graphics_fill_rect(tile_x + 13U, tile_y + 12U, 25U, 25U,
                               accent[i][0], accent[i][1], accent[i][2]);
            graphics_draw_text(tile_x + 20U, tile_y + 21U, "N", 1U,
                               8U, 25U, 43U);
            graphics_draw_text(tile_x + 13U, tile_y + 51U, labels[i],
                               1U,
                desktop_theme_is_light() ? 41U : 235U,
                desktop_theme_is_light() ? 59U : 245U,
                desktop_theme_is_light() ? 79U : 252U);
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
                desktop_theme_is_light() ? 232U : 22U,
                desktop_theme_is_light() ? 239U : 38U,
                desktop_theme_is_light() ? 247U : 60U);
            graphics_draw_rect(card_x, card_y, card_width, card_h,
                desktop_theme_is_light() ? 177U : 50U,
                desktop_theme_is_light() ? 193U : 79U,
                desktop_theme_is_light() ? 211U : 107U);
            graphics_fill_rect(card_x, card_y, 3U, card_h,
                               card_accent[i][0], card_accent[i][1],
                               card_accent[i][2]);
            graphics_draw_text(card_x + 11U, card_y + 13U,
                               card_labels[i], 1U,
                desktop_theme_is_light() ? 91U : 148U,
                desktop_theme_is_light() ? 109U : 177U,
                desktop_theme_is_light() ? 132U : 198U);
            if (i == 0U) {
                graphics_draw_text(card_x + 11U, card_y + 34U,
                                   memory_value, 1U,
                desktop_theme_is_light() ? 41U : 233U,
                desktop_theme_is_light() ? 59U : 244U,
                desktop_theme_is_light() ? 80U : 252U);
            } else if (i == 1U) {
                graphics_draw_text(card_x + 11U, card_y + 34U,
                                   uptime_value, 1U,
                desktop_theme_is_light() ? 41U : 233U,
                desktop_theme_is_light() ? 59U : 244U,
                desktop_theme_is_light() ? 80U : 252U);
            } else {
                const char *storage_label =
                    vfs_nxfs_mounted() && vfs_nxfs_writable()
                        ? "NXFS READY"
                        : (vfs_root_mounted() ? "MOUNTED" : "RAM ONLY");
                graphics_draw_text(card_x + 11U, card_y + 34U,
                                   storage_label, 1U,
                desktop_theme_is_light() ? 41U : 233U,
                desktop_theme_is_light() ? 59U : 244U,
                desktop_theme_is_light() ? 80U : 252U);
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
                graphics_fill_rect(text_x, text_y + 48U, 520U, 28U,
                                    16U, 31U, 49U);
                graphics_draw_rect(text_x, text_y + 48U, 520U, 28U,
                                   installer_drive_dropdown_open
                                       ? 119U : 67U,
                                   installer_drive_dropdown_open
                                       ? 229U : 105U,
                                   installer_drive_dropdown_open
                                       ? 211U : 139U);
                graphics_draw_text(text_x + 10U, text_y + 58U,
                                   device != NULL ? device->name : "NO DRIVE",
                                   1U, 119U, 229U, 211U);
                graphics_draw_text(text_x + 142U, text_y + 58U,
                                   device != NULL ? installer_transport_short_label(device)
                                                  : "No supported device",
                                   1U, 203U, 217U, 230U);
                graphics_draw_text(text_x + 494U, text_y + 58U,
                                   installer_drive_dropdown_open ? "^" : "v",
                                   1U, 119U, 229U, 211U);
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
                installer_draw_button(text_x + 228U, text_y + 310U, 220U, 24U,
                                      "CONTINUE", false, false);
            }
            graphics_draw_text(text_x, text_y + 348U,
                               "CLICK THE TARGET FIELD TO VIEW ALL DRIVES  |  UP/DOWN SELECT",
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
                                   "Remove the installer USB and restart to launch Nexus OS.",
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
        if (installer_ui_state == INSTALLER_UI_SELECT &&
            installer_drive_dropdown_open) {
            const size_t count = blockdev_count();
            const uint64_t popup_y = text_y + 82U;
            const uint64_t row_height = 28U;
            const size_t visible_count =
                count < NEXUS_BLOCKDEV_MAX_DEVICES
                    ? count : NEXUS_BLOCKDEV_MAX_DEVICES;
            if (visible_count != 0U) {
                graphics_fill_rect(text_x, popup_y, 520U,
                                   (uint64_t)visible_count * row_height + 2U,
                                   10U, 21U, 35U);
                graphics_draw_rect(text_x, popup_y, 520U,
                                   (uint64_t)visible_count * row_height + 2U,
                                   78U, 111U, 140U);
            }
            for (size_t i = 0U; i < visible_count; ++i) {
                const struct nexus_block_device *candidate = blockdev_at(i);
                if (candidate == NULL) continue;
                const uint64_t row_y = popup_y + 1U +
                                       (uint64_t)i * row_height;
                const bool selected = i == installer_selected_device;
                graphics_fill_rect(text_x + 1U, row_y, 518U, 27U,
                    selected ? 34U : (i & 1U ? 21U : 16U),
                    selected ? 83U : (i & 1U ? 39U : 31U),
                    selected ? 101U : (i & 1U ? 56U : 49U));
                char short_model[24];
                size_t model_length = 0U;
                while (candidate->model[model_length] != '\0' &&
                       model_length < 20U) {
                    short_model[model_length] = candidate->model[model_length];
                    ++model_length;
                }
                if (candidate->model[model_length] != '\0') {
                    short_model[model_length++] = '.';
                    short_model[model_length++] = '.';
                    short_model[model_length++] = '.';
                }
                short_model[model_length] = '\0';
                const uint64_t blocks_per_gib = candidate->block_size == 0U
                    ? 0U : (1024ULL * 1024ULL * 1024ULL) /
                           candidate->block_size;
                char capacity[21];
                format_u64(blocks_per_gib == 0U ? 0U :
                           candidate->block_count / blocks_per_gib, capacity);
                graphics_draw_text(text_x + 10U, row_y + 10U,
                                   candidate->name, 1U,
                                   selected ? 239U : 233U,
                                   selected ? 249U : 243U,
                                   selected ? 255U : 252U);
                graphics_draw_text(text_x + 136U, row_y + 10U,
                                   short_model, 1U, 188U, 207U, 223U);
                graphics_draw_text(text_x + 328U, row_y + 10U,
                                   capacity, 1U, 188U, 218U, 194U);
                graphics_draw_text(text_x + 372U, row_y + 10U,
                                   "GiB", 1U, 155U, 177U, 196U);
                graphics_draw_text(text_x + 400U, row_y + 10U,
                                   installer_transport_short_label(candidate),
                                   1U, 170U, 191U, 211U);
                const enum nexus_firmware_mode candidate_layout =
                    installer_firmware == NEXUS_FIRMWARE_LEGACY_BIOS
                        ? NEXUS_FIRMWARE_DUAL : installer_firmware;
                struct nexus_partition_plan candidate_plan;
                const bool candidate_plan_valid = nexus_partition_plan_create(
                    candidate->block_count, candidate->block_size,
                    candidate_layout, &candidate_plan);
                const char *candidate_status =
                    installer_candidate_status(candidate);
                if (candidate_status[0] == 'R' && !candidate_plan_valid)
                    candidate_status = "BAD LAYOUT";
                const bool eligible = candidate_status[0] == 'R';
                graphics_draw_text(text_x + 464U, row_y + 10U,
                                   candidate_status, 1U,
                                   eligible ? 108U : 239U,
                                   eligible ? 216U : 177U,
                                   eligible ? 229U : 119U);
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
                                    : "TEXT EDITOR - 4096 BYTE LIMIT"))));
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
            static const char *const actions[6] = {
                "NEW FILE", "NEW FOLDER", "OPEN/EDIT", "DELETE", "RUN ELF", "REFRESH"
            };
            static const uint8_t action_colors[6][3] = {
                {37U, 139U, 165U}, {80U, 154U, 125U},
                {60U, 116U, 170U}, {153U, 69U, 78U},
                {109U, 139U, 88U}, {67U, 133U, 111U}
            };
            for (size_t action = 0U; action < 6U; ++action) {
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
            graphics_fill_rect(text_x + 312U, text_y + 297U,
                               196U, 23U,
                desktop_theme_is_light() ? 246U : 15U,
                desktop_theme_is_light() ? 250U : 27U,
                desktop_theme_is_light() ? 253U : 43U);
            graphics_draw_rect(text_x + 312U, text_y + 297U,
                               196U, 23U,
                files_search_focus
                    ? desktop_accent_palette[desktop_accent_index][0]
                    : (desktop_theme_is_light() ? 170U : 63U),
                files_search_focus
                    ? desktop_accent_palette[desktop_accent_index][1]
                    : (desktop_theme_is_light() ? 187U : 98U),
                files_search_focus
                    ? desktop_accent_palette[desktop_accent_index][2]
                    : (desktop_theme_is_light() ? 206U : 130U));
            graphics_draw_text(text_x + 320U, text_y + 305U,
                files_search_query_length != 0U
                    ? files_search_query : "CTRL+F TO SEARCH",
                1U,
                desktop_theme_is_light() ? 45U : 229U,
                desktop_theme_is_light() ? 62U : 241U,
                desktop_theme_is_light() ? 81U : 250U);
            graphics_draw_text(text_x + 520U, text_y + 304U,
                                nexus_file_sort_mode_label(files_sort_mode),
                                1U, 170U, 191U, 211U);
            for (size_t slot = 0U; slot < FILES_LISTING_VISIBLE; ++slot) {
                const size_t visible_index = files_listing_first_index + slot;
                if (visible_index >= files_filtered_count) break;
                const size_t index = files_filtered_indices[visible_index];
                if (index >= files_listing_count) continue;
                const uint64_t column = slot % 2U;
                const uint64_t line = slot / 2U;
                const bool selected = visible_index == files_selected_index;
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
            if (files_filtered_count == 0U) {
                graphics_draw_text(text_x, text_y + 326U,
                    files_search_query_length != 0U
                        ? "NO MATCHES" : "NO FILES",
                    1U, 170U, 191U, 211U);
            }
            if (files_run_output_available) {
                graphics_draw_text(text_x, text_y + 374U,
                    files_run_output, 1U, 188U, 218U, 194U);
            }
            graphics_draw_text(text_x, text_y + 390U,
                files_app_status, 1U,
                files_app_status[0] == '\0' ? 139U : 119U,
                files_app_status[0] == '\0' ? 163U : 229U,
                files_app_status[0] == '\0' ? 188U : 190U);
            graphics_draw_text(text_x, text_y + 408U,
                "CTRL+F SEARCH  CTRL+ENTER RUN ELF  ENTER OPEN  R RENAME",
                1, 170, 191, 211);
            if (files_run_output_truncated || files_listing_truncated) {
                graphics_draw_text(text_x, text_y + 420U,
                    files_run_output_truncated
                        ? "PROGRAM OUTPUT TRUNCATED"
                        : "LISTING LIMITED TO 128 ENTRIES",
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
    } else if (active_view == DESKTOP_SETTINGS) {
        graphics_blend_round_rect(text_x + 284U, text_y - 2U, 82U, 24U, 5U,
            settings_hardware_page ? 47U : 25U,
            settings_hardware_page ? 94U : 41U,
            settings_hardware_page ? 117U : 64U, 245U);
        graphics_round_rect(text_x + 284U, text_y - 2U, 82U, 24U, 5U,
            desktop_accent_palette[desktop_accent_index][0],
            desktop_accent_palette[desktop_accent_index][1],
            desktop_accent_palette[desktop_accent_index][2]);
        graphics_draw_text(text_x + 292U, text_y + 6U, "HARDWARE",
                           1U, 239U, 247U, 255U);
        graphics_blend_round_rect(text_x + 374U, text_y - 2U, 82U, 24U, 5U,
            settings_hardware_page ? 25U : 47U,
            settings_hardware_page ? 41U : 94U,
            settings_hardware_page ? 64U : 117U, 245U);
        graphics_round_rect(text_x + 374U, text_y - 2U, 82U, 24U, 5U,
            desktop_accent_palette[desktop_accent_index][0],
            desktop_accent_palette[desktop_accent_index][1],
            desktop_accent_palette[desktop_accent_index][2]);
        graphics_draw_text(text_x + 382U, text_y + 6U, "PERSONAL",
                           1U, 239U, 247U, 255U);
        if (settings_hardware_page) {
            graphics_draw_text(text_x, text_y, "HARDWARE OVERVIEW",
                               2U, 239U, 249U, 255U);
            graphics_draw_text(text_x, text_y + 26U,
                "DEVICE IDENTIFIERS, CAPABILITIES AND ACTIVE DRIVER STATE",
                1U, 170U, 191U, 211U);
            desktop_draw_hardware_overview(text_x, text_y + 46U);
        } else {
        graphics_draw_text(text_x, text_y, "PERSONALIZATION",
                           2U, 239U, 249U, 255U);
        graphics_draw_text(text_x, text_y + 26U,
                           "THEME AND ACCENT APPLY IMMEDIATELY",
                           1U, 170U, 191U, 211U);

        const uint64_t theme_y = text_y + 42U;
        const bool light_theme = desktop_theme_is_light();
        graphics_blend_round_rect(text_x, theme_y, 116U, 32U, 8U,
            light_theme ? 37U : 43U, light_theme ? 57U : 63U,
            light_theme ? 75U : 83U, 245U);
        graphics_round_rect(text_x, theme_y, 116U, 32U, 8U,
            !light_theme ? 45U : 95U, !light_theme ? 177U : 115U,
            !light_theme ? 202U : 135U);
        graphics_draw_text(text_x + 14U, theme_y + 12U, "DARK",
                           1U, 239U, 247U, 255U);
        graphics_blend_round_rect(text_x + 128U, theme_y, 116U, 32U, 8U,
            light_theme ? 202U : 43U, light_theme ? 222U : 63U,
            light_theme ? 237U : 83U, 245U);
        graphics_round_rect(text_x + 128U, theme_y, 116U, 32U, 8U,
            light_theme ? 45U : 95U, light_theme ? 177U : 115U,
            light_theme ? 202U : 135U);
        graphics_draw_text(text_x + 142U, theme_y + 12U, "LIGHT",
                           1U,
            light_theme ? 38U : 239U,
            light_theme ? 56U : 247U,
            light_theme ? 77U : 255U);

        graphics_draw_text(text_x, text_y + 86U, "ACCENT COLOR",
                           1U, 170U, 191U, 211U);
        static const char *const accent_names[4] = {
            "CYAN", "VIOLET", "GREEN", "AMBER"
        };
        for (size_t i = 0U; i < 4U; ++i) {
            const uint64_t swatch_x = text_x + (uint64_t)i * 78U;
            graphics_blend_round_rect(swatch_x, text_y + 104U, 62U, 25U, 6U,
                desktop_accent_palette[i][0],
                desktop_accent_palette[i][1],
                desktop_accent_palette[i][2], 250U);
            graphics_round_rect(swatch_x, text_y + 104U, 62U, 25U, 6U,
                i == desktop_accent_index ? 255U : 91U,
                i == desktop_accent_index ? 255U : 111U,
                i == desktop_accent_index ? 255U : 135U);
            graphics_draw_text(swatch_x + 3U, text_y + 136U,
                               accent_names[i], 1U,
                light_theme ? 57U : 220U,
                light_theme ? 73U : 235U,
                light_theme ? 91U : 247U);
        }

        graphics_blend_round_rect(text_x, text_y + 158U, 320U, 44U, 8U,
            light_theme ? 233U : 23U,
            light_theme ? 240U : 40U,
            light_theme ? 247U : 62U, 244U);
        desktop_draw_accent(text_x, text_y + 158U, 4U, 44U);
        graphics_draw_text(text_x + 14U, text_y + 168U,
                           "LIVE PREVIEW - NEXUS WORKSPACE",
                           1U,
            light_theme ? 43U : 239U,
            light_theme ? 60U : 247U,
            light_theme ? 80U : 255U);
        graphics_draw_text(text_x + 14U, text_y + 184U,
                           "Window chrome, taskbar and launcher updated",
                           1U,
            light_theme ? 79U : 170U,
            light_theme ? 96U : 191U,
            light_theme ? 119U : 211U);

        graphics_draw_text(text_x, text_y + 222U, "SYSTEM SNAPSHOT",
                           1U, 133U, 164U, 190U);
        char line[64];
        format_labeled_u64("TOTAL MEMORY: ", memory_total_bytes(), line);
        graphics_draw_text(text_x, text_y + 246U, line,
                           1U, 233U, 243U, 252U);
        format_labeled_u64("FREE MEMORY: ", memory_free_bytes(), line);
        graphics_draw_text(text_x, text_y + 268U, line,
                           1U, 170U, 225U, 190U);
        format_labeled_u64("UPTIME: ", timer_ticks() / 100U, line);
        graphics_draw_text(text_x + 220U, text_y + 268U, line,
                           1U, 170U, 225U, 190U);

        struct nexus_net_info info;
        if (net_get_info(&info) && info.interface_ready) {
            const char *status = !info.link_up ? "LINK DOWN"
                : (info.ipv4_configured ? "CONNECTED" : "WAITING FOR DHCP");
            graphics_draw_text(text_x, text_y + 298U, "NETWORK",
                               1U, 133U, 164U, 190U);
            graphics_draw_text(text_x + 76U, text_y + 298U, status,
                               1U, info.link_up ? 108U : 239U,
                               info.link_up ? 216U : 177U,
                               info.link_up ? 229U : 119U);
        } else {
            graphics_draw_text(text_x, text_y + 298U,
                               "NETWORK: NO ADAPTER DETECTED",
                               1U, 239U, 177U, 119U);
        }
        const char *wifi_status = "NO PCI RADIO DETECTED";
        if (wifi_available()) {
            switch (wifi_get_state()) {
                case NEXUS_WIFI_READY: wifi_status = "RADIO READY"; break;
                case NEXUS_WIFI_SCANNING: wifi_status = "SCANNING"; break;
                case NEXUS_WIFI_CONNECTING: wifi_status = "CONNECTING"; break;
                case NEXUS_WIFI_CONNECTED: wifi_status = "CONNECTED"; break;
                case NEXUS_WIFI_FAILED: wifi_status = "RADIO ERROR"; break;
                default: wifi_status = "RADIO UNAVAILABLE"; break;
            }
        } else if (wifi_pci_candidate_count() != 0U) {
            wifi_status = "DEVICE FOUND - CHIPSET DRIVER NEEDED";
        }
        graphics_draw_text(text_x, text_y + 310U, "WI-FI:",
                           1U, 133U, 164U, 190U);
        graphics_draw_text(text_x + 52U, text_y + 310U,
                           wifi_status, 1U,
                           wifi_available() ? 108U : 239U,
                           wifi_available() ? 216U : 177U,
                           wifi_available() ? 229U : 119U);

        const char *storage_status = vfs_nxfs_mounted()
            ? (vfs_nxfs_writable() ? "PERSISTENT NXFS READY"
                                   : "NXFS READ ONLY")
            : (vfs_has_filesystem() ? "VOLATILE RAM STORAGE"
                                    : "NO USER STORAGE");
        graphics_draw_text(text_x, text_y + 322U, storage_status,
                           1U, 170U, 225U, 190U);
        graphics_draw_text(text_x, text_y + 350U,
            desktop_preferences_persistent
                ? "PERSONALIZATION SAVED TO NEXUS DATA"
                : (desktop_preferences_saved
                    ? "PERSONALIZATION SAVED TO RAM ONLY"
                    : "PERSONALIZATION NOT SAVED"),
            1U, desktop_preferences_persistent ? 119U : 239U,
            desktop_preferences_persistent ? 229U : 177U,
            desktop_preferences_persistent ? 190U : 119U);
        const struct nexus_cpu_info *cpu = nexus_cpu_get_info();
        graphics_draw_text(text_x, text_y + 372U, "PROCESSOR:",
                           1U, 133U, 164U, 190U);
        graphics_draw_text(text_x + 76U, text_y + 372U,
                           cpu->brand[0] != '\0' ? cpu->brand
                                                  : "CPU INVENTORY UNAVAILABLE",
                           1U, 233U, 243U, 252U);
        format_labeled_u64("CPU CORES: ",
                           cpu->physical_cores_per_package, line);
        graphics_draw_text(text_x, text_y + 390U, line,
                           1U, 170U, 225U, 190U);
        graphics_draw_text(text_x + 166U, text_y + 390U,
                           nexus_cpu_feature_level_label(), 1U,
                           108U, 216U, 229U);

        char adapter_count_text[21];
        format_u64(gpu_adapter_count(), adapter_count_text);
        graphics_draw_text(text_x, text_y + 408U, "DISPLAY ADAPTER:",
                           1U, 133U, 164U, 190U);
        graphics_draw_text(text_x + 102U, text_y + 408U,
                           gpu_primary_name(), 1U,
                           233U, 243U, 252U);
        graphics_draw_text(text_x + 330U, text_y + 408U,
                           "COUNT:", 1U, 133U, 164U, 190U);
        graphics_draw_text(text_x + 378U, text_y + 408U,
                           adapter_count_text, 1U, 170U, 225U, 190U);
        graphics_draw_text(text_x, text_y + 426U,
                           gpu_render_path_name(), 1U,
                           108U, 216U, 229U);
        }
    } else if (active_view == DESKTOP_BROWSER) {
        graphics_draw_text(text_x, text_y, "NEXUS BROWSER - WEB SEARCH + SECURE WEB",
                           2, 239, 249, 255);
        graphics_draw_text(text_x, text_y + 38U, "SEARCH / URL:",
                           1, 108, 216, 229);
        graphics_draw_text(text_x + 92U, text_y + 38U, browser_address,
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
                           "UP/DOWN SELECT RESULT  ENTER OPEN/SEARCH  PGUP/PGDN SCROLL",
                           1, 170, 191, 211);
        graphics_draw_text(text_x, text_y + 382U,
                           "HTTPS TLS 1.3 / HTTP 1.1  HTML TEXT + SEARCH RESULTS",
                           1, 239, 177, 119);
        graphics_blend_round_rect(text_x + 420U, text_y + 398U,
                                  112U, 28U, 7U,
                                  browser_download_ready ? 45U : 35U,
                                  browser_download_ready ? 177U : 59U,
                                  browser_download_ready ? 202U : 83U, 245U);
        graphics_round_rect(text_x + 420U, text_y + 398U,
                            112U, 28U, 7U, 105U, 133U, 160U);
        graphics_draw_text(text_x + 451U, text_y + 408U, "SAVE  F6",
                           1U, 245U, 250U, 255U);
        graphics_draw_text(text_x, text_y + 406U,
                           browser_download_ready
                               ? "VERIFIED FILE READY TO SAVE"
                               : "OPEN A VERIFIED HTTPS FILE TO DOWNLOAD",
                           1U, 170U, 191U, 211U);
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
        const struct nexus_firmware_info *firmware =
            nexus_firmware_get_info();
        graphics_draw_text(text_x, text_y + 258U, "FIRMWARE:",
                           1U, 233U, 243U, 252U);
        graphics_draw_text(text_x + 82U, text_y + 258U,
                           nexus_firmware_type_label(),
                           1U, 119U, 229U, 211U);
        if (firmware->smbios_present) {
            char smbios_version[16] = "SMBIOS ";
            char major[21];
            char minor[21];
            format_u64(firmware->smbios_major, major);
            format_u64(firmware->smbios_minor, minor);
            size_t used = 7U;
            for (size_t i = 0U; major[i] != '\0' &&
                 used < sizeof(smbios_version) - 1U; ++i)
                smbios_version[used++] = major[i];
            smbios_version[used++] = '.';
            for (size_t i = 0U; minor[i] != '\0' &&
                 used < sizeof(smbios_version) - 1U; ++i)
                smbios_version[used++] = minor[i];
            smbios_version[used] = '\0';
            graphics_draw_text(text_x, text_y + 282U, smbios_version,
                               1U, 170U, 225U, 190U);
        } else {
            graphics_draw_text(text_x, text_y + 282U,
                               firmware->acpi_present
                                   ? "ACPI PRESENT - SMBIOS UNAVAILABLE"
                                   : "FIRMWARE TABLES UNAVAILABLE",
                               1U, 239U, 177U, 119U);
        }
        graphics_draw_text(text_x, text_y + 306U,
                           "DISK WRITES ARE DISABLED IN THIS BUILD",
                           1U, 239U, 177U, 119U);
        graphics_draw_text(text_x, text_y + 330U,
                           "ESC RETURNS HOME - STATUS REFRESHES ON INPUT",
                           1U, 170U, 191U, 211U);
    }

    if (!installer_session_mode && active_view != DESKTOP_HOME &&
        !window_maximized && window_snap_state == DESKTOP_SNAP_NONE &&
        panel_width >= 36U && panel_height >= 36U) {
        const uint64_t grip_x = panel_x + panel_width - 17U;
        const uint64_t grip_y = panel_y + panel_height - 17U;
        for (uint64_t line = 0U; line < 3U; ++line) {
            graphics_fill_rect(grip_x + line * 4U, grip_y + 12U,
                               1U, 4U, 125U, 151U, 176U);
            graphics_fill_rect(grip_x + 12U, grip_y + line * 4U,
                               4U, 1U, 125U, 151U, 176U);
        }
    }
    if (!installer_session_mode) {
        desktop_draw_taskbar(bottom_bar);
        desktop_draw_start_menu(bottom_bar);
    }
}

void desktop_set_installer_mode(bool enabled) {
    installer_session_mode = enabled;
}

bool desktop_show(const struct limine_framebuffer *framebuffer) {
    desktop_ready = false;
    if (framebuffer == NULL || framebuffer->address == NULL ||
        framebuffer->memory_model != LIMINE_FRAMEBUFFER_RGB) {
        serial_write("DESKTOP: no compatible RGB framebuffer; keeping serial console\r\n");
        return false;
    }

    struct nexus_graphics_surface surface = {
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
    desktop_firmware_surface = surface;
    desktop_virtio_gpu_active =
        virtio_gpu_start(surface.width, surface.height,
                         &desktop_firmware_surface,
                         &desktop_render_surface);
    if (desktop_virtio_gpu_active) {
        surface = desktop_render_surface;
        gpu_set_virtio_display_active(true);
        gpu_set_firmware_framebuffer_active(false);
    } else {
        surface = desktop_firmware_surface;
        gpu_set_virtio_display_active(false);
        gpu_set_firmware_framebuffer_active(false);
    }
    if (!graphics_init(&surface)) {
        if (desktop_virtio_gpu_active) virtio_gpu_stop();
        desktop_virtio_gpu_active = false;
        gpu_set_virtio_display_active(false);
        serial_write("DESKTOP: graphics surface validation failed; keeping serial console\r\n");
        return false;
    }
    if (!desktop_virtio_gpu_active)
        gpu_set_firmware_framebuffer_active(true);
    screen_width = graphics_width();
    screen_height = graphics_height();
    active_view = installer_session_mode ? DESKTOP_INSTALLER : DESKTOP_HOME;
    nexus_browser_history_init(&browser_history);
    browser_address_length = 0U;
    browser_address[0] = '\0';
    browser_address_edited = false;
    browser_search_page = false;
    browser_search_selected = 0U;
    browser_search_result_count = 0U;
    installer_firmware = NEXUS_FIRMWARE_UEFI;
    installer_selected_device = 0U;
    installer_drive_dropdown_open = false;
    installer_ui_state = INSTALLER_UI_SELECT;
    installer_input_length = 0U;
    installer_input[0] = '\0';
    installer_confirmed_id[0] = '\0';
    installer_set_status("Select a supported target disk");
    installer_refresh_plan();
    if (installer_session_mode) installer_log_selected_target();
    mouse_left_was_down = false;
    start_menu_open = false;
    start_menu_query_length = 0U;
    start_menu_query[0] = '\0';
    start_menu_selection = 0U;
    window_maximized = false;
    window_positioned = false;
    window_dragging = false;
    window_resizing = false;
    window_snap_state = DESKTOP_SNAP_NONE;
    window_custom_size = false;
    window_position_x = 0U;
    window_position_y = 0U;
    for (size_t app = 0U; app < DESKTOP_APP_COUNT; ++app)
        desktop_app_windows[app] = (struct desktop_app_window_state){0};
    pointer_x = (int64_t)(screen_width / 2U);
    pointer_y = (int64_t)(screen_height / 2U);
    keyboard_left_shift = false;
    keyboard_right_shift = false;
    keyboard_left_control = false;
    keyboard_left_control_down = false;
    keyboard_right_control = false;
    keyboard_alt = false;
    keyboard_left_alt_down = false;
    keyboard_right_alt_down = false;
    keyboard_windows = false;
    keyboard_windows_used = false;
    keyboard_caps_lock_down = false;
    desktop_ready = true;
#ifdef NEXUS_INSTALLER_ENABLE_DESTRUCTIVE_WRITES
    serial_write("NEXUS INSTALLER WRITE GATE ENABLED\r\n");
#else
    serial_write("NEXUS INSTALLER WRITE GATE LOCKED\r\n");
#endif
    draw_shell();
    draw_pointer();
    if (installer_session_mode) {
        serial_write("INSTALLER: dedicated full-screen environment rendered\r\n");
        serial_write("NEXUS INSTALLER SESSION READY\r\n");
    } else {
        serial_write("DESKTOP: interactive framebuffer shell rendered\r\n");
        serial_write("NEXUS DESKTOP FRAMEBUFFER READY\r\n");
    }
    return true;
}

static void draw_pointer(void) {
    const uint64_t x = (uint64_t)pointer_x;
    const uint64_t y = (uint64_t)pointer_y;
    graphics_fill_rect(x, y, 2U, 15U, 245, 250, 255);
    graphics_fill_rect(x + 1U, y + 1U, 2U, 11U, 245, 250, 255);
    graphics_fill_rect(x + 2U, y + 5U, 5U, 2U, 245, 250, 255);

    struct nexus_graphics_damage damage;
    if (desktop_virtio_gpu_active &&
        graphics_take_damage(&damage) &&
        !virtio_gpu_present_region(damage.x, damage.y,
                                   damage.width, damage.height)) {
        const volatile uint8_t *source =
            (const volatile uint8_t *)desktop_render_surface.address;
        volatile uint8_t *destination =
            (volatile uint8_t *)desktop_firmware_surface.address;
        const uint64_t row_bytes = desktop_firmware_surface.width * 4U;
        const uint64_t copy_bytes = row_bytes < desktop_firmware_surface.pitch
            ? row_bytes : desktop_firmware_surface.pitch;
        for (uint64_t row = 0U; row < desktop_firmware_surface.height; ++row) {
            const volatile uint8_t *src =
                source + row * desktop_render_surface.pitch;
            volatile uint8_t *dst =
                destination + row * desktop_firmware_surface.pitch;
            for (uint64_t column = 0U; column < copy_bytes; ++column)
                dst[column] = src[column];
        }
        virtio_gpu_stop();
        desktop_virtio_gpu_active = false;
        gpu_set_virtio_display_active(false);
        gpu_set_firmware_framebuffer_active(true);
        (void)graphics_init(&desktop_firmware_surface);
        serial_write("DESKTOP: VirtIO GPU failed; copied frame to firmware framebuffer fallback\r\n");
    }
}

static void refresh_files_listing(void) {
    files_vfs_ready = vfs_has_filesystem();
    files_listing_count = 0U;
    files_filtered_count = 0U;
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
    files_search_rebuild();
}

static void files_search_rebuild(void) {
    if (!nexus_file_list_filter(files_listing, files_listing_count,
            files_search_query, files_filtered_indices,
            FILES_LISTING_MAX_ENTRIES, &files_filtered_count)) {
        files_search_query[0] = '\0';
        files_search_query_length = 0U;
        files_filtered_count = files_listing_count;
        for (size_t i = 0U; i < files_listing_count; ++i)
            files_filtered_indices[i] = i;
    }
}

static void files_search_reset(void) {
    files_search_query[0] = '\0';
    files_search_query_length = 0U;
    files_search_focus = false;
    files_search_rebuild();
    files_selected_index = 0U;
    files_listing_first_index = 0U;
}

static void files_search_append(char value) {
    if (files_search_query_length >= sizeof(files_search_query) - 1U)
        return;
    if (value >= 'a' && value <= 'z')
        value = (char)(value - 'a' + 'A');
    files_search_query[files_search_query_length++] = value;
    files_search_query[files_search_query_length] = '\0';
    files_search_rebuild();
    files_selected_index = 0U;
    files_listing_first_index = 0U;
}

static void files_search_backspace(void) {
    if (files_search_query_length != 0U)
        files_search_query[--files_search_query_length] = '\0';
    files_search_rebuild();
    files_selected_index = 0U;
    files_listing_first_index = 0U;
}

static bool files_listing_get_entry(
    size_t index, struct nexus_vfs_dirent *entry) {
    if (entry == NULL || index >= files_filtered_count) return false;
    const size_t source_index = files_filtered_indices[index];
    if (source_index >= files_listing_count) return false;
    *entry = (struct nexus_vfs_dirent){0};
    size_t length = 0U;
    while (length < NEXUS_VFS_NAME_MAX - 1U &&
           files_listing[source_index].name[length] != '\0') {
        entry->name[length] = files_listing[source_index].name[length];
        ++length;
    }
    entry->name[length] = '\0';
    entry->type = files_listing[source_index].directory
        ? NEXUS_VFS_DIRECTORY : NEXUS_VFS_FILE;
    entry->size = files_listing[source_index].size;
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
    if (files_filtered_count == 0U) {
        files_listing_first_index = 0U;
        files_selected_index = 0U;
        return;
    }
    if (files_selected_index >= files_filtered_count)
        files_selected_index = files_filtered_count - 1U;
    if (files_selected_index < files_listing_first_index) {
        files_listing_first_index = files_selected_index;
    } else if (files_selected_index >=
               files_listing_first_index + FILES_LISTING_VISIBLE) {
        files_listing_first_index =
            files_selected_index - FILES_LISTING_VISIBLE + 1U;
    }
    const size_t maximum_first = files_filtered_count > FILES_LISTING_VISIBLE
        ? files_filtered_count - FILES_LISTING_VISIBLE : 0U;
    if (files_listing_first_index > maximum_first)
        files_listing_first_index = maximum_first;
}

static void files_listing_page(int direction) {
    if (files_filtered_count == 0U) return;
    const size_t maximum_first = files_filtered_count > FILES_LISTING_VISIBLE
        ? files_filtered_count - FILES_LISTING_VISIBLE : 0U;
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
    struct nexus_vfs_dirent selected_entry;
    const bool had_selection =
        files_listing_get_entry(files_selected_index, &selected_entry);
    if (had_selection) {
        for (size_t i = 0U; i < NEXUS_VFS_NAME_MAX; ++i)
            selected_name[i] = selected_entry.name[i];
    }

    files_sort_mode = (enum nexus_file_sort_mode)
        (((unsigned int)files_sort_mode + 1U) % NEXUS_FILE_SORT_MODE_COUNT);
    if (!nexus_file_list_sort(files_listing, files_listing_count,
                              files_sort_mode)) return;
    files_search_rebuild();

    if (had_selection) {
        for (size_t visible = 0U; visible < files_filtered_count; ++visible) {
            struct nexus_vfs_dirent entry;
            if (!files_listing_get_entry(visible, &entry)) continue;
            bool equal = true;
            for (size_t i = 0U; i < NEXUS_VFS_NAME_MAX; ++i) {
                if (selected_name[i] != entry.name[i]) {
                    equal = false;
                    break;
                }
                if (selected_name[i] == '\0') break;
            }
            if (equal) {
                files_selected_index = visible;
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
    if (files_selected_index >= files_filtered_count ||
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
    if (files_selected_index >= files_filtered_count ||
        !files_listing_get_entry(files_selected_index, &selected)) return;
    if (selected.type == NEXUS_VFS_DIRECTORY) {
        if (vfs_enter_directory(selected.name)) {
            files_search_reset();
            refresh_files_listing();
        }
        return;
    }
    files_editor_begin_selected();
}

static void files_set_app_status(const char *message) {
    size_t length = 0U;
    while (message != NULL && message[length] != '\0' &&
           length < sizeof(files_app_status) - 1U) {
        files_app_status[length] = message[length];
        ++length;
    }
    files_app_status[length] = '\0';
}

/* Run a selected file only through the validated Nexus ELF64 process loader.
 * This remains synchronous: the foreground shell is paused until the
 * bootstrap process exits and its user address space is reclaimed.
 */
static void files_run_selected(void) {
    files_run_output[0] = '\0';
    files_run_output_available = false;
    files_run_output_truncated = false;
    struct nexus_vfs_dirent selected;
    if (files_selected_index >= files_filtered_count ||
        !files_listing_get_entry(files_selected_index, &selected)) {
        files_set_app_status("SELECT A PROGRAM TO RUN");
        return;
    }
    if (selected.type != NEXUS_VFS_FILE) {
        files_set_app_status("FOLDERS CANNOT BE EXECUTED");
        return;
    }

    size_t name_length = 0U;
    while (name_length < NEXUS_VFS_NAME_MAX &&
           selected.name[name_length] != '\0') ++name_length;
    if (name_length == 0U || name_length >= NEXUS_PROCESS_NAME_MAX) {
        files_set_app_status("PROGRAM NAME LIMIT: 15 CHARACTERS");
        return;
    }

    struct nexus_process_result result;
    enum nexus_process_error error = NEXUS_PROCESS_ERROR_NONE;
    if (!process_execute_file(selected.name, &result, &error)) {
        const char *message = "NATIVE PROGRAM FAILED";
        switch (error) {
            case NEXUS_PROCESS_ERROR_NO_VOLUME:
                message = "NO MOUNTED USER VOLUME"; break;
            case NEXUS_PROCESS_ERROR_NOT_FOUND:
                message = "PROGRAM NOT FOUND"; break;
            case NEXUS_PROCESS_ERROR_IO:
                message = "PROGRAM READ ERROR"; break;
            case NEXUS_PROCESS_ERROR_TOO_LARGE:
                message = "PROGRAM EXCEEDS 128 KIB"; break;
            case NEXUS_PROCESS_ERROR_TABLE_FULL:
                message = "PROCESS TABLE FULL"; break;
            case NEXUS_PROCESS_ERROR_BAD_ELF:
                message = "INVALID OR UNSUPPORTED NEXUS ELF"; break;
            case NEXUS_PROCESS_ERROR_EXECUTION:
                message = "NATIVE PROGRAM EXECUTION FAILED"; break;
            default:
                break;
        }
        files_set_app_status(message);
        serial_write("FILES: run failed: ");
        serial_write(message);
        serial_write("\r\n");
        return;
    }

    const size_t output_length = usermode_console_output_copy(
        files_run_output, sizeof(files_run_output),
        &files_run_output_truncated);
    files_run_output_available =
        output_length != 0U || files_run_output_truncated;

    size_t out = 0U;
    const char *prefix = "PID ";
    while (prefix[out] != '\0' && out < sizeof(files_app_status) - 1U) {
        files_app_status[out] = prefix[out];
        ++out;
    }
    char number[21];
    format_u64(result.pid, number);
    for (size_t i = 0U; number[i] != '\0' &&
         out < sizeof(files_app_status) - 1U; ++i)
        files_app_status[out++] = number[i];
    const char *middle = " EXIT ";
    for (size_t i = 0U; middle[i] != '\0' &&
         out < sizeof(files_app_status) - 1U; ++i)
        files_app_status[out++] = middle[i];
    format_u64(result.exit_status, number);
    for (size_t i = 0U; number[i] != '\0' &&
         out < sizeof(files_app_status) - 1U; ++i)
        files_app_status[out++] = number[i];
    files_app_status[out] = '\0';

    serial_write("FILES: native ELF exited, pid=");
    serial_write_hex(result.pid);
    serial_write(" status=");
    serial_write_hex(result.exit_status);
    serial_write("\r\n");
}

static void files_editor_begin_operation(enum files_editor_state state) {
    struct nexus_vfs_dirent selected;
    if (state != FILES_EDITOR_RENAME && state != FILES_EDITOR_COPY &&
        state != FILES_EDITOR_MOVE) return;
    if (files_selected_index >= files_filtered_count ||
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
    files_filtered_count = 0U;
    files_selected_index = 0U;
    files_listing_first_index = 0U;
    files_search_reset();
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

static void browser_set_status(const char *message);

static bool browser_download_name_exists(const char *name) {
    if (name == NULL) return false;
    for (size_t i = 0U; i < vfs_root_entry_count(); ++i) {
        struct nexus_vfs_dirent entry;
        if (!vfs_root_entry(i, &entry)) continue;
        size_t j = 0U;
        bool same = true;
        while (name[j] != '\0' || entry.name[j] != '\0') {
            char left = name[j];
            char right = entry.name[j];
            if (left >= 'a' && left <= 'z')
                left = (char)(left - 'a' + 'A');
            if (right >= 'a' && right <= 'z')
                right = (char)(right - 'a' + 'A');
            if (left != right) { same = false; break; }
            ++j;
            if (j >= NEXUS_VFS_NAME_MAX) { same = false; break; }
        }
        if (same) return true;
    }
    return false;
}

static void browser_save_current_download(void) {
    if (!browser_download_ready || browser_download_length == 0U) {
        browser_set_status("No verified HTTPS file is ready to save");
        return;
    }
    /* Downloading is a file-management operation too. Mount installed Nexus
     * Data on demand so the user does not have to open Files first.
     */
    scan_files_once();
    if (!vfs_current_directory_writable()) {
        browser_set_status("Current folder is read-only; choose Nexus Data in Files");
        return;
    }
    if (browser_download_length > NEXUS_NXFS_MAX_FILE_BYTES) {
        browser_set_status("Download exceeds the 1 MiB file limit");
        return;
    }

    char target[NEXUS_DOWNLOAD_NAME_MAX];
    bool selected = false;
    for (uint32_t suffix = 0U; suffix < 1000U; ++suffix) {
        if (!nexus_download_unique_name(browser_download_name, suffix, target))
            break;
        if (!browser_download_name_exists(target)) {
            selected = true;
            break;
        }
    }
    if (!selected) {
        browser_set_status("Could not choose a unique download filename");
        return;
    }

    if (!vfs_write_file(target, browser_body_buffer, browser_download_length)) {
        browser_set_status(vfs_nxfs_writable()
            ? "Save failed: Nexus Data storage is full or unavailable"
            : (browser_download_length > NEXUS_VFS_RAM_MAX_FILE_SIZE
                ? "Persistent Nexus Data is needed for downloads over 4 KiB"
                : "Save failed: no writable Nexus Data or RAM storage"));
        return;
    }

    serial_write("BROWSER: saved verified HTTPS download ");
    serial_write(target);
    serial_write(" bytes=");
    serial_write_hex(browser_download_length);
    serial_write(vfs_nxfs_writable()
        ? " persistent\r\n" : " volatile RAM\r\n");

    char message[80] = "SAVED: ";
    size_t used = 7U;
    for (size_t i = 0U; target[i] != '\0' &&
         used < sizeof(message) - 1U; ++i)
        message[used++] = target[i];
    const char *storage = vfs_nxfs_writable()
        ? " TO NEXUS DATA" : " TO RAM ONLY";
    for (size_t i = 0U; storage[i] != '\0' &&
         used < sizeof(message) - 1U; ++i)
        message[used++] = storage[i];
    message[used] = '\0';
    browser_set_status(message);
    browser_download_ready = false;
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

static bool browser_search_fallback(bool search_request,
                                        bool *use_bing,
                                        bool *fallback_attempted) {
    if (!search_request || use_bing == NULL || fallback_attempted == NULL ||
        !*use_bing || *fallback_attempted) return false;
    *fallback_attempted = true;
    *use_bing = false;
    serial_write("NEXUS WEB SEARCH FALLBACK DUCKDUCKGO\r\n");
    browser_set_status("Bing response unusable; trying DuckDuckGo HTML...");
    return true;
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

static bool browser_content_disposition_is_attachment(
    const char *value) {
    static const char keyword[] = "attachment";
    if (value == NULL) return false;
    size_t i = 0U;
    while (keyword[i] != '\0') {
        char actual = value[i];
        char wanted = keyword[i];
        if (actual >= 'A' && actual <= 'Z')
            actual = (char)(actual - 'A' + 'a');
        if (actual != wanted) return false;
        ++i;
    }
    return value[i] == '\0' || value[i] == ';' ||
           value[i] == ' ' || value[i] == '\t';
}

static bool browser_content_type_is(const char *value,
                                       const char *expected) {
    if (value == NULL || expected == NULL) return false;
    size_t i = 0U;
    while (expected[i] != '\0') {
        char actual = value[i];
        char wanted = expected[i];
        if (actual >= 'A' && actual <= 'Z')
            actual = (char)(actual - 'A' + 'a');
        if (wanted >= 'A' && wanted <= 'Z')
            wanted = (char)(wanted - 'A' + 'a');
        if (actual == '\0' || actual != wanted) return false;
        ++i;
    }
    return value[i] == '\0' || value[i] == ';' || value[i] == ' ' ||
           value[i] == '\t';
}

static void browser_render_plain_body(const uint8_t *data, size_t length) {
    browser_clear_page();
    if (data == NULL) return;
    for (size_t i = 0U; i < length; ++i) {
        char value = (char)data[i];
        if ((unsigned char)value > 0x7EU) value = '?';
        browser_append_page_char(value);
    }
}

static void browser_render_body(const uint8_t *data, size_t length) {
    browser_clear_page();
    if (data == NULL) return;

    struct nexus_html_text_result result;
    if (!nexus_html_to_text(data, length, browser_text_buffer,
                            sizeof(browser_text_buffer), &result)) {
        browser_set_status("HTML text conversion rejected invalid input");
        return;
    }
    for (size_t i = 0U; i < result.output_length; ++i)
        browser_append_page_char(browser_text_buffer[i]);
    if (result.truncated || result.malformed)
        browser_content_truncated = result.truncated;
}

static void browser_append_page_text(const char *text) {
    if (text == NULL) return;
    for (size_t i = 0U; text[i] != '\0'; ++i)
        browser_append_page_char(text[i]);
}

static void browser_render_search_results(void) {
    browser_clear_page();
    browser_append_page_text("WEB SEARCH RESULTS - UP/DOWN SELECT, ENTER OPEN\n\n");
    for (size_t i = 0U; i < browser_search_result_count; ++i) {
        browser_append_page_char(i == browser_search_selected ? '>' : ' ');
        browser_append_page_char(' ');
        browser_append_page_char((char)('1' + (char)i));
        browser_append_page_char('.');
        browser_append_page_char(' ');
        browser_append_page_text(browser_search_results[i].title);
        browser_append_page_char('\n');
        browser_append_page_text("    ");
        browser_append_page_text(browser_search_results[i].snippet);
        browser_append_page_char('\n');
        browser_append_page_text("    ");
        browser_append_page_text(browser_search_results[i].url);
        browser_append_page_char('\n');
        browser_append_page_char('\n');
    }
    browser_line_scroll = 0U;
    browser_search_page = browser_search_result_count != 0U;
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
        if (i >= length) return false;
        char actual = value[i];
        char expected = prefix[i];
        if (actual >= 'A' && actual <= 'Z')
            actual = (char)(actual - 'A' + 'a');
        if (expected >= 'A' && expected <= 'Z')
            expected = (char)(expected - 'A' + 'a');
        if (actual != expected) return false;
        ++i;
    }
    return true;
}

static bool browser_address_is_search_query(void) {
    if (browser_address_length == 0U ||
        browser_starts_with(browser_address, browser_address_length, "http://") ||
        browser_starts_with(browser_address, browser_address_length, "https://"))
        return false;

    bool has_dot = false;
    for (size_t i = 0U; i < browser_address_length; ++i) {
        const char ch = browser_address[i];
        if (ch == ' ' || ch == '\t') return true;
        if (ch == '/') break;
        if (ch == '.') has_dot = true;
    }
    return !has_dot;
}

static bool browser_build_search_target(char *host, size_t host_capacity,
                                        size_t *host_length,
                                        char *path, size_t path_capacity,
                                        size_t *path_length) {
    static const char search_host[] = "www.bing.com";
    static const char search_path[] = "/search";
    if (host == NULL || host_length == NULL || path == NULL ||
        path_length == NULL || sizeof(search_host) > host_capacity ||
        sizeof(search_path) > path_capacity) return false;
    for (size_t i = 0U; i < sizeof(search_host); ++i)
        host[i] = search_host[i];
    for (size_t i = 0U; i < sizeof(search_path); ++i)
        path[i] = search_path[i];
    *host_length = sizeof(search_host) - 1U;
    *path_length = sizeof(search_path) - 1U;
    return true;
}

static bool browser_build_duckduckgo_search_target(
    char *host, size_t host_capacity, size_t *host_length,
    char *path, size_t path_capacity, size_t *path_length) {
    static const char search_host[] = "html.duckduckgo.com";
    if (host == NULL || host_length == NULL || path == NULL ||
        path_length == NULL || sizeof(search_host) > host_capacity ||
        path_capacity == 0U) return false;
    for (size_t i = 0U; i < sizeof(search_host); ++i)
        host[i] = search_host[i];
    *host_length = sizeof(search_host) - 1U;
    return nexus_browser_search_build_path(
        browser_address, browser_address_length,
        path, path_capacity, path_length);
}

static bool browser_is_http_redirect(uint16_t status_code) {
    return status_code == 301U || status_code == 302U ||
           status_code == 303U || status_code == 307U ||
           status_code == 308U;
}

static bool browser_load_ca_bundle(size_t *pem_length) {
    if (pem_length == NULL) return false;
    *pem_length = 0U;
    browser_ca_bundle_data = NULL;
    scan_files_once();

    /* NEXUS.CRT on the writable Nexus Data volume is an administrator
     * override. Invalid overrides fall back to the immutable release roots.
     */
    struct nexus_vfs_file file = {0};
    if (vfs_open_root_readonly(NEXUS_BROWSER_CA_FILE, &file)) {
        if (file.nxfs_file) {
            uint32_t file_size = 0U;
            if (vfs_file_size(&file, &file_size) &&
                file_size != 0U &&
                file_size <= NEXUS_BROWSER_CA_MAX_BYTES) {
                size_t total = 0U;
                bool read_ok = true;
                while (total < (size_t)file_size) {
                    size_t received = 0U;
                    if (!vfs_read(&file, browser_ca_bundle + total,
                                  (size_t)file_size - total, &received) ||
                        received == 0U ||
                        received > (size_t)file_size - total) {
                        read_ok = false;
                        break;
                    }
                    total += received;
                }
                vfs_close(&file);
                if (read_ok && total == (size_t)file_size) {
                    browser_ca_bundle[total] = 0U;
                    browser_ca_bundle_data = browser_ca_bundle;
                    *pem_length = total + 1U;
                    return true;
                }
            } else {
                vfs_close(&file);
            }
        } else {
            vfs_close(&file);
        }
        for (size_t i = 0U; i < sizeof(browser_ca_bundle); ++i)
            browser_ca_bundle[i] = 0U;
    }

    size_t builtin_length = 0U;
    const unsigned char *builtin =
        nexus_builtin_ca_bundle_get(&builtin_length);
    if (builtin == NULL || builtin_length < 100000U ||
        builtin_length > NEXUS_BROWSER_CA_MAX_BYTES + 1U ||
        builtin[builtin_length - 1U] != 0U) return false;
    browser_ca_bundle_data = builtin;
    *pem_length = builtin_length;
    return true;
}

static void browser_clear_ca_bundle(void) {
    volatile unsigned char *bytes =
        (volatile unsigned char *)browser_ca_bundle;
    for (size_t i = 0U; i < sizeof(browser_ca_bundle); ++i)
        bytes[i] = 0U;
    browser_ca_bundle_data = NULL;
}


static bool browser_https_send_all(struct nexus_https_connection *connection,
                                   const unsigned char *request,
                                   size_t request_length) {
    if (connection == NULL || request == NULL || request_length == 0U)
        return false;
    const uint64_t start = timer_ticks();
    size_t sent = 0U;
    while (sent < request_length && timer_ticks() - start < 500U) {
        const int result = nexus_https_write(
            connection, request + sent, request_length - sent);
        if (result > 0) {
            if ((size_t)result > request_length - sent) return false;
            sent += (size_t)result;
            continue;
        }
        if (result != NEXUS_HTTPS_WANT_READ &&
            result != NEXUS_HTTPS_WANT_WRITE) return false;
        net_poll();
        __asm__ volatile ("pause" ::: "memory");
    }
    return sent == request_length;
}

static bool browser_https_receive_response(
    struct nexus_https_connection *connection,
    uint8_t *response, size_t response_capacity, size_t *response_length,
    uint8_t *body, size_t body_capacity,
    struct nexus_http_response *parsed,
    bool *orderly_eof, int *failure_result) {
    if (connection == NULL || response == NULL || response_capacity == 0U ||
        response_length == NULL || body == NULL || body_capacity == 0U ||
        parsed == NULL || orderly_eof == NULL || failure_result == NULL)
        return false;
    *failure_result = 0;
    *response_length = 0U;
    *orderly_eof = false;
    const uint64_t start = timer_ticks();

    while (*response_length < response_capacity &&
           timer_ticks() - start < 3000U) {
        const int result = nexus_https_read(
            connection, response + *response_length,
            response_capacity - *response_length);
        if (result > 0) {
            if ((size_t)result > response_capacity - *response_length) {
                *failure_result = -1;
                return false;
            }
            *response_length += (size_t)result;

            /*
             * Do not treat an idle encrypted stream as a complete HTTP page.
             * Keep reading until the HTTP decoder confirms a self-delimited
             * body (Content-Length/chunked), an authenticated TLS EOF arrives,
             * or the overall deadline/capacity is reached.
             */
            if (nexus_http_decode_response(response, *response_length,
                                           body, body_capacity, parsed) &&
                (parsed->has_content_length || parsed->chunked))
                break;
            continue;
        }
        if (result == 0) {
            *orderly_eof = true;
            break;
        }
        if (result != NEXUS_HTTPS_WANT_READ &&
            result != NEXUS_HTTPS_WANT_WRITE) {
            *failure_result = result;
            return false;
        }
        net_poll();
        __asm__ volatile ("pause" ::: "memory");
    }
    if (*response_length == 0U && !*orderly_eof)
        *failure_result = -2; /* Response deadline expired with no TLS data. */
    else if (*response_length != 0U && !*orderly_eof &&
             timer_ticks() - start >= 3000U)
        *failure_result = -3; /* Partial response reached its overall deadline. */
    return *response_length != 0U;
}

static void browser_navigate(bool record_history) {
    size_t redirect_count = 0U;
    bool history_saved = false;
    bool search_request = browser_address_is_search_query();
    bool search_use_bing = true;
    bool search_fallback_attempted = false;
    browser_search_page = false;
    browser_search_result_count = 0U;
    browser_search_selected = 0U;
    browser_download_ready = false;
    browser_download_length = 0U;
    browser_download_name[0] = '\0';
browser_retry:
    browser_clear_page();
    if (browser_address_length == 0U) {
        browser_set_status("Enter a search query or URL");
        return;
    }

    bool use_https = true;
    char host[64];
    char path[NEXUS_BROWSER_SEARCH_PATH_MAX];
    size_t host_length = 0U;
    size_t path_length = 0U;
    if (search_request) {
        const bool target_ready = search_use_bing
            ? (browser_build_search_target(
                   host, sizeof(host), &host_length,
                   path, sizeof(path), &path_length) &&
               nexus_browser_search_build_bing_path(
                   browser_address, browser_address_length,
                   path, sizeof(path), &path_length))
            : browser_build_duckduckgo_search_target(
                   host, sizeof(host), &host_length,
                   path, sizeof(path), &path_length);
        if (!target_ready) {
            browser_set_status("Search query is empty, invalid or too long");
            return;
        }
    } else {
        size_t address_start = 0U;
        if (browser_starts_with(browser_address, browser_address_length,
                                "https://")) {
            address_start = 8U;
        } else if (browser_starts_with(browser_address, browser_address_length,
                                       "http://")) {
            use_https = false;
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
                if (ch == '#') break;
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
            browser_set_status(
                "Invalid host; ports and IPv6 literals are not supported");
            return;
        }
    }
    if (record_history && !history_saved) {
        if (!nexus_browser_history_push(&browser_history, browser_address)) {
            browser_set_status("Browser history could not store this address");
            return;
        }
        history_saved = true;
    }

    struct nexus_https_connection https = {0};
    bool https_open = false;
    if (use_https) {
        size_t ca_length = 0U;
        browser_set_status("Loading built-in Mozilla trust store...");
        if (!browser_load_ca_bundle(&ca_length)) {
            if (search_request)
                serial_write("NEXUS BING SEARCH TLS ROOTS MISSING\r\n");
            browser_set_status(
                "No built-in CA store; add a valid NEXUS.CRT to Nexus Data");
            browser_clear_ca_bundle();
            return;
        }
        browser_set_status("Opening verified TLS 1.3 connection...");
        const int connect_result = nexus_https_connect(
            &https, host, browser_ca_bundle_data, ca_length, 1800U);
        browser_clear_ca_bundle();
        if (connect_result != 0 || !nexus_https_is_verified(&https)) {
            nexus_https_close(&https);
            if (search_request) {
                if (connect_result == NEXUS_HTTPS_ERR_DNS) {
                    serial_write("NEXUS BING SEARCH DNS RESOLUTION FAILED\r\n");
                } else if (connect_result == NEXUS_HTTPS_ERR_CONNECT) {
                    serial_write("NEXUS BING SEARCH TCP CONNECT FAILED\r\n");
                } else if (connect_result == NEXUS_HTTPS_ERR_TIMEOUT) {
                    serial_write("NEXUS BING SEARCH HTTPS CONNECT TIMEOUT\r\n");
                } else if (connect_result == NEXUS_HTTPS_ERR_TLS) {
                    serial_write("NEXUS BING SEARCH TLS CERTIFICATE VERIFY FAILED\r\n");
                } else {
                    serial_write("NEXUS BING SEARCH TLS STACK FAILED code=");
                    serial_write_hex((uint64_t)(int64_t)connect_result);
                    serial_write("\r\n");
                }
            }
            if (browser_search_fallback(
                    search_request, &search_use_bing,
                    &search_fallback_attempted)) goto browser_retry;
            if (search_request) serial_write("NEXUS WEB SEARCH FAILED\r\n");
            browser_set_status(
                "HTTPS failed: check CA trust bundle, RTC clock and network");
            return;
        }
        https_open = true;
    } else {
        browser_set_status("Resolving host...");
        uint8_t address[4];
        if (!net_dns_resolve(host, address, 600U)) {
            browser_set_status("DNS failed; check DHCP/network");
            return;
        }
        browser_set_status("Opening HTTP connection...");
        if (!net_tcp_connect(address, 80U, 800U)) {
            browser_set_status("TCP connection to port 80 failed");
            return;
        }
    }

    char request[1536];
    size_t request_length = 0U;
    const bool request_ok =
        browser_append_text(request, sizeof(request), &request_length,
                            "GET ") &&
        browser_append_text(request, sizeof(request), &request_length, path) &&
        browser_append_text(request, sizeof(request), &request_length,
                            " HTTP/1.1\r\nHost: ") &&
        browser_append_text(request, sizeof(request), &request_length, host) &&
        (search_request
            ? (search_use_bing
                ? browser_append_text(request, sizeof(request), &request_length,
                    "\r\nUser-Agent: Mozilla/5.0 (X11; Nexus OS x86_64; rv:128.0) "
                    "Gecko/20100101 NexusBrowser/1.0\r\n"
                    "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n"
                    "Accept-Language: en-US,en;q=0.8\r\n"
                    "Accept-Encoding: identity\r\n"
                    "Cache-Control: no-cache\r\nConnection: close\r\n\r\n")
                : browser_append_text(request, sizeof(request), &request_length,
                    "\r\nUser-Agent: Mozilla/5.0 (X11; Linux x86_64) "
                    "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/128.0.0.0 Safari/537.36\r\n"
                    "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n"
                    "Accept-Language: en-US,en;q=0.8\r\n"
                    "Accept-Encoding: identity\r\n"
                    "Referer: https://html.duckduckgo.com/\r\n"
                    "Sec-Fetch-Dest: document\r\nSec-Fetch-Mode: navigate\r\n"
                    "Sec-Fetch-Site: same-origin\r\nUpgrade-Insecure-Requests: 1\r\n"
                    "Cache-Control: no-cache\r\nConnection: close\r\n\r\n"))
            : browser_append_text(request, sizeof(request), &request_length,
                "\r\nUser-Agent: NexusBrowser/0.3\r\n"
                "Accept: text/html,text/plain,*/*\r\n"
                "Accept-Encoding: identity\r\n"
                "Cache-Control: no-cache\r\n"
                "Connection: close\r\n\r\n"));
    bool request_sent = false;
    if (request_ok) {
        request_sent = use_https
            ? browser_https_send_all(
                &https, (const unsigned char *)request, request_length)
            : net_tcp_send(request, (uint16_t)request_length);
    }
    if (!request_sent) {
        if (https_open) nexus_https_close(&https);
        else net_tcp_close();
        if (browser_search_fallback(
                search_request, &search_use_bing,
                &search_fallback_attempted)) goto browser_retry;
        if (search_request) serial_write("NEXUS WEB SEARCH FAILED\r\n");
        browser_set_status("HTTP request could not be sent");
        return;
    }

    uint8_t *response = browser_response_buffer;
    uint8_t *body = browser_body_buffer;
    struct nexus_http_response parsed;
    size_t response_length = 0U;
    bool orderly_eof = false;
    int receive_error = 0;
    bool receive_ok = true;
    browser_set_status(use_https
        ? "Receiving encrypted HTTPS response..."
        : "Receiving unencrypted HTTP response...");
    if (use_https) {
        receive_ok = browser_https_receive_response(
            &https, response, NEXUS_BROWSER_RESPONSE_CAPACITY,
            &response_length, body, NEXUS_NXFS_MAX_FILE_BYTES,
            &parsed, &orderly_eof, &receive_error);
        if (search_request && !receive_ok) {
            serial_write("NEXUS BING SEARCH TLS READ FAILED code=");
            serial_write_hex((uint64_t)(int64_t)receive_error);
            serial_write("\r\n");
        }
        nexus_https_close(&https);
        https_open = false;
    } else {
        const uint64_t start = timer_ticks();
        uint64_t last_data = start;
        while (timer_ticks() - start < 600U &&
               (response_length == 0U || timer_ticks() - last_data < 40U) &&
               response_length < NEXUS_BROWSER_RESPONSE_CAPACITY) {
            net_poll();
            const size_t received =
                net_tcp_receive(response + response_length,
                                NEXUS_BROWSER_RESPONSE_CAPACITY - response_length);
            if (received != 0U) {
                response_length += received;
                last_data = timer_ticks();
            }
            __asm__ volatile ("pause" ::: "memory");
        }
        net_tcp_close();
    }

    if (!receive_ok || response_length == 0U) {
        if (browser_search_fallback(
                search_request, &search_use_bing,
                &search_fallback_attempted)) goto browser_retry;
        if (search_request) serial_write("NEXUS WEB SEARCH FAILED\r\n");
        browser_set_status(use_https
            ? "No complete TLS response received"
            : "No HTTP response received");
        return;
    }
    if (!nexus_http_decode_response(response, response_length, body,
                                    NEXUS_NXFS_MAX_FILE_BYTES, &parsed)) {
        if (browser_search_fallback(
                search_request, &search_use_bing,
                &search_fallback_attempted)) goto browser_retry;
        if (search_request) {
            serial_write("NEXUS BING SEARCH HTTP FRAMING INVALID length=");
            serial_write_hex(response_length);
            serial_write("\r\nNEXUS BING RAW RESPONSE PREFIX:\r\n");
            size_t debug_length = response_length < 256U
                ? response_length : 256U;
            char debug_line[65];
            for (size_t offset = 0U; offset < debug_length; offset += 64U) {
                size_t amount = debug_length - offset;
                if (amount > 64U) amount = 64U;
                for (size_t i = 0U; i < amount; ++i) {
                    const uint8_t value = response[offset + i];
                    debug_line[i] = value >= 0x20U && value <= 0x7EU
                        ? (char)value : '.';
                }
                debug_line[amount] = '\0';
                serial_write(debug_line);
                serial_write("\r\n");
            }
        }
        if (search_request) serial_write("NEXUS WEB SEARCH FAILED\r\n");
        browser_set_status("Invalid, truncated, or unsupported HTTP framing");
        return;
    }
    /* Require TLS close_notify for close-delimited bodies; a timed-out TLS
     * stream must not be mistaken for a complete page.
     */
    if (use_https && !parsed.has_content_length && !parsed.chunked &&
        !orderly_eof) {
        if (browser_search_fallback(
                search_request, &search_use_bing,
                &search_fallback_attempted)) goto browser_retry;
        if (search_request) serial_write("NEXUS WEB SEARCH FAILED\r\n");
        browser_set_status("HTTPS response ended before authenticated EOF");
        return;
    }
    if (browser_is_http_redirect(parsed.status_code)) {
        if (!parsed.has_location) {
            browser_set_status("Redirect response did not include Location");
            return;
        }
        if (redirect_count >= 5U) {
            browser_set_status("Redirect limit reached (maximum five hops)");
            return;
        }
        char redirect_target[sizeof(browser_address)];
        size_t redirect_length = 0U;
        size_t location_length = 0U;
        while (location_length < sizeof(parsed.location) &&
               parsed.location[location_length] != '\0')
            ++location_length;
        if (location_length == sizeof(parsed.location) ||
            !nexus_browser_resolve_redirect(
                host, host_length, path, path_length, use_https,
                parsed.location, location_length,
                redirect_target, sizeof(redirect_target), &redirect_length)) {
            browser_set_status(
                "Redirect rejected (invalid URL or HTTPS downgrade)");
            return;
        }
        for (size_t i = 0U; i < redirect_length; ++i)
            browser_address[i] = redirect_target[i];
        browser_address[redirect_length] = '\0';
        browser_address_length = redirect_length;
        search_request = false;
        ++redirect_count;
        goto browser_retry;
    }
    if (parsed.status_code < 200U || parsed.status_code >= 300U) {
        if (browser_search_fallback(
                search_request, &search_use_bing,
                &search_fallback_attempted)) goto browser_retry;
        if (search_request) serial_write("NEXUS WEB SEARCH FAILED\r\n");
        char number[21];
        char message[80] = "HTTP status ";
        format_u64(parsed.status_code, number);
        size_t length = 12U;
        for (size_t i = 0U; number[i] != '\0' &&
             length < sizeof(message) - 1U; ++i)
            message[length++] = number[i];
        message[length] = '\0';
        browser_set_status(message);
        return;
    }

    if (!search_request && use_https && parsed.body_length != 0U &&
        parsed.body_length <= NEXUS_NXFS_MAX_FILE_BYTES &&
        nexus_download_filename(browser_address,
            parsed.has_content_disposition ? parsed.content_disposition : NULL,
            parsed.has_content_type ? parsed.content_type : NULL,
            browser_download_name)) {
        browser_download_ready = true;
        browser_download_length = parsed.body_length;
    }

    bool rendered_plain_text = false;
    if (!search_request && browser_download_ready &&
        parsed.has_content_disposition &&
        browser_content_disposition_is_attachment(parsed.content_disposition)) {
        browser_clear_page();
        browser_append_page_text("DOWNLOAD READY\\n\\n");
        browser_append_page_text(browser_download_name);
        browser_append_page_text("\\n\\nSIZE: ");
        char download_size[21];
        format_u64(browser_download_length, download_size);
        browser_append_page_text(download_size);
        browser_append_page_text(" BYTES\\n\\n");
        browser_append_page_text("PRESS F6 OR CLICK SAVE TO STORE THIS FILE");
        browser_address_edited = false;
        browser_set_status("Verified HTTPS attachment ready; press F6 or click SAVE");
        return;
    }
    if (search_request &&
        (!parsed.has_content_type ||
         browser_content_type_is(parsed.content_type, "text/html") ||
         browser_content_type_is(parsed.content_type, "application/xhtml+xml"))) {
        size_t result_count = 0U;
        if (nexus_browser_search_is_challenge(body, parsed.body_length)) {
            serial_write(search_use_bing
                ? "NEXUS BING SEARCH CHALLENGE\r\n"
                : "NEXUS DUCKDUCKGO SEARCH CHALLENGE\r\n");
            if (browser_search_fallback(
                    search_request, &search_use_bing,
                    &search_fallback_attempted)) goto browser_retry;
            if (search_request) serial_write("NEXUS WEB SEARCH FAILED\r\n");
            browser_set_status(
                "Search provider blocked this request (anti-bot challenge)");
            browser_render_plain_body(body, parsed.body_length);
            return;
        }
        const bool results_ready = search_use_bing
            ? nexus_browser_search_parse_bing(
                body, parsed.body_length, browser_search_results,
                NEXUS_BROWSER_SEARCH_MAX_RESULTS, &result_count)
            : nexus_browser_search_parse(
                body, parsed.body_length, browser_search_results,
                NEXUS_BROWSER_SEARCH_MAX_RESULTS, &result_count);
        if (results_ready) {
            browser_search_result_count = result_count;
            browser_search_selected = 0U;
            serial_write(search_use_bing
                ? "NEXUS BING SEARCH RESULTS READY count="
                : "NEXUS DUCKDUCKGO SEARCH RESULTS READY count=");
            serial_write_hex((uint64_t)result_count);
            serial_write("\r\nNEXUS WEB SEARCH RESULTS READY count=");
            serial_write_hex((uint64_t)result_count);
            serial_write("\r\n");
            browser_render_search_results();
            browser_address_edited = false;
            browser_set_status(use_https
                ? "Web results received over verified HTTPS/TLS 1.3"
                : "Search provider response was not encrypted");
            return;
        }
        serial_write(search_use_bing
            ? "NEXUS BING SEARCH NO COMPATIBLE RESULT LINKS\r\n"
            : "NEXUS DUCKDUCKGO SEARCH NO COMPATIBLE RESULT LINKS\r\n");
        if (browser_search_fallback(
                search_request, &search_use_bing,
                &search_fallback_attempted)) goto browser_retry;
        if (search_request) serial_write("NEXUS WEB SEARCH FAILED\r\n");
        browser_set_status(
            "No compatible result links; showing the search page text");
    }
    if (parsed.has_content_type) {
        if (browser_content_type_is(parsed.content_type, "text/plain") ||
            browser_content_type_is(parsed.content_type, "application/json") ||
            browser_content_type_is(parsed.content_type, "application/xml") ||
            browser_content_type_is(parsed.content_type, "text/xml") ||
            browser_content_type_is(parsed.content_type, "text/css")) {
            browser_render_plain_body(body, parsed.body_length);
            rendered_plain_text = true;
        } else if (!browser_content_type_is(parsed.content_type, "text/html") &&
                   !browser_content_type_is(
                       parsed.content_type, "application/xhtml+xml")) {
            if (search_request) {
                if (browser_search_fallback(
                        search_request, &search_use_bing,
                        &search_fallback_attempted)) goto browser_retry;
                serial_write("NEXUS WEB SEARCH FAILED\r\n");
                browser_set_status("Search response has an unsupported content type");
                return;
            }
            if (!browser_download_ready) {
                browser_set_status(use_https
                    ? "Could not prepare this file for download"
                    : "Downloads require verified HTTPS; use an https:// URL");
                return;
            }
            browser_clear_page();
            browser_append_page_text("DOWNLOAD READY\n\n");
            browser_append_page_text(browser_download_name);
            browser_append_page_text("\n\nSIZE: ");
            char download_size[21];
            format_u64(browser_download_length, download_size);
            browser_append_page_text(download_size);
            browser_append_page_text(" BYTES\n\n");
            browser_append_page_text("PRESS F6 OR CLICK SAVE TO STORE THIS FILE");
            browser_address_edited = false;
            browser_set_status("Verified HTTPS file ready; press F6 or click SAVE");
            return;
        }
    }
    if (!rendered_plain_text)
        browser_render_body(body, parsed.body_length);
    browser_address_edited = false;
    if (browser_content_truncated) {
        browser_set_status(browser_download_ready
            ? "Page clipped; verified download copy is ready with F6"
            : "Page text clipped to the bounded browser view");
    } else if (use_https) {
        browser_set_status(browser_download_ready
            ? "Verified HTTPS page ready; press F6 to save a copy"
            : (rendered_plain_text
                ? "HTTPS certificate verified (TLS 1.3); plain text"
                : "HTTPS certificate verified (TLS 1.3); HTML text only"));
    } else {
        browser_set_status(rendered_plain_text
            ? "HTTP loaded without encryption; downloads disabled"
            : "HTTP loaded without encryption; downloads disabled");
    }
}
static void browser_history_navigate(bool forward) {
    char destination[sizeof(browser_address)];
    const bool moved = forward
        ? nexus_browser_history_forward(&browser_history, destination,
                                        sizeof(destination))
        : nexus_browser_history_back(&browser_history, destination,
                                     sizeof(destination));
    if (!moved) {
        browser_set_status(forward ? "No forward page in this session"
                                   : "No previous page in this session");
        return;
    }

    size_t length = 0U;
    while (length < sizeof(browser_address) - 1U &&
           destination[length] != '\0') {
        browser_address[length] = destination[length];
        ++length;
    }
    browser_address[length] = '\0';
    browser_address_length = length;
    browser_address_edited = false;
    browser_navigate(false);
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

static void installer_select_device(size_t index) {
    if (index >= blockdev_count() || index == installer_selected_device)
        return;
    installer_selected_device = index;
    installer_refresh_plan();
    installer_log_selected_target();
}

static void installer_select_delta(bool next) {
    const size_t count = blockdev_count();
    if (count == 0U) return;
    if (next) {
        if (installer_selected_device + 1U < count)
            installer_select_device(installer_selected_device + 1U);
    } else if (installer_selected_device > 0U) {
        installer_select_device(installer_selected_device - 1U);
    }
}

static bool installer_begin_confirmation(void) {
    const struct nexus_block_device *device =
        blockdev_at(installer_selected_device);
    installer_drive_dropdown_open = false;
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
    struct nexus_input_event normalized_event;
    bool extended_key = false;
    if (event->type == NEXUS_INPUT_KEYBOARD) {
        extended_key = (event->scancode & 0x0100U) != 0U;
        if (extended_key) {
            normalized_event = *event;
            normalized_event.scancode &= 0x00FFU;
            event = &normalized_event;
        }
    }
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
        if (clicked && active_view == DESKTOP_FILES)
            files_search_focus = false;

        if (window_resizing) {
            if (left_down && active_view != DESKTOP_HOME &&
                !window_maximized &&
                window_snap_state == DESKTOP_SNAP_NONE) {
                int64_t next_width = (int64_t)resize_width_origin +
                    pointer_x - resize_pointer_origin_x;
                int64_t next_height = (int64_t)resize_height_origin +
                    pointer_y - resize_pointer_origin_y;
                const uint64_t max_width =
                    screen_width > window_position_x + 16U
                        ? screen_width - window_position_x - 16U : 1U;
                const uint64_t bottom = clamp_min(screen_height / 11U, 48U);
                const uint64_t max_height =
                    screen_height > window_position_y + bottom
                        ? screen_height - window_position_y - bottom : 1U;
                const uint64_t min_width = max_width >= 560U
                    ? 560U : max_width;
                const uint64_t min_height = max_height >= 420U
                    ? 420U : max_height;
                if (next_width < (int64_t)min_width)
                    next_width = (int64_t)min_width;
                if (next_width > (int64_t)max_width)
                    next_width = (int64_t)max_width;
                if (next_height < (int64_t)min_height)
                    next_height = (int64_t)min_height;
                if (next_height > (int64_t)max_height)
                    next_height = (int64_t)max_height;
                window_custom_width = (uint64_t)next_width;
                window_custom_height = (uint64_t)next_height;
                window_custom_size = true;
                draw_shell();
                draw_pointer();
                return;
            }
            window_resizing = false;
        }

        if (window_dragging) {
            if (left_down && active_view != DESKTOP_HOME &&
                !window_maximized) {
                const struct desktop_window_rect bounds =
                    desktop_window_bounds();
                const uint64_t top = clamp_min(screen_height / 16U, 34U);
                const uint64_t bottom = clamp_min(screen_height / 11U, 48U);
                int64_t next_x = pointer_x - (int64_t)window_drag_offset_x;
                int64_t next_y = pointer_y - (int64_t)window_drag_offset_y;
                const int64_t max_x = (int64_t)(screen_width - bounds.width);
                const int64_t min_y = (int64_t)top;
                const int64_t max_y =
                    (int64_t)(screen_height - bottom - bounds.height);
                if (next_x < 0) next_x = 0;
                if (next_x > max_x) next_x = max_x;
                if (next_y < min_y) next_y = min_y;
                if (next_y > max_y) next_y = max_y;
                window_position_x = (uint64_t)next_x;
                window_position_y = (uint64_t)next_y;
                window_positioned = true;
                draw_shell();
                draw_pointer();
                return;
            }
            window_dragging = false;
            if (active_view != DESKTOP_HOME && !window_maximized) {
                const uint64_t top = clamp_min(screen_height / 16U, 34U);
                if ((uint64_t)pointer_y <= top + 8U) {
                    window_maximized = true;
                } else if ((uint64_t)pointer_x <= 8U) {
                    window_snap_state = DESKTOP_SNAP_LEFT;
                    window_custom_size = false;
                    window_positioned = false;
                } else if ((uint64_t)pointer_x + 8U >= screen_width) {
                    window_snap_state = DESKTOP_SNAP_RIGHT;
                    window_custom_size = false;
                    window_positioned = false;
                }
            }
        }

        bool shell_click_handled = false;
        if (clicked && !installer_session_mode) {
            const uint64_t taskbar_height = clamp_min(screen_height / 11U, 48U);
            const uint64_t taskbar_top = screen_height - taskbar_height;
            if (start_menu_open) {
                const uint64_t menu_width =
                    screen_width > 340U ? 304U : screen_width - 24U;
                const uint64_t menu_height = 258U;
                const uint64_t top_limit =
                    clamp_min(screen_height / 16U, 34U) + 8U;
                uint64_t menu_y = taskbar_top > menu_height
                    ? taskbar_top - menu_height + 4U : top_limit;
                if (menu_y < top_limit) menu_y = top_limit;
                if ((uint64_t)pointer_y >= taskbar_top &&
                    (uint64_t)pointer_x >= 12U &&
                    (uint64_t)pointer_x < 100U) {
                    desktop_start_menu_toggle();
                    shell_click_handled = true;
                } else if ((uint64_t)pointer_x >= 12U &&
                    (uint64_t)pointer_x < 12U + menu_width &&
                    (uint64_t)pointer_y >= menu_y &&
                    (uint64_t)pointer_y < menu_y + menu_height) {
                    const uint64_t rows_y = menu_y + 80U;
                    if ((uint64_t)pointer_y >= rows_y &&
                        (uint64_t)pointer_y < rows_y + 5U * 33U) {
                        const size_t row = (size_t)((uint64_t)pointer_y -
                            rows_y) / 33U;
                        const size_t app = desktop_start_menu_visible_app(row);
                        if (app < 5U) {
                            desktop_open_app(app);
                            shell_click_handled = true;
                        }
                    }
                    if (!shell_click_handled) shell_click_handled = true;
                } else {
                    start_menu_open = false;
                    start_menu_query_length = 0U;
                    start_menu_query[0] = '\0';
                }
            }
            if (!shell_click_handled &&
                (uint64_t)pointer_y >= taskbar_top) {
                if ((uint64_t)pointer_x >= 12U &&
                    (uint64_t)pointer_x < 100U) {
                    desktop_start_menu_toggle();
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
                const struct desktop_window_rect bounds =
                    desktop_window_bounds();
                const uint64_t pw = bounds.width;
                const uint64_t ph = bounds.height;
                const uint64_t px = bounds.x;
                const uint64_t py = bounds.y;
                const uint64_t controls_x = px + pw - 66U;
                if ((uint64_t)pointer_y >= py + 8U &&
                    (uint64_t)pointer_y < py + 28U &&
                    (uint64_t)pointer_x >= controls_x &&
                    (uint64_t)pointer_x < controls_x + 58U) {
                    const uint64_t control = (uint64_t)pointer_x - controls_x;
                    if (control < 14U) {
                        desktop_open_view(DESKTOP_HOME); /* Minimize */
                    } else if (control >= 22U && control < 36U) {
                        window_maximized = !window_maximized; /* Maximize / restore */
                        if (!window_maximized)
                            window_positioned = false;
                    } else if (control >= 44U) {
                        desktop_close_view(); /* Close and discard saved placement */
                    }
                    start_menu_open = false;
                    shell_click_handled = true;
                }
                if (!shell_click_handled && !window_maximized &&
                    window_snap_state == DESKTOP_SNAP_NONE &&
                    (uint64_t)pointer_x >= px + pw - 20U &&
                    (uint64_t)pointer_x < px + pw &&
                    (uint64_t)pointer_y >= py + ph - 20U &&
                    (uint64_t)pointer_y < py + ph) {
                    window_position_x = px;
                    window_position_y = py;
                    window_positioned = true;
                    resize_pointer_origin_x = pointer_x;
                    resize_pointer_origin_y = pointer_y;
                    resize_width_origin = pw;
                    resize_height_origin = ph;
                    window_resizing = true;
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
                    if (window_snap_state != DESKTOP_SNAP_NONE) {
                        window_custom_width = pw;
                        window_custom_height = ph;
                        window_custom_size = true;
                        window_snap_state = DESKTOP_SNAP_NONE;
                    }
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
        if (clicked && active_view == DESKTOP_SETTINGS) {
            const struct desktop_window_rect bounds = desktop_window_bounds();
            const uint64_t px = bounds.x;
            const uint64_t py = bounds.y;
            const uint64_t sx = px + 32U;
            const uint64_t sy = py + 82U;
            const uint64_t tab_y = py + 60U;
            if ((uint64_t)pointer_y >= tab_y &&
                (uint64_t)pointer_y < tab_y + 24U) {
                if ((uint64_t)pointer_x >= sx + 284U &&
                    (uint64_t)pointer_x < sx + 366U) {
                    settings_hardware_page = true;
                } else if ((uint64_t)pointer_x >= sx + 374U &&
                           (uint64_t)pointer_x < sx + 456U) {
                    settings_hardware_page = false;
                }
                draw_shell();
                draw_pointer();
                return;
            }
            bool changed = false;
            if ((uint64_t)pointer_y >= sy + 42U &&
                (uint64_t)pointer_y < sy + 74U) {
                if ((uint64_t)pointer_x >= sx &&
                    (uint64_t)pointer_x < sx + 116U &&
                    desktop_theme != DESKTOP_THEME_DARK) {
                    desktop_theme = DESKTOP_THEME_DARK;
                    changed = true;
                } else if ((uint64_t)pointer_x >= sx + 128U &&
                           (uint64_t)pointer_x < sx + 244U &&
                           desktop_theme != DESKTOP_THEME_LIGHT) {
                    desktop_theme = DESKTOP_THEME_LIGHT;
                    changed = true;
                }
            } else if ((uint64_t)pointer_y >= sy + 104U &&
                       (uint64_t)pointer_y < sy + 129U) {
                for (size_t i = 0U; i < 4U; ++i) {
                    const uint64_t x = sx + (uint64_t)i * 78U;
                    if ((uint64_t)pointer_x >= x &&
                        (uint64_t)pointer_x < x + 62U &&
                        desktop_accent_index != (uint8_t)i) {
                        desktop_accent_index = (uint8_t)i;
                        changed = true;
                        break;
                    }
                }
            }
            if (changed) desktop_preferences_save();
            draw_shell();
            draw_pointer();
            return;
        }
        if (clicked && active_view == DESKTOP_BROWSER &&
            browser_search_page && !browser_address_edited &&
            browser_search_result_count != 0U) {
            const struct desktop_window_rect bounds = desktop_window_bounds();
            const uint64_t text_top = bounds.y + 48U + 108U;
            if ((uint64_t)pointer_y >= text_top &&
                (uint64_t)pointer_y < text_top +
                    NEXUS_BROWSER_VISIBLE_LINES * 24U) {
                const size_t visible_row =
                    (size_t)(((uint64_t)pointer_y - text_top) / 24U);
                const size_t document_line = browser_line_scroll + visible_row;
                const size_t selected =
                    nexus_browser_search_result_for_line(document_line);
                if (selected < browser_search_result_count) {
                    browser_search_selected = selected;
                    const char *target = browser_search_results[selected].url;
                    size_t length = 0U;
                    while (length < sizeof(browser_address) - 1U &&
                           target[length] != '\0') {
                        browser_address[length] = target[length];
                        ++length;
                    }
                    browser_address[length] = '\0';
                    browser_address_length = length;
                    browser_address_edited = true;
                    browser_navigate(true);
                    draw_shell();
                    draw_pointer();
                    return;
                }
            }
        }
        if (clicked && active_view == DESKTOP_INSTALLER) {
            const struct desktop_window_rect bounds = desktop_window_bounds();
            const uint64_t px = bounds.x;
            const uint64_t py = bounds.y;
            const int64_t relative_x = pointer_x - (int64_t)(px + 32U);
            const int64_t relative_y = pointer_y - (int64_t)(py + 48U);
            if (relative_x >= 0 && relative_y >= 0) {
                if (installer_ui_state == INSTALLER_UI_SELECT) {
                    if (installer_drive_dropdown_open) {
                        const size_t count = blockdev_count();
                        const uint64_t popup_bottom =
                            82U + (uint64_t)(count < NEXUS_BLOCKDEV_MAX_DEVICES
                                ? count : NEXUS_BLOCKDEV_MAX_DEVICES) * 28U;
                        if (relative_x < 520 && relative_y >= 82 &&
                            (uint64_t)relative_y < popup_bottom) {
                            const size_t selected =
                                (size_t)((uint64_t)relative_y - 82U) / 28U;
                            installer_select_device(selected);
                        }
                        installer_drive_dropdown_open = false;
                    } else if (relative_y >= 48 && relative_y < 76 &&
                               relative_x < 520) {
                        installer_drive_dropdown_open = true;
                    } else if (relative_y >= 158 && relative_y < 182) {
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
                    } else if (relative_y >= 310 && relative_y < 334 &&
                               relative_x >= 228 && relative_x < 448) {
                        (void)installer_begin_confirmation();
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
            const struct desktop_window_rect bounds = desktop_window_bounds();
            const uint64_t px = bounds.x;
            const uint64_t py = bounds.y;
            const uint64_t tx = px + 32U;
            const uint64_t ty = py + 82U;
            const int64_t relative_x = pointer_x - (int64_t)tx;
            const int64_t relative_search_x = relative_x - 312;
            const int64_t relative_search_y =
                pointer_y - (int64_t)(ty + 297U);
            if (relative_search_x >= 0 && relative_search_x < 196 &&
                relative_search_y >= 0 && relative_search_y < 23) {
                files_search_focus = true;
                draw_shell();
                draw_pointer();
                return;
            }
            files_search_focus = false;
            const int64_t relative_y = pointer_y - (int64_t)(ty + 270U);
            if (relative_y >= 0 && relative_y < 25 &&
                relative_x >= 0 && relative_x < 456) {
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
                    if (files_selected_index < files_filtered_count &&
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
                    files_run_selected();
                } else if (action == 5U) {
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
                    index < files_filtered_count) {
                    files_selected_index = index;
                    files_listing_ensure_selection_visible();
                }
            }
        }
        if (clicked && active_view == DESKTOP_BROWSER) {
            const struct desktop_window_rect bounds = desktop_window_bounds();
            const int64_t save_x = (int64_t)(bounds.x + 32U + 420U);
            const int64_t save_y = (int64_t)(bounds.y + 82U + 398U);
            if (pointer_x >= save_x && pointer_x < save_x + 112 &&
                pointer_y >= save_y && pointer_y < save_y + 28) {
                browser_save_current_download();
                draw_shell();
                draw_pointer();
                return;
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
        if (event->mouse_wheel != 0) {
            const struct nexus_input_event wheel_key = {
                .type = NEXUS_INPUT_KEYBOARD,
                .scancode = event->mouse_wheel > 0 ? 0x49U : 0x51U,
                .mouse_buttons = 0U,
                .mouse_dx = 0,
                .mouse_dy = 0,
                .mouse_wheel = 0
            };
            desktop_handle_input(&wheel_key);
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
    if (key_code == 0x1DU) {
        if (extended_key) keyboard_right_control = !key_released;
        else keyboard_left_control_down = !key_released;
        keyboard_left_control =
            keyboard_left_control_down || keyboard_right_control;
        return;
    }
    if (key_code == 0x38U) {
        if (extended_key) keyboard_right_alt_down = !key_released;
        else keyboard_left_alt_down = !key_released;
        keyboard_alt = keyboard_left_alt_down || keyboard_right_alt_down;
        return;
    }
    if (key_code == 0x5BU || key_code == 0x5CU) {
        if (!key_released) {
            keyboard_windows = true;
            keyboard_windows_used = false;
        } else {
            if (keyboard_windows && !keyboard_windows_used &&
                active_view != DESKTOP_INSTALLER) {
                desktop_start_menu_toggle();
                draw_shell();
                draw_pointer();
            }
            keyboard_windows = false;
            keyboard_windows_used = false;
        }
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

    if (active_view != DESKTOP_INSTALLER && keyboard_alt &&
        event->scancode == 0x0FU) {
        size_t current_app = 4U;
        bool current_found = false;
        for (size_t app = 0U; app < 5U; ++app) {
            if (desktop_view_for_app(app) == active_view) {
                current_app = app;
                current_found = true;
                break;
            }
        }
        size_t next_app;
        if (!current_found) {
            next_app = keyboard_left_shift || keyboard_right_shift ? 4U : 0U;
        } else {
            next_app = keyboard_left_shift || keyboard_right_shift
                ? (current_app + 4U) % 5U : (current_app + 1U) % 5U;
        }
        desktop_open_app(next_app);
        draw_shell();
        draw_pointer();
        return;
    }
    if (!installer_session_mode && keyboard_alt &&
        event->scancode == 0x3EU && active_view != DESKTOP_HOME) {
        desktop_close_view();
        draw_shell();
        draw_pointer();
        return;
    }
    if (keyboard_windows && event->scancode != 0x5BU &&
        event->scancode != 0x5CU &&
        active_view != DESKTOP_INSTALLER) {
        if (event->scancode == 0x4BU || event->scancode == 0x4DU ||
            event->scancode == 0x48U || event->scancode == 0x50U) {
            keyboard_windows_used = true;
            if (active_view != DESKTOP_HOME) {
                if (event->scancode == 0x4BU) {
                    window_maximized = false;
                    window_snap_state = DESKTOP_SNAP_LEFT;
                    window_custom_size = false;
                    window_positioned = false;
                } else if (event->scancode == 0x4DU) {
                    window_maximized = false;
                    window_snap_state = DESKTOP_SNAP_RIGHT;
                    window_custom_size = false;
                    window_positioned = false;
                } else if (event->scancode == 0x48U) {
                    window_maximized = true;
                } else if (window_maximized) {
                    window_maximized = false;
                    window_positioned = false;
                } else if (window_snap_state != DESKTOP_SNAP_NONE) {
                    window_snap_state = DESKTOP_SNAP_NONE;
                    window_custom_size = false;
                    window_positioned = false;
                } else {
                    desktop_open_view(DESKTOP_HOME);
                }
            }
            window_dragging = false;
            window_resizing = false;
            draw_shell();
            draw_pointer();
            return;
        }
        if (event->scancode == 0x12U || event->scancode == 0x17U ||
            event->scancode == 0x20U) {
            keyboard_windows_used = true;
            if (event->scancode == 0x12U) desktop_open_app(0U); /* Win+E */
            else if (event->scancode == 0x17U) desktop_open_app(2U); /* Win+I */
            else desktop_open_view(DESKTOP_HOME); /* Win+D */
            draw_shell();
            draw_pointer();
            return;
        }
    }
    if (active_view != DESKTOP_INSTALLER &&
        keyboard_left_control && event->scancode == 0x01U) {
        desktop_start_menu_toggle();
        draw_shell();
        draw_pointer();
        return;
    }
    if (start_menu_open && active_view != DESKTOP_INSTALLER) {
        bool redraw = false;
        if (event->scancode == 0x01U) {
            start_menu_open = false;
            start_menu_query_length = 0U;
            start_menu_query[0] = '\0';
            redraw = true;
        } else if (event->scancode == 0x1CU) {
            const size_t app =
                desktop_start_menu_visible_app(start_menu_selection);
            if (app < 5U) {
                desktop_open_app(app);
                redraw = true;
            }
        } else if (event->scancode == 0x48U) {
            if (start_menu_selection > 0U) --start_menu_selection;
            redraw = true;
        } else if (event->scancode == 0x50U) {
            const size_t count = desktop_start_menu_visible_count();
            if (count != 0U && start_menu_selection + 1U < count)
                ++start_menu_selection;
            redraw = true;
        } else if (event->scancode == 0x0EU) {
            if (start_menu_query_length != 0U)
                start_menu_query[--start_menu_query_length] = '\0';
            start_menu_selection = 0U;
            redraw = true;
        } else {
            char value = terminal_scancode_char(event->scancode);
            if ((value >= 'a' && value <= 'z') ||
                (value >= 'A' && value <= 'Z') ||
                (value >= '0' && value <= '9') || value == ' ') {
                if (start_menu_query_length <
                    sizeof(start_menu_query) - 1U) {
                    start_menu_query[start_menu_query_length++] =
                        desktop_search_upper(value);
                    start_menu_query[start_menu_query_length] = '\0';
                    start_menu_selection = 0U;
                    redraw = true;
                }
            }
        }
        desktop_start_menu_clamp_selection();
        if (redraw) {
            draw_shell();
            draw_pointer();
        }
        return;
    }
    if (active_view == DESKTOP_INSTALLER) {
        if ((event->scancode & 0x80U) != 0U ||
            installer_ui_state == INSTALLER_UI_RUNNING) return;
        if (event->scancode == 0x01U) {
            if (installer_ui_state == INSTALLER_UI_SELECT &&
                installer_drive_dropdown_open) {
                installer_drive_dropdown_open = false;
            } else if (installer_ui_state == INSTALLER_UI_SELECT) {
                if (!installer_session_mode)
                    desktop_open_view(DESKTOP_HOME);
            } else {
                installer_ui_state = INSTALLER_UI_SELECT;
                installer_input_length = 0U;
                installer_input[0] = '\0';
                installer_set_status("Select a supported target disk");
            }
        } else if (installer_ui_state == INSTALLER_UI_SELECT) {
            if (event->scancode == 0x0FU) {
                installer_drive_dropdown_open = !installer_drive_dropdown_open;
                serial_write(installer_drive_dropdown_open
                    ? "NEXUS INSTALLER DROPDOWN OPEN\r\n"
                    : "NEXUS INSTALLER DROPDOWN CLOSED\r\n");
            } else if (event->scancode == 0x3BU) {
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
                if (installer_drive_dropdown_open) {
                    installer_drive_dropdown_open = false;
                    serial_write("NEXUS INSTALLER DROPDOWN CLOSED\r\n");
                } else {
                    (void)installer_begin_confirmation();
                }
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
    if (active_view == DESKTOP_SETTINGS) {
        if (event->scancode == 0x01U) {
            desktop_open_view(DESKTOP_HOME);
        } else if (event->scancode == 0x14U) {
            desktop_toggle_theme();
        } else if (event->scancode == 0x1EU) {
            desktop_cycle_accent();
        } else if (event->scancode == 0x23U) {
            settings_hardware_page = true;
        } else if (event->scancode == 0x19U) {
            settings_hardware_page = false;
        } else {
            return;
        }
        draw_shell();
        draw_pointer();
        return;
    }
    if (active_view == DESKTOP_BROWSER) {
        if ((event->scancode & 0x80U) != 0U) return;
        if (event->scancode == 0x40U ||
            (keyboard_left_control && event->scancode == 0x1FU)) {
            browser_save_current_download();
            draw_shell(); draw_pointer(); return;
        }
        if (event->scancode == 0x01U) {
            desktop_open_view(DESKTOP_HOME);
            draw_shell(); draw_pointer(); return;
        }
        if (keyboard_alt && event->scancode == 0x4BU) {
            browser_history_navigate(false);
        } else if (keyboard_alt && event->scancode == 0x4DU) {
            browser_history_navigate(true);
        } else if (event->scancode == 0x1CU) {
            if (browser_search_page && !browser_address_edited &&
                browser_search_result_count != 0U &&
                browser_search_selected < browser_search_result_count) {
                const char *target =
                    browser_search_results[browser_search_selected].url;
                size_t length = 0U;
                while (length < sizeof(browser_address) - 1U &&
                       target[length] != '\0') {
                    browser_address[length] = target[length];
                    ++length;
                }
                browser_address[length] = '\0';
                browser_address_length = length;
                browser_address_edited = true;
            }
            browser_navigate(true);
        } else if (browser_search_page && !browser_address_edited &&
                   event->scancode == 0x48U) {
            if (browser_search_selected > 0U) --browser_search_selected;
            browser_render_search_results();
        } else if (browser_search_page && !browser_address_edited &&
                   event->scancode == 0x50U) {
            if (browser_search_selected + 1U < browser_search_result_count)
                ++browser_search_selected;
            browser_render_search_results();
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
            browser_address_edited = true;
        } else {
            const char ch = terminal_scancode_char(event->scancode);
            if (ch != '\0' && browser_address_length < sizeof(browser_address)-1U) {
                browser_address[browser_address_length++] = ch;
                browser_address[browser_address_length] = '\0';
                browser_address_edited = true;
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
        if (keyboard_left_control && event->scancode == 0x21U) {
            files_search_focus = true;
            draw_shell(); draw_pointer(); return;
        }
        if (files_search_focus) {
            if (event->scancode == 0x01U) {
                files_search_reset();
            } else if (event->scancode == 0x1CU) {
                files_search_focus = false;
            } else if (event->scancode == 0x0EU) {
                files_search_backspace();
            } else {
                const char ch = terminal_scancode_char(event->scancode);
                if (ch >= 0x20 && ch <= 0x7E)
                    files_search_append(ch);
                else if (event->scancode == 0x48U ||
                         event->scancode == 0x50U)
                    files_search_focus = false;
            }
            draw_shell(); draw_pointer(); return;
        }
        if (keyboard_left_control && event->scancode == 0x1CU) {
            files_run_selected();
            draw_shell(); draw_pointer(); return;
        }
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
            files_search_reset();
            if (vfs_parent_directory()) refresh_files_listing();
            draw_shell(); draw_pointer(); return;
        }
        if (event->scancode == 0x48U && files_filtered_count > 0U) {
            if (files_selected_index > 0U) --files_selected_index;
            files_listing_ensure_selection_visible();
            draw_shell();
            draw_pointer();
            return;
        }
        if (event->scancode == 0x50U && files_filtered_count > 0U) {
            if (files_selected_index + 1U < files_filtered_count)
                ++files_selected_index;
            files_listing_ensure_selection_visible();
            draw_shell();
            draw_pointer();
            return;
        }
        if (event->scancode == 0x49U && files_filtered_count > 0U) {
            files_listing_page(-1);
            draw_shell();
            draw_pointer();
            return;
        }
        if (event->scancode == 0x51U && files_filtered_count > 0U) {
            files_listing_page(1);
            draw_shell();
            draw_pointer();
            return;
        }
        if (event->scancode == 0x47U && files_filtered_count > 0U) {
            files_selected_index = 0U;
            files_listing_ensure_selection_visible();
            draw_shell();
            draw_pointer();
            return;
        }
        if (event->scancode == 0x4FU && files_filtered_count > 0U) {
            files_selected_index = files_filtered_count - 1U;
            files_listing_ensure_selection_visible();
            draw_shell();
            draw_pointer();
            return;
        }
        if (event->scancode == 0x53U &&
            files_selected_index < files_filtered_count) {
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
        if (event->scancode == 0x1CU && files_selected_index < files_filtered_count) {
            struct nexus_vfs_dirent selected;
            if (!files_listing_get_entry(files_selected_index, &selected)) {
                draw_shell();
                draw_pointer();
                return;
            }
            if (selected.type == NEXUS_VFS_DIRECTORY) {
                if (vfs_enter_directory(selected.name)) {
                    files_search_reset();
                    refresh_files_listing();
                }
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
        installer_drive_dropdown_open = false;
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
    desktop_open_view(next_view);
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

    static const char expected_output[] =
        "NEXUS FILE EXEC OK; SDK FILE IO/WRITE OK; FORMAT=0023";
    char captured_output[sizeof(expected_output)] = {0};
    bool output_truncated = false;
    const size_t output_length = usermode_console_output_copy(
        captured_output, sizeof(captured_output), &output_truncated);
    bool output_matches =
        output_length == sizeof(expected_output) - 1U &&
        !output_truncated;
    for (size_t i = 0U; i < output_length && output_matches; ++i) {
        if (captured_output[i] != expected_output[i])
            output_matches = false;
    }

    serial_write("DESKTOP: HELLO.ELF exit status=");
    serial_write_hex(result.exit_status);
    serial_write("\r\n");
    if (!output_matches) {
        serial_write("DESKTOP: HELLO.ELF native SDK output mismatch\r\n");
        return NEXUS_DESKTOP_PROCESS_TEST_FAILED;
    }
    serial_write("DESKTOP: HELLO.ELF native SDK file read/write, formatted output and capture passed\r\n");
    return result.exit_status == 7U
        ? NEXUS_DESKTOP_PROCESS_TEST_PASSED
        : NEXUS_DESKTOP_PROCESS_TEST_FAILED;
}
