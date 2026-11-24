#ifndef NEXUS_NET_HTTPS_H
#define NEXUS_NET_HTTPS_H

#include "tls_client.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NEXUS_HTTPS_PORT 443U
#define NEXUS_HTTPS_IO_WAIT_TICKS 25U
#define NEXUS_HTTPS_SEND_CHUNK 1024U
#define NEXUS_HTTPS_RECEIVE_CHUNK 1024U

#define NEXUS_HTTPS_ERR_ARGUMENT (-1101)
#define NEXUS_HTTPS_ERR_DNS (-1102)
#define NEXUS_HTTPS_ERR_CONNECT (-1103)
#define NEXUS_HTTPS_ERR_TIMEOUT (-1104)
#define NEXUS_HTTPS_ERR_TLS (-1105)

struct nexus_https_connection {
    struct nexus_tls_client tls;
    bool tcp_open;
    bool tls_ready;
    bool verified;
};

/*
 * Resolve hostname, connect TCP/443, initialize the pinned TLS 1.3 client,
 * and complete a certificate- and hostname-verified handshake.
 * ca_pem must be a NUL-terminated PEM trust bundle.
 */
int nexus_https_connect(struct nexus_https_connection *connection,
                        const char *hostname,
                        const unsigned char *ca_pem,
                        size_t ca_pem_length,
                        uint64_t timeout_ticks);

bool nexus_https_is_verified(
    const struct nexus_https_connection *connection);

/* Return Mbed TLS byte counts/errors; callers must handle WANT_READ/WRITE. */
int nexus_https_write(struct nexus_https_connection *connection,
                      const unsigned char *buffer, size_t length);
int nexus_https_read(struct nexus_https_connection *connection,
                     unsigned char *buffer, size_t capacity);

void nexus_https_close(struct nexus_https_connection *connection);

#endif
