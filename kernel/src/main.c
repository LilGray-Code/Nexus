#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <limine.h>

#include "acpi.h"
#include "interrupts.h"
#include "ioapic.h"
#include "memory.h"
#include "pci.h"
#include "serial.h"
#include "scheduler.h"
#include "timer.h"

__attribute__((used, section(".limine_requests")))
static volatile uint64_t base_revision[] = LIMINE_BASE_REVISION(6);
__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t requests_start[] = LIMINE_REQUESTS_START_MARKER;
__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID, .revision = 0
};
__attribute__((used, section(".limine_requests")))
static volatile struct limine_memmap_request memory_map_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID, .revision = 0
};
__attribute__((used, section(".limine_requests")))
static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID, .revision = 0
};
__attribute__((used, section(".limine_requests")))
static volatile struct limine_rsdp_request rsdp_request = {
    .id = LIMINE_RSDP_REQUEST_ID, .revision = 0
};
__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t requests_end[] = LIMINE_REQUESTS_END_MARKER;

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

static volatile uint32_t scheduler_count_a;
static volatile uint32_t scheduler_count_b;
static volatile uint8_t scheduler_trace[6];
static volatile uint32_t scheduler_trace_count;

static void scheduler_worker_a(void *argument) {
    (void)argument;
    for (uint32_t i = 0; i < 3; ++i) {
        scheduler_count_a++;
        if (scheduler_trace_count < 6)
            scheduler_trace[scheduler_trace_count++] = 1;
        scheduler_yield();
    }
}

static void scheduler_worker_b(void *argument) {
    (void)argument;
    for (uint32_t i = 0; i < 3; ++i) {
        scheduler_count_b++;
        if (scheduler_trace_count < 6)
            scheduler_trace[scheduler_trace_count++] = 2;
        scheduler_yield();
    }
}

__attribute__((noreturn))
static void halt_forever(void) {
    for (;;) __asm__ volatile ("cli; hlt" ::: "memory");
}

void kmain(void) {
    __asm__ volatile ("cli" : : : "memory");
    serial_init();
    serial_write("\r\nNEXUS OS KERNEL STARTED\r\n");
    serial_write("Architecture: x86_64\r\n");
    serial_write("Boot protocol: Limine\r\n");

    if (!LIMINE_BASE_REVISION_SUPPORTED(base_revision)) {
        serial_write("ERROR: unsupported Limine base revision\r\n");
        halt_forever();
    }

    idt_init();
    serial_write("K1: CPU exception IDT installed (vectors 0-31)\r\n");

    if (!memory_init(memory_map_request.response, hhdm_request.response)) {
        serial_write("ERROR: physical memory map initialization failed\r\n");
        halt_forever();
    }

    uint64_t first_page = 0;
    if (!memory_alloc_page(&first_page) ||
        memory_phys_to_virt(first_page) == NULL) {
        serial_write("ERROR: physical page allocation failed\r\n");
        halt_forever();
    }

    serial_write("K1: physical memory allocator ready\r\n");
    serial_write("Usable memory bytes: ");
    serial_write_hex(memory_total_bytes());
    serial_write("\r\nRemaining bootstrap pages bytes: ");
    serial_write_hex(memory_free_bytes());
    serial_write("\r\nFirst allocated physical page: ");
    serial_write_hex(first_page);
    serial_write("\r\n");

    if (rsdp_request.response == NULL ||
        !acpi_init(rsdp_request.response->address)) {
        serial_write("ERROR: ACPI RSDP/MADT discovery failed\r\n");
        halt_forever();
    }
    serial_write("NEXUS ACPI MADT READY\r\n");

    if (!ioapic_probe()) {
        serial_write("ERROR: I/O APIC read-only probe failed\r\n");
        halt_forever();
    }
    serial_write("NEXUS IOAPIC PROBE READY\r\n");

    if (!pci_init()) {
        serial_write("WARNING: no PCI devices discovered\r\n");
    }
    serial_write("NEXUS PCI ENUMERATION READY\r\n");

    timer_init(100);
    serial_write("IOAPIC: direct timer routing held pending interrupt-frame validation; using PIC/PIT virtual-wire mode\r\n");

    serial_write("K2: PIT configured for 100 Hz\r\n");

    const uint64_t target_tick = timer_ticks() + 5;
    __asm__ volatile ("sti" : : : "memory");
    while (timer_ticks() < target_tick) {
        __asm__ volatile ("hlt" : : : "memory");
    }
    __asm__ volatile ("cli" : : : "memory");

    serial_write("K2: hardware timer ticks received: ");
    serial_write_hex(timer_ticks());
    serial_write("\r\n");

    scheduler_init();
    scheduler_count_a = 0;
    scheduler_count_b = 0;
    scheduler_trace_count = 0;

    if (!scheduler_create_thread(scheduler_worker_a, NULL) ||
        !scheduler_create_thread(scheduler_worker_b, NULL)) {
        serial_write("ERROR: unable to create cooperative scheduler test threads\r\n");
        halt_forever();
    }

    scheduler_start();

    bool scheduler_ok = scheduler_count_a == 3 &&
                        scheduler_count_b == 3 &&
                        scheduler_trace_count == 6;
    static const uint8_t expected_trace[6] = {1, 2, 1, 2, 1, 2};
    for (uint32_t i = 0; i < 6 && scheduler_ok; ++i) {
        if (scheduler_trace[i] != expected_trace[i]) scheduler_ok = false;
    }

    if (!scheduler_ok) {
        serial_write("ERROR: cooperative scheduler round-robin test failed\r\n");
        halt_forever();
    }
    serial_write("K2: cooperative scheduler test passed (A-B-A-B-A-B)\r\n");

    show_splash();
    serial_write("NEXUS K2 COOPERATIVE SCHEDULER READY\r\n");
    serial_write("NEXUS K2 TIMER INTERRUPT READY\r\n");
    serial_write("NEXUS K1 MEMORY+EXCEPTIONS READY\r\n");
    serial_write("NEXUS K0 BOOT COMPLETE\r\n");
    halt_forever();
}
