#include "nexus/runtime.h"
#include "nexus/syscall.h"

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>


#define NEXUS_RUNTIME_READ_CHUNK 256U
#define NEXUS_RUNTIME_WRITE_CHUNK 128U

size_t nexus_strlen(const char *text) {
    if (text == NULL) return 0U;
    size_t length = 0U;
    while (text[length] != '\0') ++length;
    return length;
}

size_t nexus_strnlen(const char *text, size_t maximum) {
    if (text == NULL) return 0U;
    size_t length = 0U;
    while (length < maximum && text[length] != '\0') ++length;
    return length;
}

int nexus_strcmp(const char *left, const char *right) {
    if (left == right) return 0;
    if (left == NULL) return -1;
    if (right == NULL) return 1;
    size_t i = 0U;
    while (left[i] != '\0' && left[i] == right[i]) ++i;
    return (int)(unsigned char)left[i] - (int)(unsigned char)right[i];
}

int nexus_strncmp(const char *left, const char *right, size_t maximum) {
    if (maximum == 0U || left == right) return 0;
    if (left == NULL) return -1;
    if (right == NULL) return 1;
    for (size_t i = 0U; i < maximum; ++i) {
        const unsigned char a = (unsigned char)left[i];
        const unsigned char b = (unsigned char)right[i];
        if (a != b) return (int)a - (int)b;
        if (a == 0U) return 0;
    }
    return 0;
}

void *nexus_memcpy(void *destination, const void *source, size_t length) {
    if (destination == NULL || source == NULL) return destination;
    uint8_t *out = (uint8_t *)destination;
    const uint8_t *in = (const uint8_t *)source;
    for (size_t i = 0U; i < length; ++i) out[i] = in[i];
    return destination;
}

void *nexus_memset(void *destination, int value, size_t length) {
    if (destination == NULL) return destination;
    uint8_t *out = (uint8_t *)destination;
    for (size_t i = 0U; i < length; ++i) out[i] = (uint8_t)value;
    return destination;
}

int nexus_memcmp(const void *left, const void *right, size_t length) {
    if (left == right || length == 0U) return 0;
    if (left == NULL) return -1;
    if (right == NULL) return 1;
    const uint8_t *a = (const uint8_t *)left;
    const uint8_t *b = (const uint8_t *)right;
    for (size_t i = 0U; i < length; ++i)
        if (a[i] != b[i]) return (int)a[i] - (int)b[i];
    return 0;
}

int64_t nexus_write_all(int descriptor, const void *buffer, size_t length) {
    if (length != 0U && buffer == NULL) return -NEXUS_EINVAL;
    const uint8_t *bytes = (const uint8_t *)buffer;
    size_t written = 0U;
    while (written < length) {
        size_t amount = length - written;
        if (amount > NEXUS_RUNTIME_WRITE_CHUNK)
            amount = NEXUS_RUNTIME_WRITE_CHUNK;
        const int64_t result =
            nexus_write(descriptor, bytes + written, amount);
        if (result < 0) return result;
        if (result == 0 || (uint64_t)result > amount) return -NEXUS_EIO;
        written += (size_t)result;
    }
    return (int64_t)written;
}

int64_t nexus_puts(const char *text) {
    if (text == NULL) return -NEXUS_EINVAL;
    return nexus_write_all(1, text, nexus_strlen(text));
}

int64_t nexus_read_file(const char *path, void *buffer, size_t capacity,
                        size_t *bytes_read) {
    if (path == NULL || bytes_read == NULL ||
        (capacity != 0U && buffer == NULL)) return -NEXUS_EINVAL;
    *bytes_read = 0U;

    const int64_t descriptor = nexus_open(path);
    if (descriptor < 0) return descriptor;

    int64_t error = 0;
    size_t total = 0U;
    for (;;) {
        if (total == capacity) {
            uint8_t overflow_byte = 0U;
            const int64_t extra =
                nexus_read((int)descriptor, &overflow_byte, 1U);
            if (extra < 0) error = extra;
            else if (extra > 0) error = -NEXUS_EFBIG;
            break;
        }
        size_t amount = capacity - total;
        if (amount > NEXUS_RUNTIME_READ_CHUNK)
            amount = NEXUS_RUNTIME_READ_CHUNK;
        const int64_t result = nexus_read(
            (int)descriptor, (uint8_t *)buffer + total, amount);
        if (result < 0) {
            error = result;
            break;
        }
        if (result == 0) break;
        if ((uint64_t)result > amount) {
            error = -NEXUS_EIO;
            break;
        }
        total += (size_t)result;
    }

    const int64_t close_result = nexus_close((int)descriptor);
    if (error < 0) return error;
    if (close_result < 0) return close_result;
    *bytes_read = total;
    return (int64_t)total;
}

 
#define NEXUS_RUNTIME_FORMAT_CAPACITY 512U

static bool format_append(char output[NEXUS_RUNTIME_FORMAT_CAPACITY],
                          size_t *length, char value) {
    if (*length >= NEXUS_RUNTIME_FORMAT_CAPACITY - 1U) return false;
    output[(*length)++] = value;
    return true;
}

static bool format_unsigned(char output[NEXUS_RUNTIME_FORMAT_CAPACITY],
                            size_t *length, uint64_t value, unsigned base,
                            bool uppercase, unsigned width, char padding) {
    char digits[32];
    size_t count = 0U;
    do {
        const unsigned digit = (unsigned)(value % base);
        digits[count++] = (char)(digit < 10U ? '0' + digit :
            (uppercase ? 'A' : 'a') + (digit - 10U));
        value /= base;
    } while (value != 0U && count < sizeof(digits));

    while (count < width) {
        if (!format_append(output, length, padding)) return false;
        --width;
    }
    while (count != 0U) {
        if (!format_append(output, length, digits[--count])) return false;
    }
    return true;
}

int64_t nexus_printf(const char *format, ...) {
    if (format == NULL) return -NEXUS_EINVAL;
    char output[NEXUS_RUNTIME_FORMAT_CAPACITY];
    size_t length = 0U;
    va_list arguments;
    va_start(arguments, format);

    for (size_t i = 0U; format[i] != '\0'; ++i) {
        if (format[i] != '%') {
            if (!format_append(output, &length, format[i])) {
                va_end(arguments);
                return -NEXUS_EFBIG;
            }
            continue;
        }

        ++i;
        if (format[i] == '\0') {
            va_end(arguments);
            return -NEXUS_EINVAL;
        }
        char padding = ' ';
        if (format[i] == '0') {
            padding = '0';
            ++i;
        }
        unsigned width = 0U;
        while (format[i] >= '0' && format[i] <= '9') {
            if (width > 32U) {
                va_end(arguments);
                return -NEXUS_EINVAL;
            }
            width = width * 10U + (unsigned)(format[i++] - '0');
            if (width > 32U) {
                va_end(arguments);
                return -NEXUS_EINVAL;
            }
        }

        bool ok = true;
        switch (format[i]) {
            case '%':
                ok = format_append(output, &length, '%');
                break;
            case 'c':
                ok = format_append(output, &length,
                                   (char)va_arg(arguments, int));
                break;
            case 's': {
                const char *value = va_arg(arguments, const char *);
                if (value == NULL) value = "(null)";
                const size_t value_length = nexus_strlen(value);
                if (value_length > NEXUS_RUNTIME_FORMAT_CAPACITY - length - 1U) {
                    ok = false;
                    break;
                }
                for (size_t n = 0U; n < value_length && ok; ++n)
                    ok = format_append(output, &length, value[n]);
                break;
            }
            case 'd':
            case 'i': {
                const int64_t signed_value = (int64_t)va_arg(arguments, int);
                const bool negative = signed_value < 0;
                const uint64_t magnitude = negative
                    ? (uint64_t)(-(signed_value + 1)) + 1U
                    : (uint64_t)signed_value;
                if (negative) {
                    ok = format_append(output, &length, '-');
                    if (width > 0U) --width;
                }
                if (ok) ok = format_unsigned(output, &length, magnitude,
                                              10U, false, width, padding);
                break;
            }
            case 'u':
            case 'x':
            case 'X':
            case 'p': {
                const uint64_t value = format[i] == 'p'
                    ? (uint64_t)(uintptr_t)va_arg(arguments, void *)
                    : (uint64_t)va_arg(arguments, unsigned int);
                const unsigned base = (format[i] == 'u') ? 10U : 16U;
                if (format[i] == 'p') {
                    ok = format_append(output, &length, '0') &&
                         format_append(output, &length, 'x');
                    if (width > 0U) width = width > 2U ? width - 2U : 0U;
                }
                if (ok) ok = format_unsigned(output, &length, value, base,
                    format[i] == 'X', width, padding);
                break;
            }
            default:
                va_end(arguments);
                return -NEXUS_EINVAL;
        }
        if (!ok) {
            va_end(arguments);
            return -NEXUS_EFBIG;
        }
    }

    va_end(arguments);
    return nexus_write_all(1, output, length);
}

int64_t nexus_write_file(const char *path, const void *buffer, size_t length) {
    if (path == NULL || (length != 0U && buffer == NULL))
        return -NEXUS_EINVAL;
    return nexus_file_write(path, buffer, length);
}
