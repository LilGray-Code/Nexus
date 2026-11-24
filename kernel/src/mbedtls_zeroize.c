#include <stddef.h>

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
