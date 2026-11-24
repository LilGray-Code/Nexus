#include "net_https.h"

int nexus_https_connect_port(struct nexus_https_connection *connection,
                             const char *hostname, uint16_t remote_port,
                             const unsigned char *ca_pem,
                             size_t ca_pem_length,
                             uint64_t timeout_ticks) {
    (void)hostname;
    (void)remote_port;
    (void)ca_pem;
    (void)ca_pem_length;
    (void)timeout_ticks;
    if (connection != NULL)
        *connection = (struct nexus_https_connection){0};
    return NEXUS_HTTPS_ERR_UNAVAILABLE;
}

bool nexus_https_is_verified(
    const struct nexus_https_connection *connection) {
    (void)connection;
    return false;
}

int nexus_https_write(struct nexus_https_connection *connection,
                      const unsigned char *buffer, size_t length) {
    (void)connection;
    (void)buffer;
    (void)length;
    return NEXUS_HTTPS_ERR_UNAVAILABLE;
}

int nexus_https_read(struct nexus_https_connection *connection,
                     unsigned char *buffer, size_t capacity) {
    (void)connection;
    (void)buffer;
    (void)capacity;
    return NEXUS_HTTPS_ERR_UNAVAILABLE;
}

void nexus_https_close(struct nexus_https_connection *connection) {
    if (connection != NULL)
        *connection = (struct nexus_https_connection){0};
}

int nexus_https_connect(struct nexus_https_connection *connection,
                        const char *hostname,
                        const unsigned char *ca_pem,
                        size_t ca_pem_length,
                        uint64_t timeout_ticks) {
    return nexus_https_connect_port(connection, hostname, NEXUS_HTTPS_PORT,
                                    ca_pem, ca_pem_length, timeout_ticks);
}
