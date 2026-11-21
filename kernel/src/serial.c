#include "serial.h"

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
}

void serial_write(const char *text) {
    if (text == 0) return;
    while (*text) serial_putc(*text++);
}

void serial_write_n(const char *text, size_t length) {
    if (text == 0) return;
    for (size_t i = 0; i < length; ++i) serial_putc(text[i]);
}

void serial_write_hex(uint64_t value) {
    static const char digits[] = "0123456789ABCDEF";
    serial_write("0x");
    for (int shift = 60; shift >= 0; shift -= 4) {
        serial_putc(digits[(value >> shift) & 0x0F]);
    }
}
