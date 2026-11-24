#include <stddef.h>
#include <stdint.h>

/* Freestanding C memory primitives required by the Mbed TLS static archive. */
void *memcpy(void *destination, const void *source, size_t length) {
    unsigned char *out = (unsigned char *)destination;
    const unsigned char *in = (const unsigned char *)source;
    for (size_t i = 0U; i < length; ++i) out[i] = in[i];
    return destination;
}

void *memmove(void *destination, const void *source, size_t length) {
    unsigned char *out = (unsigned char *)destination;
    const unsigned char *in = (const unsigned char *)source;
    if ((uintptr_t)out <= (uintptr_t)in ||
        (uintptr_t)out - (uintptr_t)in >= length) {
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
    if (left == NULL) return right == NULL ? 0 : -1;
    if (right == NULL) return 1;
    size_t i = 0U;
    while (left[i] != '\0' && left[i] == right[i]) ++i;
    return (unsigned char)left[i] - (unsigned char)right[i];
}

int strncmp(const char *left, const char *right, size_t length) {
    if (length == 0U) return 0;
    if (left == NULL) return right == NULL ? 0 : -1;
    if (right == NULL) return 1;
    for (size_t i = 0U; i < length; ++i) {
        const unsigned char a = (unsigned char)left[i];
        const unsigned char b = (unsigned char)right[i];
        if (a != b) return (int)a - (int)b;
        if (a == 0U) return 0;
    }
    return 0;
}

char *strchr(const char *text, int character) {
    if (text == NULL) return NULL;
    const char wanted = (char)character;
    for (;;) {
        if (*text == wanted) return (char *)(uintptr_t)text;
        if (*text == '\0') return NULL;
        ++text;
    }
}

char *strncpy(char *destination, const char *source, size_t capacity) {
    if (destination == NULL || capacity == 0U) return destination;
    size_t i = 0U;
    if (source != NULL) {
        while (i < capacity && source[i] != '\0') {
            destination[i] = source[i];
            ++i;
        }
    }
    while (i < capacity) destination[i++] = '\0';
    return destination;
}

char *strstr(const char *haystack, const char *needle) {
    if (haystack == NULL || needle == NULL) return NULL;
    if (*needle == '\0') return (char *)(uintptr_t)haystack;
    for (const char *start = haystack; *start != '\0'; ++start) {
        size_t i = 0U;
        while (needle[i] != '\0' && start[i] != '\0' &&
               start[i] == needle[i]) ++i;
        if (needle[i] == '\0') return (char *)(uintptr_t)start;
    }
    return NULL;
}

/* inet_pton(AF_INET, ...) support for the optional numeric-host utility path. */
int inet_pton(int family, const char *text, void *address) {
    if (family != 2 || text == NULL || address == NULL) return 0;
    unsigned char parsed[4] = {0U, 0U, 0U, 0U};
    size_t part = 0U;
    unsigned value = 0U;
    unsigned digits = 0U;
    for (size_t i = 0U;; ++i) {
        const char ch = text[i];
        if (ch >= '0' && ch <= '9') {
            if (++digits > 3U) return 0;
            value = value * 10U + (unsigned)(ch - '0');
            if (value > 255U) return 0;
        } else if (ch == '.' || ch == '\0') {
            if (digits == 0U || part >= 4U) return 0;
            parsed[part++] = (unsigned char)value;
            value = 0U;
            digits = 0U;
            if (ch == '\0') break;
        } else {
            return 0;
        }
    }
    if (part != 4U) return 0;
    unsigned char *out = (unsigned char *)address;
    for (size_t i = 0U; i < 4U; ++i) out[i] = parsed[i];
    return 1;
}
