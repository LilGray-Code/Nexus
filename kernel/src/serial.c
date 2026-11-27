#include "serial.h"
#include "log_buffer.h"

#include <stdint.h>

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void serial_putc(char c) {
    for (unsigned int spin = 0; spin < 100000; ++spin) {
        if (inb(0x3FD) & 0x20) {
            outb(0x3F8, (uint8_t)c);
            return;
        }
    }
}

void serial_init(void) {
    outb(0x3F9, 0x00);
    outb(0x3FB, 0x80);
    outb(0x3F8, 0x01);
    outb(0x3F9, 0x00);
    outb(0x3FB, 0x03);
    outb(0x3FA, 0xC7);
    outb(0x3FC, 0x0B);
    nexus_log_buffer_init();
}

void serial_write(const char *text) {
    if (text == NULL) return;
    size_t length = 0U;
    while (text[length] != 0) ++length;
    serial_write_n(text, length);
}

void serial_write_n(const char *text, size_t length) {
    if (text == NULL || length == 0U) return;
    nexus_log_buffer_append(text, length);
    for (size_t i = 0U; i < length; ++i) serial_putc(text[i]);
}

void serial_write_hex(uint64_t value) {
    static const char digits[] = "0123456789ABCDEF";
    char text[18];
    text[0] = '0';
    text[1] = 'x';
    for (unsigned int i = 0U; i < 16U; ++i) {
        const unsigned int shift = 60U - (i * 4U);
        text[i + 2U] = digits[(value >> shift) & 0x0FU];
    }
    /* Append the formatted value as one log record instead of locking the
     * ring once for the prefix and once per hexadecimal digit.
     */
    serial_write_n(text, sizeof(text));
}

size_t serial_log_read_recent(char *output, size_t capacity) {
    return nexus_log_buffer_read_recent(output, capacity);
}

uint64_t serial_log_dropped(void) {
    return nexus_log_buffer_dropped();
}
