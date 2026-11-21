#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <limine.h>

__attribute__((used, section(".limine_requests")))
static volatile uint64_t base_revision[] = LIMINE_BASE_REVISION(6);
__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t requests_start[] = LIMINE_REQUESTS_START_MARKER;
__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID, .revision = 0
};
__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t requests_end[] = LIMINE_REQUESTS_END_MARKER;

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}
static void serial_init(void) {
    outb(0x3F9, 0x00); outb(0x3FB, 0x80);
    outb(0x3F8, 0x01); outb(0x3F9, 0x00);
    outb(0x3FB, 0x03); outb(0x3FA, 0xC7); outb(0x3FC, 0x0B);
}
static void serial_putc(char c) {
    for (unsigned int spin = 0; spin < 100000; ++spin) {
        if (inb(0x3FD) & 0x20) { outb(0x3F8, (uint8_t)c); return; }
    }
}
static void serial_write(const char *s) { while (*s) serial_putc(*s++); }

static uint8_t glyph_row(char c, unsigned int row) {
    static const struct { char c; uint8_t r[7]; } font[] = {
        {'A',{14,17,17,31,17,17,17}}, {'B',{30,17,17,30,17,17,30}},
        {'D',{30,17,17,17,17,17,30}}, {'E',{31,16,16,30,16,16,31}},
        {'K',{17,18,20,24,20,18,17}}, {'L',{16,16,16,16,16,16,31}},
        {'N',{17,25,25,21,19,19,17}}, {'O',{14,17,17,17,17,17,14}},
        {'R',{30,17,17,30,20,18,17}}, {'S',{15,16,16,14,1,1,30}},
        {'T',{31,4,4,4,4,4,4}}, {'U',{17,17,17,17,17,17,14}},
        {'X',{17,17,10,4,10,17,17}}, {' ',{0,0,0,0,0,0,0}}
    };
    for (size_t i = 0; i < sizeof(font) / sizeof(font[0]); ++i)
        if (font[i].c == c) return font[i].r[row];
    return 0;
}
static uint32_t rgb(const struct limine_framebuffer *fb,
                    uint8_t red, uint8_t green, uint8_t blue) {
    uint64_t rmax = ((uint64_t)1 << fb->red_mask_size) - 1;
    uint64_t gmax = ((uint64_t)1 << fb->green_mask_size) - 1;
    uint64_t bmax = ((uint64_t)1 << fb->blue_mask_size) - 1;
    return (uint32_t)(((red * rmax / 255) << fb->red_mask_shift) |
                      ((green * gmax / 255) << fb->green_mask_shift) |
                      ((blue * bmax / 255) << fb->blue_mask_shift));
}
static void pixel(struct limine_framebuffer *fb, uint64_t x, uint64_t y,
                  uint32_t color) {
    volatile uint32_t *row = (volatile uint32_t *)
        ((uintptr_t)fb->address + y * fb->pitch);
    row[x] = color;
}
static void draw_text(struct limine_framebuffer *fb, const char *text,
                      uint64_t y, uint32_t scale, uint32_t color) {
    size_t length = 0;
    while (text[length]) ++length;
    uint64_t width = (uint64_t)length * 6 * scale - scale;
    uint64_t x0 = fb->width > width ? (fb->width - width) / 2 : 0;
    for (size_t i = 0; i < length; ++i) {
        for (unsigned int gy = 0; gy < 7; ++gy) {
            uint8_t bits = glyph_row(text[i], gy);
            for (unsigned int gx = 0; gx < 5; ++gx) {
                if (!(bits & (1u << (4u - gx)))) continue;
                for (uint32_t sy = 0; sy < scale; ++sy)
                    for (uint32_t sx = 0; sx < scale; ++sx) {
                        uint64_t px = x0 + (i * 6 + gx) * scale + sx;
                        uint64_t py = y + gy * scale + sy;
                        if (px < fb->width && py < fb->height)
                            pixel(fb, px, py, color);
                    }
            }
        }
    }
}
static void show_splash(void) {
    if (framebuffer_request.response == NULL ||
        framebuffer_request.response->framebuffer_count == 0) return;
    struct limine_framebuffer *fb = framebuffer_request.response->framebuffers[0];
    if (fb == NULL || fb->address == NULL || fb->bpp != 32 ||
        fb->memory_model != LIMINE_FRAMEBUFFER_RGB) return;
    uint32_t bg = rgb(fb, 7, 12, 28);
    uint32_t cyan = rgb(fb, 20, 210, 235);
    uint32_t white = rgb(fb, 235, 245, 255);
    for (uint64_t y = 0; y < fb->height; ++y)
        for (uint64_t x = 0; x < fb->width; ++x) pixel(fb, x, y, bg);
    uint32_t scale = fb->width >= 800 ? 6 : 3;
    uint64_t title_y = fb->height / 2 - (7 * scale);
    draw_text(fb, "NEXUS OS", title_y, scale, white);
    draw_text(fb, "KERNEL STARTED", title_y + 12 * scale, scale, cyan);
}
__attribute__((noreturn))
static void halt_forever(void) {
    for (;;) __asm__ volatile ("cli; hlt" ::: "memory");
}
void kmain(void) {
    serial_init();
    serial_write("\r\nNEXUS OS KERNEL STARTED\r\n");
    serial_write("Architecture: x86_64\r\n");
    serial_write("Boot protocol: Limine\r\n");
    if (!LIMINE_BASE_REVISION_SUPPORTED(base_revision)) {
        serial_write("ERROR: unsupported Limine base revision\r\n");
        halt_forever();
    }
    show_splash();
    serial_write("NEXUS K0 BOOT COMPLETE\r\n");
    halt_forever();
}
