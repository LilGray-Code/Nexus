#ifndef NEXUS_MBEDTLS_KERNEL_COMPAT_H
#define NEXUS_MBEDTLS_KERNEL_COMPAT_H

/* Mbed TLS diagnostic printf is intentionally discarded in the kernel. */
int nexus_mbedtls_printf(const char *format, ...);

#endif
