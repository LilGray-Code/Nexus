#include "input.h"

#include "serial.h"
#include "timer.h"
#include "usb_hid.h"

#include <stddef.h>
#include <stdint.h>

#define PS2_DATA_PORT 0x60U
#define PS2_STATUS_PORT 0x64U
#define PS2_COMMAND_PORT 0x64U
#define PS2_QUEUE_SIZE 64U
#define PS2_QUEUE_MASK (PS2_QUEUE_SIZE - 1U)
#define PS2_WAIT_LIMIT 100000U

static struct nexus_input_event event_queue[PS2_QUEUE_SIZE];
static volatile uint8_t event_head;
static volatile uint8_t event_tail;
static uint8_t mouse_packet[3];
static uint8_t mouse_packet_index;
static bool keyboard_ready;
static bool mouse_ready;

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static bool wait_input_empty(void) {
    for (uint32_t i = 0; i < PS2_WAIT_LIMIT; ++i) {
        if ((inb(PS2_STATUS_PORT) & 0x02U) == 0) return true;
        __asm__ volatile ("pause");
    }
    return false;
}

static bool wait_output_full(void) {
    for (uint32_t i = 0; i < PS2_WAIT_LIMIT; ++i) {
        if ((inb(PS2_STATUS_PORT) & 0x01U) != 0) return true;
        __asm__ volatile ("pause");
    }
    return false;
}

static bool controller_command(uint8_t command) {
    if (!wait_input_empty()) return false;
    outb(PS2_COMMAND_PORT, command);
    return true;
}

static bool controller_write(uint8_t value) {
    if (!wait_input_empty()) return false;
    outb(PS2_DATA_PORT, value);
    return true;
}

static bool controller_read(uint8_t *value) {
    if (value == NULL || !wait_output_full()) return false;
    *value = inb(PS2_DATA_PORT);
    return true;
}

static void flush_output(void) {
    for (uint32_t i = 0; i < PS2_WAIT_LIMIT; ++i) {
        if ((inb(PS2_STATUS_PORT) & 0x01U) == 0) break;
        (void)inb(PS2_DATA_PORT);
    }
}

static bool keyboard_command(uint8_t command) {
    uint8_t response = 0;
    return controller_write(command) && controller_read(&response) &&
           response == 0xFAU;
}

static bool mouse_command(uint8_t command) {
    uint8_t response = 0;
    if (!controller_command(0xD4U) || !controller_write(command) ||
        !controller_read(&response)) return false;
    return response == 0xFAU;
}

static void queue_event(const struct nexus_input_event *event) {
    const uint8_t next = (uint8_t)((event_tail + 1U) & PS2_QUEUE_MASK);
    if (next == event_head) return; /* Drop newest event rather than overwrite. */
    event_queue[event_tail] = *event;
    __asm__ volatile ("" : : : "memory");
    event_tail = next;
}

bool input_init(void) {
    event_head = 0;
    event_tail = 0;
    mouse_packet_index = 0;
    keyboard_ready = false;
    mouse_ready = false;

    /* Disable both PS/2 ports while configuring the controller. */
    if (!controller_command(0xADU) || !controller_command(0xA7U)) {
        serial_write("INPUT: PS/2 controller did not respond\r\n");
        return false;
    }
    flush_output();

    uint8_t config = 0;
    if (!controller_command(0x20U) || !controller_read(&config)) {
        serial_write("INPUT: unable to read PS/2 controller configuration\r\n");
        return false;
    }

    /* Enable IRQ1/IRQ12 and translation; keep both clocks enabled. */
    config = (uint8_t)((config | 0x43U) & (uint8_t)~0x30U);
    if (!controller_command(0x60U) || !controller_write(config)) {
        serial_write("INPUT: unable to configure PS/2 controller\r\n");
        return false;
    }

    if (controller_command(0xAEU)) {
        /* F4 enables keyboard scanning and is acknowledged with 0xFA. */
        keyboard_ready = keyboard_command(0xF4U);
    }

    if (controller_command(0xA8U)) {
        /* Set defaults, then enable mouse packet reporting. */
        (void)mouse_command(0xF6U);
        mouse_ready = mouse_command(0xF4U);
    }

    flush_output();
    if (keyboard_ready) pic_irq_unmask(1U);
    if (mouse_ready) pic_irq_unmask(12U);

    if (keyboard_ready) {
        serial_write("INPUT: PS/2 keyboard enabled\r\n");
        serial_write("NEXUS KEYBOARD INPUT READY\r\n");
    } else {
        serial_write("INPUT: PS/2 keyboard unavailable\r\n");
    }
    if (mouse_ready) {
        serial_write("INPUT: PS/2 mouse enabled\r\n");
        serial_write("NEXUS MOUSE INPUT READY\r\n");
    } else {
        serial_write("INPUT: PS/2 mouse unavailable\r\n");
    }
    return keyboard_ready || mouse_ready;
}

void input_keyboard_interrupt(void) {
    const uint8_t scancode = inb(PS2_DATA_PORT);
    struct nexus_input_event event = {
        .type = NEXUS_INPUT_KEYBOARD,
        .scancode = scancode,
        .mouse_buttons = 0,
        .mouse_dx = 0,
        .mouse_dy = 0
    };
    queue_event(&event);
}

void input_mouse_interrupt(void) {
    const uint8_t value = inb(PS2_DATA_PORT);
    if (mouse_packet_index == 0 && (value & 0x08U) == 0) return;

    mouse_packet[mouse_packet_index++] = value;
    if (mouse_packet_index < 3U) return;
    mouse_packet_index = 0;

    const uint8_t flags = mouse_packet[0];
    if ((flags & 0xC0U) != 0) return; /* Ignore overflow packets. */

    struct nexus_input_event event = {
        .type = NEXUS_INPUT_MOUSE,
        .scancode = 0,
        .mouse_buttons = (uint8_t)(flags & 0x07U),
        .mouse_dx = (int16_t)(int8_t)mouse_packet[1],
        .mouse_dy = (int16_t)-(int16_t)(int8_t)mouse_packet[2]
    };
    queue_event(&event);
}

void input_usb_keyboard_report(const uint8_t report[8]) {
    uint8_t scancodes[8];
    const size_t count = usb_hid_keyboard_decode(report, scancodes,
                                                 sizeof(scancodes));
    uint64_t interrupt_flags;
    __asm__ volatile ("pushfq; popq %0; cli"
                      : "=r"(interrupt_flags) : : "memory", "cc");
    for (size_t i = 0U; i < count; ++i) {
        struct nexus_input_event event = {
            .type = NEXUS_INPUT_KEYBOARD,
            .scancode = scancodes[i],
            .mouse_buttons = 0U,
            .mouse_dx = 0,
            .mouse_dy = 0
        };
        queue_event(&event);
    }
    __asm__ volatile ("pushq %0; popfq"
                      : : "r"(interrupt_flags) : "memory", "cc");
    for (size_t i = 0U; i < count; ++i) {
        serial_write("INPUT: USB HID keyboard scancode=");
        serial_write_hex(scancodes[i]);
        serial_write("\\r\\n");
    }
}

bool input_pop_event(struct nexus_input_event *event) {
    if (event == NULL || event_head == event_tail) return false;
    __asm__ volatile ("" : : : "memory");
    *event = event_queue[event_head];
    event_head = (uint8_t)((event_head + 1U) & PS2_QUEUE_MASK);
    return true;
}


void input_usb_mouse_report(const uint8_t report[3]) {
    struct nexus_usb_mouse_event decoded;
    if (!usb_hid_mouse_decode(report, &decoded)) return;

    uint64_t interrupt_flags;
    __asm__ volatile ("pushfq; popq %0; cli"
                      : "=r"(interrupt_flags) : : "memory", "cc");
    const struct nexus_input_event event = {
        .type = NEXUS_INPUT_MOUSE,
        .scancode = 0U,
        .mouse_buttons = decoded.buttons,
        .mouse_dx = decoded.dx,
        .mouse_dy = decoded.dy
    };
    queue_event(&event);
    __asm__ volatile ("pushq %0; popfq"
                      : : "r"(interrupt_flags) : "memory", "cc");
}
