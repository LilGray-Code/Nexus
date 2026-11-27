#include "input.h"

#include "serial.h"
#include "timer.h"
#include "usb_hid.h"

#include <stddef.h>
#include <stdint.h>

#define PS2_DATA_PORT 0x60U
#define PS2_STATUS_PORT 0x64U
#define PS2_COMMAND_PORT 0x64U
#define INPUT_EVENT_QUEUE_SIZE NEXUS_INPUT_EVENT_QUEUE_SIZE
#define INPUT_EVENT_QUEUE_MASK (INPUT_EVENT_QUEUE_SIZE - 1U)
_Static_assert((INPUT_EVENT_QUEUE_SIZE & INPUT_EVENT_QUEUE_MASK) == 0U,
               "input event queue size must be a power of two");
#define GAMEPAD_QUEUE_SIZE 16U
#define GAMEPAD_QUEUE_MASK (GAMEPAD_QUEUE_SIZE - 1U)
#define PS2_WAIT_LIMIT 100000U

#define PS2_MOUSE_PACKET_STANDARD 3U
#define PS2_MOUSE_PACKET_WHEEL    4U

static struct nexus_input_event event_queue[INPUT_EVENT_QUEUE_SIZE];
static struct nexus_usb_gamepad_event gamepad_queue[GAMEPAD_QUEUE_SIZE];
static volatile uint8_t gamepad_head;
static volatile uint8_t gamepad_tail;
static volatile uint8_t event_head;
static volatile uint8_t event_tail;
static volatile uint32_t event_drop_count;
static uint8_t mouse_packet[4];
static uint8_t mouse_packet_index;
static uint8_t mouse_packet_length = PS2_MOUSE_PACKET_STANDARD;
static bool mouse_explorer_buttons;
static bool keyboard_e0_prefix;
static uint8_t keyboard_e1_remaining;
static bool keyboard_ready;
static bool mouse_ready;
static struct nexus_usb_keyboard_state default_input_keyboard_state;

static void queue_event(const struct nexus_input_event *event);

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

static bool mouse_set_sample_rate(uint8_t rate) {
    return mouse_command(0xF3U) && mouse_command(rate);
}

static bool mouse_read_id(uint8_t *id) {
    uint8_t acknowledgement = 0U;
    if (id == NULL || !controller_command(0xD4U) ||
        !controller_write(0xF2U) ||
        !controller_read(&acknowledgement) || acknowledgement != 0xFAU ||
        !controller_read(id)) return false;
    return true;
}

static void mouse_enable_wheel_if_supported(void) {
    uint8_t id = 0U;
    /* IntelliMouse Explorer sequence enables wheel + side buttons (ID 4). */
    if (mouse_set_sample_rate(200U) &&
        mouse_set_sample_rate(200U) &&
        mouse_set_sample_rate(80U) &&
        mouse_read_id(&id) && id == 4U) {
        mouse_packet_length = PS2_MOUSE_PACKET_WHEEL;
        mouse_explorer_buttons = true;
        serial_write("INPUT: PS/2 IntelliMouse wheel and side buttons enabled\r\n");
        return;
    }

    /* Standard IntelliMouse wheel sequence (ID 3) is the common fallback. */
    if (mouse_set_sample_rate(200U) &&
        mouse_set_sample_rate(100U) &&
        mouse_set_sample_rate(80U) &&
        mouse_read_id(&id) && id == 3U) {
        mouse_packet_length = PS2_MOUSE_PACKET_WHEEL;
        mouse_explorer_buttons = false;
        serial_write("INPUT: PS/2 IntelliMouse wheel enabled\r\n");
    } else {
        mouse_packet_length = PS2_MOUSE_PACKET_STANDARD;
        mouse_explorer_buttons = false;
        serial_write("INPUT: PS/2 standard three-byte mouse mode\r\n");
    }
}

static void input_process_keyboard_scancode(uint8_t scancode) {
    if (keyboard_e1_remaining != 0U) {
        --keyboard_e1_remaining;
        return; /* Pause/Break is a multi-byte sequence; no desktop binding yet. */
    }
    if (scancode == 0xE1U) {
        keyboard_e0_prefix = false;
        keyboard_e1_remaining = 5U;
        return;
    }
    if (scancode == 0xE0U) {
        keyboard_e0_prefix = true;
        return;
    }

    const bool extended = keyboard_e0_prefix;
    keyboard_e0_prefix = false;

    /* Print Screen uses fake Shift bytes. Never let them stick Shift state. */
    if (extended && (scancode == 0x2AU || scancode == 0xAAU)) return;

    const struct nexus_input_event event = {
        .type = NEXUS_INPUT_KEYBOARD,
        .scancode = (uint16_t)scancode | (extended ? 0x0100U : 0U),
        .mouse_buttons = 0U,
        .mouse_dx = 0,
        .mouse_dy = 0,
        .mouse_wheel = 0
    };
    queue_event(&event);
}

#ifdef NEXUS_INPUT_HOST_TEST
void input_test_keyboard_scancode(uint8_t scancode) {
    input_process_keyboard_scancode(scancode);
}
#endif

static void queue_event(const struct nexus_input_event *event) {
    const uint8_t next = (uint8_t)((event_tail + 1U) & INPUT_EVENT_QUEUE_MASK);
    if (next == event_head) {
        /* Preserve queued key/button transitions and make overload observable. */
        if (event_drop_count != UINT32_MAX) ++event_drop_count;
        return;
    }
    event_queue[event_tail] = *event;
    __asm__ volatile ("" : : : "memory");
    event_tail = next;
}

static uint64_t input_interrupt_save(void) {
#ifdef NEXUS_INPUT_HOST_TEST
    return 0U;
#else
    uint64_t flags;
    __asm__ volatile ("pushfq; popq %0; cli"
                      : "=r"(flags) : : "memory", "cc");
    return flags;
#endif
}

static void input_interrupt_restore(uint64_t flags) {
#ifdef NEXUS_INPUT_HOST_TEST
    (void)flags;
#else
    __asm__ volatile ("pushq %0; popfq"
                      : : "r"(flags) : "memory", "cc");
#endif
}

bool input_init(void) {
    event_head = 0;
    event_tail = 0;
    event_drop_count = 0U;
    gamepad_head = 0U;
    gamepad_tail = 0U;
    mouse_packet_index = 0U;
    mouse_packet_length = PS2_MOUSE_PACKET_STANDARD;
    mouse_explorer_buttons = false;
    keyboard_e0_prefix = false;
    keyboard_e1_remaining = 0U;
    usb_hid_keyboard_state_reset(&default_input_keyboard_state);
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
        /* Set defaults, negotiate IntelliMouse extensions, then report. */
        (void)mouse_command(0xF6U);
        mouse_enable_wheel_if_supported();
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

bool input_ps2_keyboard_ready(void) {
    return keyboard_ready;
}

bool input_ps2_mouse_ready(void) {
    return mouse_ready;
}

void input_keyboard_interrupt(void) {
    input_process_keyboard_scancode(inb(PS2_DATA_PORT));
}

static void input_process_mouse_byte(uint8_t value) {
    if (mouse_packet_index == 0U && (value & 0x08U) == 0U) return;

    mouse_packet[mouse_packet_index++] = value;
    if (mouse_packet_index < mouse_packet_length) return;
    mouse_packet_index = 0U;

    const uint8_t flags = mouse_packet[0];
    if ((flags & 0xC0U) != 0U) return; /* Ignore X/Y overflow packets. */

    uint8_t buttons = (uint8_t)(flags & 0x07U);
    int8_t wheel = 0;
    if (mouse_packet_length == PS2_MOUSE_PACKET_WHEEL) {
        const uint8_t nibble = (uint8_t)(mouse_packet[3] & 0x0FU);
        wheel = (int8_t)((nibble & 0x08U) != 0U
            ? (int16_t)nibble - 16 : nibble);
        if (mouse_explorer_buttons) {
            buttons |= (uint8_t)((mouse_packet[3] & 0x30U) >> 1);
        }
    }

    const struct nexus_input_event event = {
        .type = NEXUS_INPUT_MOUSE,
        .scancode = 0U,
        .mouse_buttons = buttons,
        .mouse_dx = (int16_t)(int8_t)mouse_packet[1],
        .mouse_dy = (int16_t)-(int16_t)(int8_t)mouse_packet[2],
        .mouse_wheel = wheel
    };
    queue_event(&event);
}

void input_mouse_interrupt(void) {
    input_process_mouse_byte(inb(PS2_DATA_PORT));
}

#ifdef NEXUS_INPUT_HOST_TEST
void input_test_mouse_device_id(uint8_t id) {
    mouse_packet_index = 0U;
    mouse_explorer_buttons = id == 4U;
    mouse_packet_length = (id == 3U || id == 4U)
        ? PS2_MOUSE_PACKET_WHEEL : PS2_MOUSE_PACKET_STANDARD;
}

void input_test_mouse_byte(uint8_t value) {
    input_process_mouse_byte(value);
}
#endif

void input_usb_keyboard_report_state(struct nexus_usb_keyboard_state *state,
                                     const uint8_t report[8]) {
    if (state == NULL || report == NULL) return;
    /* Eight modifier transitions plus six releases and six key makes. */
    uint16_t scancodes[24];
    const size_t count = usb_hid_keyboard_decode_state(
        state, report, scancodes, sizeof(scancodes) / sizeof(scancodes[0]));
    const uint64_t interrupt_flags = input_interrupt_save();
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
    input_interrupt_restore(interrupt_flags);
    for (size_t i = 0U; i < count; ++i) {
        serial_write("INPUT: USB HID keyboard scancode=");
        serial_write_hex(scancodes[i]);
        serial_write("\r\n");
    }
}

void input_usb_keyboard_report(const uint8_t report[8]) {
    input_usb_keyboard_report_state(&default_input_keyboard_state, report);
}

uint32_t input_dropped_event_count(void) {
    return event_drop_count;
}

bool input_pop_event(struct nexus_input_event *event) {
    if (event == NULL || event_head == event_tail) return false;
    __asm__ volatile ("" : : : "memory");
    *event = event_queue[event_head];
    event_head = (uint8_t)((event_head + 1U) & INPUT_EVENT_QUEUE_MASK);
    return true;
}


void input_usb_mouse_report_sized(const uint8_t *report,
                                  size_t report_length) {
    struct nexus_usb_mouse_event decoded;
    if (!usb_hid_mouse_decode_report(report, report_length, &decoded)) return;
    const uint64_t interrupt_flags = input_interrupt_save();
    const struct nexus_input_event event = {
        .type = NEXUS_INPUT_MOUSE,
        .scancode = 0U,
        .mouse_buttons = decoded.buttons,
        .mouse_dx = decoded.dx,
        .mouse_dy = decoded.dy,
        .mouse_wheel = decoded.wheel
    };
    queue_event(&event);
    input_interrupt_restore(interrupt_flags);
    serial_write("INPUT: USB HID mouse report buttons=");
    serial_write_hex(decoded.buttons);
    serial_write(" dx=");
    serial_write_hex((uint16_t)decoded.dx);
    serial_write(" dy=");
    serial_write_hex((uint16_t)decoded.dy);
    serial_write(" wheel=");
    serial_write_hex((uint8_t)decoded.wheel);
    serial_write("\\r\\n");
}

void input_usb_mouse_report(const uint8_t report[3]) {
    input_usb_mouse_report_sized(report, 3U);
}

void input_usb_gamepad_event(const struct nexus_usb_gamepad_event *event) {
    if (event == NULL) return;
    const uint64_t flags = input_interrupt_save();
    const uint8_t next = (uint8_t)((gamepad_tail + 1U) & GAMEPAD_QUEUE_MASK);
    if (next != gamepad_head) {
        gamepad_queue[gamepad_tail] = *event;
        __asm__ volatile ("" : : : "memory");
        gamepad_tail = next;
    }
    input_interrupt_restore(flags);
}

bool input_pop_gamepad_event(struct nexus_usb_gamepad_event *event) {
    if (event == NULL || gamepad_head == gamepad_tail) return false;
    __asm__ volatile ("" : : : "memory");
    *event = gamepad_queue[gamepad_head];
    gamepad_head = (uint8_t)((gamepad_head + 1U) & GAMEPAD_QUEUE_MASK);
    return true;
}
