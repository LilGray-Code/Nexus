#ifndef NEXUS_NET_HTTPS_H
#define NEXUS_NET_HTTPS_H

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
#define NEXUS_HTTPS_ERR_UNAVAILABLE (-1106)
#define NEXUS_HTTPS_WANT_READ (-1110)
#define NEXUS_HTTPS_WANT_WRITE (-1111)

/* Opaque to the desktop so the no-TLS kernel build needs no Mbed TLS headers.
 * The current TCP stack supports one active connection, so this handle is
 * deliberately non-copyable while connected.
 */
struct nexus_https_connection {
    void *private_state;
    bool tcp_open;
    bool tls_ready;
    bool verified;
};

int nexus_https_connect(struct nexus_https_connection *connection,
                        const char *hostname,
                        const unsigned char *ca_pem,
                        size_t ca_pem_length,
                        uint64_t timeout_ticks);

bool nexus_https_is_verified(
    const struct nexus_https_connection *connection);
int nexus_https_write(struct nexus_https_connection *connection,
                      const unsigned char *buffer, size_t length);
int nexus_https_read(struct nexus_https_connection *connection,
                     unsigned char *buffer, size_t capacity);
void nexus_https_close(struct nexus_https_connection *connection);

#endif
