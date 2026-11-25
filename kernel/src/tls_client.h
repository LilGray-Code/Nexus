#ifndef NEXUS_TLS_CLIENT_H
#define NEXUS_TLS_CLIENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mbedtls/ssl.h"

#define NEXUS_TLS_ERR_ARGUMENT (-1001)
#define NEXUS_TLS_ERR_HEAP_UNAVAILABLE (-1002)
#define NEXUS_TLS_ERR_CLOCK_UNAVAILABLE (-1003)
#define NEXUS_TLS_ERR_PSA_UNAVAILABLE (-1004)
#define NEXUS_TLS_ERR_TLS13_UNAVAILABLE (-1005)
#define NEXUS_TLS_ERR_NOT_CONNECTED (-1006)
#define NEXUS_TLS_ERR_TRANSPORT (-1007)
#define NEXUS_TLS_ERR_TRUST_STORE_MISMATCH (-1008)
#define NEXUS_TLS_ERR_TRUST_STORE_BUSY (-1009)

typedef int nexus_tls_send_fn(void *context,
                              const unsigned char *buffer, size_t length);
typedef int nexus_tls_receive_fn(void *context,
                                 unsigned char *buffer, size_t capacity);

struct nexus_tls_io {
    void *context;
    nexus_tls_send_fn *send;
    nexus_tls_receive_fn *receive;
};

/*
 * Caller-owned TLS 1.3 client state. The transport is borrowed during
 * initialization. The root trust chain is shared, immutable, and retained
 * for the kernel lifetime after the first successful TLS initialization.
 */
struct nexus_tls_client {
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config config;
    struct nexus_tls_io io;
    bool ssl_initialized;
    bool config_initialized;
    bool initialized;
    bool handshake_complete;
};

/*
 * Initialize a certificate-authenticated TLS 1.3 client.
 *
 * ca_pem must contain one or more PEM certificates and include the final NUL
 * byte in ca_pem_length. A valid RTC clock, the Nexus bounded TLS heap, and
 * the PSA random provider are mandatory. No insecure verification fallback
 * is provided. The first valid bundle is parsed once and retained for the
 * lifetime of the kernel. Different bundles are rejected until reboot.
 */
int nexus_tls_client_init(struct nexus_tls_client *client,
                          const char *hostname,
                          const unsigned char *ca_pem,
                          size_t ca_pem_length,
                          const struct nexus_tls_io *io);

/* Returns zero only after a complete TLS handshake with verified identity. */
int nexus_tls_client_handshake(struct nexus_tls_client *client);
bool nexus_tls_client_is_verified(const struct nexus_tls_client *client);

/* Mbed TLS return values are preserved, including WANT_READ/WANT_WRITE. */
int nexus_tls_client_write(struct nexus_tls_client *client,
                           const unsigned char *buffer, size_t length);
int nexus_tls_client_read(struct nexus_tls_client *client,
                          unsigned char *buffer, size_t capacity);

/* Frees TLS allocations and wipes the caller-owned context. */
void nexus_tls_client_free(struct nexus_tls_client *client);

#endif
