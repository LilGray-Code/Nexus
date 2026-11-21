#include "desktop.h"

#include "blockdev.h"
#include "gpt.h"
#include "fat32.h"

#include "graphics.h"
#include "input.h"
#include "serial.h"

#include <stddef.h>
#include <stdint.h>

enum desktop_view {
    DESKTOP_HOME = 0,
    DESKTOP_FILES,
    DESKTOP_BROWSER,
    DESKTOP_SETTINGS
};

static enum desktop_view active_view = DESKTOP_HOME;
static uint64_t screen_width;
static uint64_t screen_height;
static bool desktop_ready;
static bool mouse_left_was_down;
static void draw_pointer(void);
static int64_t pointer_x;
static int64_t pointer_y;
static bool files_scanned;
static bool files_scan_ok;
static size_t files_partition_count;
static uint64_t files_first_lba;
static uint64_t files_last_lba;
static bool files_fat32_ok;
static char files_first_name[13];

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

static void draw_shell(void) {
    const uint64_t width = screen_width;
    const uint64_t height = screen_height;
    const uint64_t top_bar = clamp_min(height / 16U, 34U);
    const uint64_t bottom_bar = clamp_min(height / 11U, 48U);

    for (uint64_t y = 0; y < height; ++y) {
        const uint8_t red = (uint8_t)(7U + (y * 7U / height));
        const uint8_t green = (uint8_t)(16U + (y * 23U / height));
        const uint8_t blue = (uint8_t)(34U + (y * 34U / height));
        graphics_fill_rect(0, y, width, 1, red, green, blue);
    }

    graphics_fill_rect(0, 0, width, top_bar, 11, 20, 39);
    graphics_fill_rect(0, top_bar - 2U, width, 2, 27, 183, 209);
    graphics_draw_text(22, top_bar / 2U - 4U, "NEXUS", 1, 229, 246, 255);
    graphics_draw_text(width > 190U ? width - 170U : 8U,
                       top_bar / 2U - 4U, "SYSTEM READY", 1,
                       119, 229, 211);

    uint64_t panel_width = width * 3U / 4U;
    uint64_t panel_height = height * 53U / 100U;
    if (panel_width > 920U) panel_width = 920U;
    if (panel_height > 480U) panel_height = 480U;
    if (panel_width > width - 16U) panel_width = width - 16U;
    if (panel_height > height - top_bar - bottom_bar - 16U)
        panel_height = height - top_bar - bottom_bar - 16U;

    const uint64_t panel_x = (width - panel_width) / 2U;
    const uint64_t panel_y = top_bar +
        (height - top_bar - bottom_bar - panel_height) / 2U;

    graphics_fill_rect(panel_x + 5U, panel_y + 6U, panel_width,
                       panel_height, 2, 7, 19);
    graphics_fill_rect(panel_x, panel_y, panel_width, panel_height,
                       18, 31, 54);
    graphics_draw_rect(panel_x, panel_y, panel_width, panel_height,
                       56, 88, 119);
    graphics_fill_rect(panel_x, panel_y, panel_width, 38U, 24, 42, 69);

    const char *window_title = "NEXUS DESKTOP";
    if (active_view == DESKTOP_FILES) window_title = "FILES";
    if (active_view == DESKTOP_BROWSER) window_title = "NEXUS BROWSER";
    if (active_view == DESKTOP_SETTINGS) window_title = "SETTINGS";
    graphics_draw_text(panel_x + 18U, panel_y + 15U, window_title,
                       1, 225, 241, 255);

    const uint64_t text_x = panel_x + 32U;
    const uint64_t text_y = panel_y + 82U;
    if (active_view == DESKTOP_HOME) {
        graphics_draw_text(text_x, text_y, "WELCOME TO NEXUS OS",
                           2, 239, 249, 255);
        graphics_draw_text(text_x + 2U, text_y + 30U,
                           "ORIGINAL KERNEL DESKTOP PREVIEW",
                           1, 108, 216, 229);

        const uint64_t tile_y = text_y + 76U;
        uint64_t tile_width = (panel_width - 80U) / 3U;
        if (tile_width > 190U) tile_width = 190U;
        const char *labels[3] = {"FILES F1", "BROWSER F2", "SETTINGS F3"};
        const uint8_t accent[3][3] = {
            {45, 177, 202}, {117, 136, 244}, {82, 198, 155}
        };
        for (uint32_t i = 0; i < 3U; ++i) {
            const uint64_t tile_x = panel_x + 28U +
                                    (uint64_t)i * (tile_width + 10U);
            graphics_fill_rect(tile_x, tile_y, tile_width, 74U,
                               27, 45, 72);
            graphics_draw_rect(tile_x, tile_y, tile_width, 74U,
                               58, 87, 117);
            graphics_fill_rect(tile_x + 12U, tile_y + 12U, 22U, 22U,
                               accent[i][0], accent[i][1], accent[i][2]);
            graphics_draw_text(tile_x + 12U, tile_y + 47U, labels[i],
                               1, 233, 243, 252);
        }
        graphics_draw_text(text_x, panel_y + panel_height - 42U,
                           "SELECT AN APP WITH F1 F2 OR F3",
                           1, 170, 191, 211);
    } else if (active_view == DESKTOP_FILES) {
        graphics_draw_text(text_x, text_y, "NEXUS FILES",
                           2, 239, 249, 255);
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
        } else if (!files_scan_ok) {
            graphics_draw_text(text_x, text_y + 108U,
                               "NO VALID GPT PARTITION TABLE FOUND",
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
            if (files_fat32_ok) {
                graphics_draw_text(text_x, text_y + 228U,
                                   "ROOT FILE:", 1, 233, 243, 252);
                graphics_draw_text(text_x + 100U, text_y + 228U,
                                   files_first_name, 1, 119, 229, 211);
            } else {
                graphics_draw_text(text_x, text_y + 228U,
                                   "NO SUPPORTED FAT32 VOLUME",
                                   1, 239, 177, 119);
            }
        }
        graphics_draw_text(text_x, text_y + 208U,
                           "PRESS ESC TO RETURN HOME",
                           1, 119, 229, 211);
    } else if (active_view == DESKTOP_BROWSER) {
        graphics_draw_text(text_x, text_y, "NEXUS BROWSER",
                           2, 239, 249, 255);
        graphics_draw_text(text_x, text_y + 48U,
                           "BROWSER ENGINE PORT IS IN DEVELOPMENT",
                           1, 108, 216, 229);
        graphics_draw_text(text_x, text_y + 78U,
                           "THE ORIGINAL BROWSER UI IS PRESERVED",
                           1, 233, 243, 252);
        graphics_draw_text(text_x, text_y + 108U,
                           "NETWORK AND TLS SUPPORT ARE REQUIRED",
                           1, 170, 191, 211);
        graphics_draw_text(text_x, text_y + 158U,
                           "PRESS ESC TO RETURN HOME",
                           1, 119, 229, 211);
    } else {
        graphics_draw_text(text_x, text_y, "SYSTEM SETTINGS",
                           2, 239, 249, 255);
        graphics_draw_text(text_x, text_y + 48U,
                           "ARCHITECTURE X86 64",
                           1, 108, 216, 229);
        graphics_draw_text(text_x, text_y + 78U,
                           "BOOT MODES UEFI AND LEGACY BIOS",
                           1, 233, 243, 252);
        graphics_draw_text(text_x, text_y + 108U,
                           "DISPLAY AND INPUT DRIVERS IN DEVELOPMENT",
                           1, 170, 191, 211);
        graphics_draw_text(text_x, text_y + 158U,
                           "PRESS ESC TO RETURN HOME",
                           1, 119, 229, 211);
    }

    graphics_fill_rect(0, height - bottom_bar, width, bottom_bar,
                       10, 19, 37);
    graphics_fill_rect(0, height - bottom_bar, width, 2,
                       38, 75, 105);
    graphics_draw_text(18U, height - bottom_bar / 2U - 4U,
                       "F1 FILES  F2 BROWSER  F3 SETTINGS  ESC HOME",
                       1, 178, 202, 221);
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
    mouse_left_was_down = false;
    pointer_x = (int64_t)(screen_width / 2U);
    pointer_y = (int64_t)(screen_height / 2U);
    desktop_ready = true;
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
            uint64_t tw = (pw - 80U) / 3U;
            if (tw > 190U) tw = 190U;
            if ((uint64_t)pointer_y >= ty && (uint64_t)pointer_y < ty + 74U) {
                for (uint32_t i = 0; i < 3U; ++i) {
                    const uint64_t tx = px + 28U + (uint64_t)i * (tw + 10U);
                    if ((uint64_t)pointer_x >= tx &&
                        (uint64_t)pointer_x < tx + tw) {
                        active_view = i == 0U ? DESKTOP_FILES :
                                      (i == 1U ? DESKTOP_BROWSER : DESKTOP_SETTINGS);
                        serial_write("DESKTOP: app tile opened by mouse\\r\\n");
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
    enum desktop_view next_view = active_view;
    switch (event->scancode) {
        case 0x3BU: next_view = DESKTOP_FILES; break;
        case 0x3CU: next_view = DESKTOP_BROWSER; break;
        case 0x3DU: next_view = DESKTOP_SETTINGS; break;
        case 0x01U: next_view = DESKTOP_HOME; break;
        default: return;
    }
    if (next_view == DESKTOP_FILES && !files_scanned) {
        files_scanned = true;
        files_scan_ok = false;
        files_partition_count = 0;
        for (size_t i = 0; i < blockdev_count(); ++i) {
            const struct nexus_block_device *device = blockdev_at(i);
            if (device != NULL && gpt_scan_device(device)) {
                files_scan_ok = true;
                files_partition_count = gpt_partition_count();
                if (files_partition_count > 0U) {
                    const struct nexus_gpt_partition *part = gpt_partition_at(0);
                    if (part != NULL) {
                        files_first_lba = part->first_lba;
                        files_last_lba = part->last_lba;
                        files_fat32_ok = fat32_mount(device, part);
                        if (files_fat32_ok && fat32_root_entry_count() > 0U) {
                            const struct nexus_fat32_entry *entry =
                                fat32_root_entry_at(0);
                            if (entry != NULL) {
                                size_t n = 0;
                                while (n < sizeof(files_first_name) - 1U &&
                                       entry->name[n] != '\\0') {
                                    files_first_name[n] = entry->name[n];
                                    ++n;
                                }
                                files_first_name[n] = '\\0';
                            }
                        }
                    }
                }
                serial_write("FILES: read-only GPT scan found partitions on ");
                serial_write(device->name);
                serial_write("\\r\\n");
                break;
            }
        }
        if (!files_scan_ok)
            serial_write("FILES: no valid GPT disk discovered during user-requested scan\\r\\n");
    }
    if (next_view == active_view) return;
    active_view = next_view;
    draw_shell();
    draw_pointer();
    serial_write("DESKTOP: view changed by keyboard shortcut\\r\\n");
}
