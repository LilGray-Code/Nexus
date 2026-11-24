#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <limits.h>
#include <stdbool.h>

void *memcpy(void *restrict destination, const void *restrict source,
             size_t length) {
    unsigned char *out = (unsigned char *)destination;
    const unsigned char *in = (const unsigned char *)source;
    for (size_t i = 0U; i < length; ++i) out[i] = in[i];
    return destination;
}

void *memmove(void *destination, const void *source, size_t length) {
    unsigned char *out = (unsigned char *)destination;
    const unsigned char *in = (const unsigned char *)source;
    if (out == in || length == 0U) return destination;
    if ((uintptr_t)out < (uintptr_t)in) {
        for (size_t i = 0U; i < length; ++i) out[i] = in[i];
    } else {
        for (size_t i = length; i != 0U; --i) out[i - 1U] = in[i - 1U];
    }
    return destination;
}

void *memset(void *destination, int value, size_t length) {
    unsigned char *out = (unsigned char *)destination;
    for (size_t i = 0U; i < length; ++i) out[i] = (unsigned char)value;
    return destination;
}

int memcmp(const void *left, const void *right, size_t length) {
    const unsigned char *a = (const unsigned char *)left;
    const unsigned char *b = (const unsigned char *)right;
    for (size_t i = 0U; i < length; ++i) {
        if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    }
    return 0;
}

size_t strlen(const char *text) {
    size_t length = 0U;
    if (text == NULL) return 0U;
    while (text[length] != '\0') ++length;
    return length;
}

int strcmp(const char *left, const char *right) {
    if (left == NULL || right == NULL) {
        if (left == right) return 0;
        return left == NULL ? -1 : 1;
    }
    while (*left != '\0' && *left == *right) {
        ++left;
        ++right;
    }
    const unsigned char a = (unsigned char)*left;
    const unsigned char b = (unsigned char)*right;
    return a == b ? 0 : (a < b ? -1 : 1);
}

char *strchr(const char *text, int character) {
    if (text == NULL) return NULL;
    const char wanted = (char)character;
    for (;;) {
        if (*text == wanted) return (char *)text;
        if (*text == '\0') return NULL;
        ++text;
    }
}

char *strstr(const char *text, const char *pattern) {
    if (text == NULL || pattern == NULL) return NULL;
    if (*pattern == '\0') return (char *)text;
    const size_t pattern_length = strlen(pattern);
    for (const char *candidate = text; *candidate != '\0'; ++candidate) {
        size_t i = 0U;
        while (i < pattern_length && candidate[i] == pattern[i]) ++i;
        if (i == pattern_length) return (char *)candidate;
    }
    return NULL;
}

static bool parse_ipv4(const char *source, uint8_t output[4]) {
    if (source == NULL || output == NULL) return false;
    size_t cursor = 0U;
    for (size_t part = 0U; part < 4U; ++part) {
        unsigned value = 0U;
        unsigned digits = 0U;
        while (source[cursor] >= '0' && source[cursor] <= '9') {
            if (++digits > 3U) return false;
            value = value * 10U + (unsigned)(source[cursor] - '0');
            if (value > 255U) return false;
            ++cursor;
        }
        if (digits == 0U) return false;
        output[part] = (uint8_t)value;
        if (part != 3U) {
            if (source[cursor] != '.') return false;
            ++cursor;
        } else if (source[cursor] != '\0') {
            return false;
        }
    }
    return true;
}

static int hex_value(char character) {
    if (character >= '0' && character <= '9') return character - '0';
    if (character >= 'a' && character <= 'f') return character - 'a' + 10;
    if (character >= 'A' && character <= 'F') return character - 'A' + 10;
    return -1;
}

/* Minimal POSIX-compatible inet_pton for the address families needed by
 * certificate IP subjectAltName validation. No DNS or socket dependencies.
 */
int inet_pton(int family, const char *source, void *destination) {
    if (source == NULL || destination == NULL || source[0] == '\0') return 0;
    if (family == 2) {
        uint8_t address[4];
        if (!parse_ipv4(source, address)) return 0;
        memcpy(destination, address, sizeof(address));
        return 1;
    }
    if (family != 10) return -1; /* AF_INET6 */
    uint8_t address[16] = {0U};
    size_t used = 0U;
    size_t compressed_at = SIZE_MAX;
    const char *cursor = source;

    if (*cursor == ':') {
        if (cursor[1] != ':') return 0;
        compressed_at = 0U;
        cursor += 2;
        if (*cursor == '\0') {
            memcpy(destination, address, sizeof(address));
            return 1;
        }
    }

    while (*cursor != '\0') {
        if (used >= sizeof(address)) return 0;
        const char *group_start = cursor;
        const char *scan = cursor;
        bool has_dot = false;
        while (*scan != '\0' && *scan != ':') {
            if (*scan == '.') has_dot = true;
            ++scan;
        }
        if (has_dot) {
            if (scan[0] != '\0' || used > 12U) return 0;
            uint8_t ipv4[4];
            if (!parse_ipv4(group_start, ipv4)) return 0;
            memcpy(address + used, ipv4, sizeof(ipv4));
            used += sizeof(ipv4);
            cursor = scan;
            break;
        }

        unsigned value = 0U;
        unsigned digits = 0U;
        while (*cursor != '\0' && *cursor != ':') {
            const int nibble = hex_value(*cursor);
            if (nibble < 0 || ++digits > 4U) return 0;
            value = (value << 4) | (unsigned)nibble;
            ++cursor;
        }
        if (digits == 0U || used > 14U) return 0;
        address[used++] = (uint8_t)(value >> 8);
        address[used++] = (uint8_t)value;

        if (*cursor == ':') {
            ++cursor;
            if (*cursor == ':') {
                if (compressed_at != SIZE_MAX) return 0;
                compressed_at = used;
                ++cursor;
                if (*cursor == '\0') break;
            } else if (*cursor == '\0') {
                return 0;
            }
        }
    }

    if (compressed_at != SIZE_MAX) {
        if (used >= sizeof(address)) return 0;
        const size_t zeros = sizeof(address) - used;
        memmove(address + compressed_at + zeros, address + compressed_at,
                used - compressed_at);
        memset(address + compressed_at, 0, zeros);
        used = sizeof(address);
    }
    if (used != sizeof(address)) return 0;
    memcpy(destination, address, sizeof(address));
    return 1;
}

struct nexus_format_output {
    char *buffer;
    size_t capacity;
    size_t length;
};

static void format_put(struct nexus_format_output *out, char value) {
    if (out->capacity != 0U && out->length + 1U < out->capacity)
        out->buffer[out->length] = value;
    if (out->length < SIZE_MAX) ++out->length;
}

static void format_repeat(struct nexus_format_output *out, char value,
                          size_t count) {
    while (count-- != 0U) format_put(out, value);
}

static uint64_t format_unsigned_arg(va_list *args, unsigned length_kind) {
    switch (length_kind) {
        case 1U: return (unsigned char)va_arg(*args, unsigned int);
        case 2U: return (unsigned short)va_arg(*args, unsigned int);
        case 3U: return va_arg(*args, unsigned long);
        case 4U: return va_arg(*args, unsigned long long);
        case 5U: return (uint64_t)va_arg(*args, size_t);
        case 6U: return (uint64_t)va_arg(*args, uintmax_t);
        case 7U: return (uint64_t)va_arg(*args, uintptr_t);
        default: return va_arg(*args, unsigned int);
    }
}

static int64_t format_signed_arg(va_list *args, unsigned length_kind) {
    switch (length_kind) {
        case 1U: return (signed char)va_arg(*args, int);
        case 2U: return (short)va_arg(*args, int);
        case 3U: return va_arg(*args, long);
        case 4U: return va_arg(*args, long long);
        case 5U: return (int64_t)va_arg(*args, ptrdiff_t);
        case 6U: return (int64_t)va_arg(*args, intmax_t);
        case 7U: return (int64_t)va_arg(*args, ptrdiff_t);
        default: return va_arg(*args, int);
    }
}

int vsnprintf(char *restrict buffer, size_t capacity,
              const char *restrict format, va_list arguments) {
    if (format == NULL || (capacity != 0U && buffer == NULL)) return -1;
    struct nexus_format_output out = {buffer, capacity, 0U};
    va_list args;
    va_copy(args, arguments);

    for (size_t i = 0U; format[i] != '\0';) {
        if (format[i] != '%') {
            format_put(&out, format[i++]);
            continue;
        }
        ++i;
        if (format[i] == '%') {
            format_put(&out, '%');
            ++i;
            continue;
        }

        bool left = false, plus = false, space = false;
        bool alternate = false, zero = false;
        bool flags = true;
        while (flags) {
            switch (format[i]) {
                case '-': left = true; ++i; break;
                case '+': plus = true; ++i; break;
                case ' ': space = true; ++i; break;
                case '#': alternate = true; ++i; break;
                case '0': zero = true; ++i; break;
                default: flags = false; break;
            }
        }

        int width = 0;
        if (format[i] == '*') {
            width = va_arg(args, int);
            ++i;
            if (width < 0) {
                left = true;
                width = width == INT_MIN ? INT_MAX : -width;
            }
        } else {
            while (format[i] >= '0' && format[i] <= '9') {
                const int digit = format[i++] - '0';
                if (width <= (INT_MAX - digit) / 10)
                    width = width * 10 + digit;
            }
        }

        int precision = -1;
        if (format[i] == '.') {
            ++i;
            precision = 0;
            if (format[i] == '*') {
                precision = va_arg(args, int);
                ++i;
                if (precision < 0) precision = -1;
            } else {
                while (format[i] >= '0' && format[i] <= '9') {
                    const int digit = format[i++] - '0';
                    if (precision <= (INT_MAX - digit) / 10)
                        precision = precision * 10 + digit;
                }
            }
        }

        unsigned length_kind = 0U;
        if (format[i] == 'h') {
            ++i;
            length_kind = format[i] == 'h' ? (++i, 1U) : 2U;
        } else if (format[i] == 'l') {
            ++i;
            length_kind = format[i] == 'l' ? (++i, 4U) : 3U;
        } else if (format[i] == 'z') {
            ++i; length_kind = 5U;
        } else if (format[i] == 'j') {
            ++i; length_kind = 6U;
        } else if (format[i] == 't') {
            ++i; length_kind = 7U;
        } else if (format[i] == 'L') {
            ++i;
        }

        const char conversion = format[i] == '\0' ? '\0' : format[i++];
        if (conversion == '\0') break;
        if (conversion == 's') {
            const char *value = va_arg(args, const char *);
            if (value == NULL) value = "(null)";
            size_t length = strlen(value);
            if (precision >= 0 && length > (size_t)precision)
                length = (size_t)precision;
            const size_t padding = width > 0 && (size_t)width > length
                ? (size_t)width - length : 0U;
            if (!left) format_repeat(&out, ' ', padding);
            for (size_t j = 0U; j < length; ++j) format_put(&out, value[j]);
            if (left) format_repeat(&out, ' ', padding);
            continue;
        }
        if (conversion == 'c') {
            const char value = (char)va_arg(args, int);
            const size_t padding = width > 1 ? (size_t)width - 1U : 0U;
            if (!left) format_repeat(&out, ' ', padding);
            format_put(&out, value);
            if (left) format_repeat(&out, ' ', padding);
            continue;
        }
        if (conversion != 'd' && conversion != 'i' &&
            conversion != 'u' && conversion != 'x' &&
            conversion != 'X' && conversion != 'o' &&
            conversion != 'p') {
            format_put(&out, '%');
            format_put(&out, conversion);
            continue;
        }

        bool negative = false;
        uint64_t value;
        unsigned base = conversion == 'o' ? 8U :
                        (conversion == 'x' || conversion == 'X' ||
                         conversion == 'p') ? 16U : 10U;
        if (conversion == 'd' || conversion == 'i') {
            const int64_t signed_value = format_signed_arg(&args, length_kind);
            negative = signed_value < 0;
            value = negative
                ? (uint64_t)(-(signed_value + 1)) + 1U
                : (uint64_t)signed_value;
        } else if (conversion == 'p') {
            value = (uint64_t)(uintptr_t)va_arg(args, void *);
            alternate = true;
        } else {
            value = format_unsigned_arg(&args, length_kind);
        }

        char digits[32];
        size_t digit_count = 0U;
        const char *alphabet = conversion == 'X'
            ? "0123456789ABCDEF" : "0123456789abcdef";
        if (value != 0U || precision != 0) {
            do {
                digits[digit_count++] = alphabet[value % base];
                value /= base;
            } while (value != 0U && digit_count < sizeof(digits));
        }
        size_t precision_zeros = precision > 0 &&
            (size_t)precision > digit_count
                ? (size_t)precision - digit_count : 0U;
        char sign = negative ? '-' : plus ? '+' : space ? ' ' : '\0';
        char prefix[2];
        size_t prefix_length = 0U;
        if (alternate && (conversion == 'x' || conversion == 'X' ||
                          conversion == 'p')) {
            prefix[0] = '0';
            prefix[1] = conversion == 'X' ? 'X' : 'x';
            prefix_length = 2U;
        } else if (alternate && conversion == 'o' &&
                   (digit_count == 0U || digits[digit_count - 1U] != '0')) {
            prefix[0] = '0';
            prefix_length = 1U;
        }
        size_t number_length = digit_count + precision_zeros +
                               prefix_length + (sign != '\0' ? 1U : 0U);
        size_t padding = width > 0 && (size_t)width > number_length
            ? (size_t)width - number_length : 0U;
        if (!left && !zero) format_repeat(&out, ' ', padding);
        if (sign != '\0') format_put(&out, sign);
        for (size_t j = 0U; j < prefix_length; ++j)
            format_put(&out, prefix[j]);
        if (!left && zero && precision < 0)
            format_repeat(&out, '0', padding);
        format_repeat(&out, '0', precision_zeros);
        while (digit_count != 0U) format_put(&out, digits[--digit_count]);
        if (left) format_repeat(&out, ' ', padding);
    }

    va_end(args);
    if (capacity != 0U) {
        const size_t terminator = out.length < capacity
            ? out.length : capacity - 1U;
        buffer[terminator] = '\0';
    }
    return out.length > (size_t)INT_MAX ? INT_MAX : (int)out.length;
}

int snprintf(char *restrict buffer, size_t capacity,
             const char *restrict format, ...) {
    va_list args;
    va_start(args, format);
    const int result = vsnprintf(buffer, capacity, format, args);
    va_end(args);
    return result;
}

void explicit_bzero(void *buffer, size_t length) {
    volatile unsigned char *bytes = (volatile unsigned char *)buffer;
    if (bytes == NULL) return;
    while (length-- != 0U) *bytes++ = 0U;
}
