#include "desktop.h"

#include "blockdev.h"
#include "gpt.h"
#include "fat32.h"
#include "vfs.h"

#include "graphics.h"
#include "input.h"
#include "serial.h"
#include "memory.h"
#include "timer.h"

#include <stddef.h>
#include <stdint.h>

enum desktop_view {
    DESKTOP_HOME = 0,
    DESKTOP_FILES,
    DESKTOP_BROWSER,
    DESKTOP_SETTINGS,
    DESKTOP_TERMINAL
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
static char files_preview[41];
static char files_listing[6][13];
static size_t files_listing_count;
static size_t files_selected_index;
static char terminal_input[64];
static size_t terminal_input_length;
static char terminal_output[160];

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
    if (active_view == DESKTOP_TERMINAL) window_title = "NEXUS TERMINAL";
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
        uint64_t tile_width = (panel_width - 100U) / 4U;
        if (tile_width > 190U) tile_width = 190U;
        const char *labels[4] = {"FILES F1", "BROWSER F2", "SETTINGS F3", "TERMINAL F4"};
        const uint8_t accent[4][3] = {
            {45, 177, 202}, {117, 136, 244},
            {82, 198, 155}, {217, 164, 83}
        };
        for (uint32_t i = 0; i < 4U; ++i) {
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
                           "SELECT AN APP WITH F1 F2 F3 OR F4",
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
            if (files_fat32_ok) {
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
        if (files_fat32_ok) {
            graphics_draw_text(text_x, text_y + 288U,
                               "ROOT DIRECTORY (READ ONLY)",
                               1, 108, 216, 229);
            for (size_t n = 0; n < files_listing_count; ++n) {
                const uint64_t column = n % 2U;
                const uint64_t line = n / 2U;
                const bool selected = n == files_selected_index;
                if (selected)
                    graphics_fill_rect(text_x + column * 260U - 6U,
                                       text_y + 307U + line * 24U,
                                       240U, 20U, 34, 78, 105);
                graphics_draw_text(text_x + column * 260U,
                                   text_y + 312U + line * 24U,
                                   files_listing[n], 1,
                                   selected ? 119 : 233,
                                   selected ? 229 : 243,
                                   selected ? 211 : 252);
            }
            graphics_draw_text(text_x, text_y + 390U,
                               "UP/DOWN SELECT  ENTER PREVIEW",
                               1, 170, 191, 211);
        }
        graphics_draw_text(text_x, text_y + 342U,
                           "PRESS ESC TO RETURN HOME",
                           1, 119, 229, 211);
    } else if (active_view == DESKTOP_TERMINAL) {
        graphics_draw_text(text_x, text_y, "NEXUS COMMAND SHELL",
                           2, 239, 249, 255);
        graphics_draw_text(text_x, text_y + 42U,
                           "READ ONLY SYSTEM COMMANDS",
                           1, 108, 216, 229);
        graphics_draw_text(text_x, text_y + 82U, terminal_output,
                           1, 170, 225, 190);
        graphics_draw_text(text_x, text_y + 128U, "nexus> ",
                           1, 119, 229, 211);
        graphics_draw_text(text_x + 56U, text_y + 128U, terminal_input,
                           1, 239, 249, 255);
        graphics_draw_text(text_x, text_y + 180U,
                           "HELP  ABOUT  MEM  UPTIME  LS  CAT FILE  CLEAR",
                           1, 170, 191, 211);
        graphics_draw_text(text_x, text_y + 210U,
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
                       "F1 FILES  F2 BROWSER  F3 SETTINGS  F4 TERMINAL  ESC HOME",
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

static void scan_files_once(void) {
    if (files_scanned) return;
    files_scanned = true;
    files_scan_ok = false;
    files_fat32_ok = false;
    files_partition_count = 0;
    files_first_lba = 0;
    files_last_lba = 0;
    for (size_t n = 0; n < sizeof(files_first_name); ++n)
        files_first_name[n] = '\0';
    for (size_t n = 0; n < sizeof(files_preview); ++n)
        files_preview[n] = '\0';
    files_listing_count = 0;
    files_selected_index = 0;
    for (size_t n = 0; n < 6U; ++n)
        for (size_t k = 0; k < sizeof(files_listing[n]); ++k)
            files_listing[n][k] = '\0';

    for (size_t i = 0; i < blockdev_count(); ++i) {
        const struct nexus_block_device *device = blockdev_at(i);
        if (device == NULL || !gpt_scan_device(device)) continue;
        const size_t count = gpt_partition_count();
        for (size_t p = 0; p < count; ++p) {
            const struct nexus_gpt_partition *part = gpt_partition_at(p);
            if (part == NULL || !vfs_mount_root_fat32(device, part)) continue;
            files_scan_ok = true;
            files_fat32_ok = true;
            files_partition_count = count;
            files_first_lba = part->first_lba;
            files_last_lba = part->last_lba;
            for (size_t file_index = 0;
                 file_index < vfs_root_entry_count(); ++file_index) {
                struct nexus_vfs_dirent entry;
                if (!vfs_root_entry(file_index, &entry)) continue;
                if (files_listing_count < 6U) {
                    size_t name_length = 0;
                    while (name_length < sizeof(files_listing[0]) - 1U &&
                           entry.name[name_length] != '\0') {
                        files_listing[files_listing_count][name_length] =
                            entry.name[name_length];
                        ++name_length;
                    }
                    files_listing[files_listing_count][name_length] = '\0';
                    ++files_listing_count;
                }
                if (files_first_name[0] == '\0') {
                    size_t name_length = 0;
                    while (name_length < sizeof(files_first_name) - 1U &&
                           entry.name[name_length] != '\0') {
                        files_first_name[name_length] = entry.name[name_length];
                        ++name_length;
                    }
                    files_first_name[name_length] = '\0';
                }
                if (entry.type != NEXUS_VFS_FILE || entry.size == 0U)
                    continue;
                struct nexus_vfs_file file;
                if (!vfs_open_readonly(entry.name, &file)) continue;
                uint8_t preview_data[sizeof(files_preview) - 1U];
                size_t amount = entry.size;
                if (amount > sizeof(preview_data)) amount = sizeof(preview_data);
                size_t bytes_read = 0;
                const bool read_ok =
                    vfs_read(&file, preview_data, amount, &bytes_read);
                vfs_close(&file);
                if (!read_ok) continue;
                for (size_t n = 0; n < bytes_read; ++n) {
                    const uint8_t ch = preview_data[n];
                    files_preview[n] = (ch >= 0x20U && ch <= 0x7EU)
                        ? (char)ch : ' ';
                }
                files_preview[bytes_read] = '\0';
                break;
            }
            serial_write("FILES: mounted read-only FAT32 volume on ");
            serial_write(device->name);
            serial_write("\r\n");
            return;
        }
    }
    serial_write("FILES: no supported read-only FAT32 GPT volume found\r\n");
}

static char terminal_scancode_char(uint8_t code) {
    static const char map[58] = {
        [0x02]='1',[0x03]='2',[0x04]='3',[0x05]='4',[0x06]='5',
        [0x07]='6',[0x08]='7',[0x09]='8',[0x0A]='9',[0x0B]='0',
        [0x0C]='-',[0x0D]='=',[0x10]='q',[0x11]='w',[0x12]='e',
        [0x13]='r',[0x14]='t',[0x15]='y',[0x16]='u',[0x17]='i',
        [0x18]='o',[0x19]='p',[0x1A]='[',[0x1B]=']',[0x1E]='a',
        [0x1F]='s',[0x20]='d',[0x21]='f',[0x22]='g',[0x23]='h',
        [0x24]='j',[0x25]='k',[0x26]='l',[0x27]=';',[0x28]='\'',
        [0x2B]='\\',[0x2C]='z',[0x2D]='x',[0x2E]='c',[0x2F]='v',
        [0x30]='b',[0x31]='n',[0x32]='m',[0x33]=',',[0x34]='.',
        [0x35]='/',[0x39]=' '
    };
    return code < sizeof(map) ? map[code] : '\0';
}

static void terminal_execute(void) {
    terminal_output[0] = '\0';
    if (terminal_input_length == 0U) return;
    terminal_input[terminal_input_length] = '\0';
    if (terminal_input[0] == 'h' && terminal_input[1] == 'e' &&
        terminal_input[2] == 'l' && terminal_input[3] == 'p' &&
        terminal_input[4] == '\0') {
        const char *s = "help about mem uptime ls cat FILE clear";
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
    } else if (terminal_input[0]=='l' && terminal_input[1]=='s' &&
               terminal_input[2]=='\0') {
        size_t out=0;
        if (!vfs_root_mounted()) {
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
    } else if (terminal_input[0]=='c' && terminal_input[1]=='a' &&
               terminal_input[2]=='t' && terminal_input[3]==' ') {
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
    terminal_input_length=0;
    terminal_input[0]='\0';
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
        if (clicked && active_view == DESKTOP_FILES && files_fat32_ok) {
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
            const uint64_t tx = px + 32U;
            const uint64_t ty = py + 82U;
            const int64_t relative_y = pointer_y - (int64_t)(ty + 307U);
            const int64_t relative_x = pointer_x - (int64_t)tx;
            if (relative_y >= 0 && relative_y < 60 &&
                relative_x >= 0 && relative_x < 500) {
                const size_t row = (size_t)relative_y / 24U;
                const size_t column = (size_t)relative_x / 260U;
                const size_t index = row * 2U + column;
                if (column < 2U && index < files_listing_count)
                    files_selected_index = index;
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
            uint64_t tw = (pw - 100U) / 4U;
            if (tw > 190U) tw = 190U;
            if ((uint64_t)pointer_y >= ty && (uint64_t)pointer_y < ty + 74U) {
                for (uint32_t i = 0; i < 4U; ++i) {
                    const uint64_t tx = px + 28U + (uint64_t)i * (tw + 10U);
                    if ((uint64_t)pointer_x >= tx &&
                        (uint64_t)pointer_x < tx + tw) {
                        active_view = i == 0U ? DESKTOP_FILES :
                                      (i == 1U ? DESKTOP_BROWSER :
                                      (i == 2U ? DESKTOP_SETTINGS : DESKTOP_TERMINAL));
                        if (active_view == DESKTOP_FILES) scan_files_once();
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
    /* Set-1 make codes only; break codes are ignored. */
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
    if (active_view == DESKTOP_FILES && files_fat32_ok) {
        if (event->scancode == 0x48U && files_listing_count > 0U) {
            if (files_selected_index > 0U) --files_selected_index;
            draw_shell();
            draw_pointer();
            return;
        }
        if (event->scancode == 0x50U && files_listing_count > 0U) {
            if (files_selected_index + 1U < files_listing_count)
                ++files_selected_index;
            draw_shell();
            draw_pointer();
            return;
        }
        if (event->scancode == 0x1CU && files_selected_index < files_listing_count) {
            struct nexus_vfs_dirent selected;
            if (vfs_root_entry(files_selected_index, &selected) &&
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
        case 0x01U: next_view = DESKTOP_HOME; break;
        default: return;
    }
    if (next_view == DESKTOP_FILES) scan_files_once();
    if (next_view == active_view) return;
    active_view = next_view;
    draw_shell();
    draw_pointer();
    serial_write("DESKTOP: view changed by keyboard shortcut\\r\\n");
}
