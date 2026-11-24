#include <stddef.h>
#include <stdio.h>

#include "mbedtls/platform_util.h"

/*
 * Freestanding constant-time cleanup for Mbed TLS. The volatile byte stores
 * prevent the compiler from eliding the wipe when the buffer is no longer
 * otherwise observable.
 */
void mbedtls_platform_zeroize(void *buffer, size_t length) {
    volatile unsigned char *bytes = (volatile unsigned char *)buffer;
    if (buffer == NULL) return;
    while (length-- != 0U) *bytes++ = 0U;
    __asm__ volatile ("" : : "r"(buffer) : "memory");
}

/* Mbed TLS self-test diagnostic output is not routed to kernel serial logs. */
int printf(const char *format, ...) {
    (void)format;
    return 0;
}

#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>

/* Append one byte while retaining snprintf's would-have-written count. */
static void nexus_format_put(char *buffer, size_t capacity,
                             size_t *written, char value) {
    if (*written < capacity && buffer != NULL) buffer[*written] = value;
    if (*written != SIZE_MAX) ++(*written);
}

static void nexus_format_repeat(char *buffer, size_t capacity,
                                size_t *written, char value, size_t count) {
    while (count-- != 0U) nexus_format_put(buffer, capacity, written, value);
}

/*
 * Small freestanding snprintf subset used only for Mbed TLS diagnostics.
 * It supports the integer/string conversions used by Mbed TLS, bounds every
 * write, and returns the untruncated output length like the C library.
 */
int snprintf(char *buffer, size_t capacity, const char *format, ...) {
    if (format == NULL || (capacity != 0U && buffer == NULL)) return -1;
    size_t written = 0U;
    va_list args;
    va_start(args, format);

    for (size_t index = 0U; format[index] != '\0'; ++index) {
        if (format[index] != '%') {
            nexus_format_put(buffer, capacity, &written, format[index]);
            continue;
        }

        ++index;
        if (format[index] == '\0') break;
        if (format[index] == '%') {
            nexus_format_put(buffer, capacity, &written, '%');
            continue;
        }

        bool left = false;
        bool zero = false;
        bool plus = false;
        bool alternate = false;
        bool flags = true;
        while (flags) {
            switch (format[index]) {
                case '-': left = true; ++index; break;
                case '0': zero = true; ++index; break;
                case '+': plus = true; ++index; break;
                case '#': alternate = true; ++index; break;
                default: flags = false; break;
            }
        }

        size_t width = 0U;
        while (format[index] >= '0' && format[index] <= '9') {
            const size_t digit = (size_t)(format[index] - '0');
            if (width <= (SIZE_MAX - digit) / 10U)
                width = width * 10U + digit;
            ++index;
        }
        if (format[index] == '.') {
            ++index;
            while (format[index] >= '0' && format[index] <= '9') ++index;
        }

        enum { NEXUS_LEN_DEFAULT, NEXUS_LEN_LONG, NEXUS_LEN_LLONG,
               NEXUS_LEN_SIZE } length = NEXUS_LEN_DEFAULT;
        if (format[index] == 'z') {
            length = NEXUS_LEN_SIZE;
            ++index;
        } else if (format[index] == 'l') {
            ++index;
            length = NEXUS_LEN_LONG;
            if (format[index] == 'l') {
                length = NEXUS_LEN_LLONG;
                ++index;
            }
        } else if (format[index] == 'h') {
            ++index;
            if (format[index] == 'h') ++index;
        }

        const char conversion = format[index];
        if (conversion == 's') {
            const char *value = va_arg(args, const char *);
            if (value == NULL) value = "(null)";
            size_t value_length = 0U;
            while (value[value_length] != '\0') ++value_length;
            const size_t padding = width > value_length
                ? width - value_length : 0U;
            if (!left) nexus_format_repeat(buffer, capacity, &written, ' ', padding);
            for (size_t i = 0U; i < value_length; ++i)
                nexus_format_put(buffer, capacity, &written, value[i]);
            if (left) nexus_format_repeat(buffer, capacity, &written, ' ', padding);
            continue;
        }
        if (conversion == 'c') {
            const char value = (char)va_arg(args, int);
            const size_t padding = width > 1U ? width - 1U : 0U;
            if (!left) nexus_format_repeat(buffer, capacity, &written, ' ', padding);
            nexus_format_put(buffer, capacity, &written, value);
            if (left) nexus_format_repeat(buffer, capacity, &written, ' ', padding);
            continue;
        }

        unsigned int base = 10U;
        bool uppercase = false;
        bool signed_value = false;
        bool pointer_value = false;
        if (conversion == 'd' || conversion == 'i') {
            signed_value = true;
        } else if (conversion == 'x' || conversion == 'X' ||
                   conversion == 'p') {
            base = 16U;
            uppercase = conversion == 'X';
            pointer_value = conversion == 'p';
        } else if (conversion == 'u') {
            base = 10U;
        } else {
            nexus_format_put(buffer, capacity, &written, '?');
            continue;
        }

        uint64_t number = 0U;
        bool negative = false;
        if (pointer_value) {
            number = (uint64_t)(uintptr_t)va_arg(args, void *);
        } else if (signed_value) {
            int64_t signed_number;
            if (length == NEXUS_LEN_LLONG)
                signed_number = (int64_t)va_arg(args, long long);
            else if (length == NEXUS_LEN_LONG)
                signed_number = (int64_t)va_arg(args, long);
            else if (length == NEXUS_LEN_SIZE)
                signed_number = (int64_t)va_arg(args, ptrdiff_t);
            else
                signed_number = (int64_t)va_arg(args, int);
            negative = signed_number < 0;
            number = negative
                ? (uint64_t)(-(signed_number + 1)) + 1U
                : (uint64_t)signed_number;
        } else {
            if (length == NEXUS_LEN_LLONG)
                number = (uint64_t)va_arg(args, unsigned long long);
            else if (length == NEXUS_LEN_LONG)
                number = (uint64_t)va_arg(args, unsigned long);
            else if (length == NEXUS_LEN_SIZE)
                number = (uint64_t)va_arg(args, size_t);
            else
                number = (uint64_t)va_arg(args, unsigned int);
        }

        char digits[32];
        size_t digit_count = 0U;
        do {
            const unsigned int digit = (unsigned int)(number % base);
            digits[digit_count++] = (char)(digit < 10U
                ? '0' + digit
                : (uppercase ? 'A' : 'a') + (digit - 10U));
            number /= base;
        } while (number != 0U && digit_count < sizeof(digits));

        const size_t prefix_length = (negative || plus ? 1U : 0U) +
                                     (pointer_value || alternate ? 2U : 0U);
        const size_t total = prefix_length + digit_count;
        const size_t padding = width > total ? width - total : 0U;
        const char pad = zero && !left ? '0' : ' ';
        if (!left && pad == ' ') nexus_format_repeat(buffer, capacity, &written, ' ', padding);
        if (negative) nexus_format_put(buffer, capacity, &written, '-');
        else if (plus && signed_value) nexus_format_put(buffer, capacity, &written, '+');
        if (pointer_value || (alternate && base == 16U)) {
            nexus_format_put(buffer, capacity, &written, '0');
            nexus_format_put(buffer, capacity, &written, uppercase ? 'X' : 'x');
        }
        if (!left && pad == '0') nexus_format_repeat(buffer, capacity, &written, '0', padding);
        while (digit_count != 0U)
            nexus_format_put(buffer, capacity, &written, digits[--digit_count]);
        if (left) nexus_format_repeat(buffer, capacity, &written, ' ', padding);
    }

    va_end(args);
    if (capacity != 0U) {
        const size_t terminator = written < capacity ? written : capacity - 1U;
        buffer[terminator] = '\0';
    }
    return written > (size_t)INT_MAX ? INT_MAX : (int)written;
}
