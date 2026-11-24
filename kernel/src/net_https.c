#include "net_https.h"

#include "net.h"
#include "timer.h"
#include "tls_client.h"

static struct nexus_tls_client https_tls_client;

#include <stddef.h>
#include <stdint.h>

static int https_tcp_send(void *context, const unsigned char *buffer,
                          size_t length) {
    struct nexus_https_connection *connection =
        (struct nexus_https_connection *)context;
    if (connection == NULL || !connection->tcp_open ||
        (length != 0U && buffer == NULL)) {
        return NEXUS_TLS_ERR_TRANSPORT;
    }
    if (length == 0U) return 0;

    size_t amount = length;
    if (amount > NEXUS_HTTPS_SEND_CHUNK)
        amount = NEXUS_HTTPS_SEND_CHUNK;
    if (!net_tcp_send(buffer, (uint16_t)amount))
        return NEXUS_TLS_ERR_TRANSPORT;
    return (int)amount;
}

static int https_tcp_receive(void *context, unsigned char *buffer,
                             size_t capacity) {
    struct nexus_https_connection *connection =
        (struct nexus_https_connection *)context;
    if (connection == NULL || !connection->tcp_open || buffer == NULL ||
        capacity == 0U) {
        return NEXUS_TLS_ERR_TRANSPORT;
    }

    size_t amount = capacity;
    if (amount > NEXUS_HTTPS_RECEIVE_CHUNK)
        amount = NEXUS_HTTPS_RECEIVE_CHUNK;
    size_t received = 0U;
    const enum nexus_tcp_receive_result result = net_tcp_receive_wait(
        buffer, amount, NEXUS_HTTPS_IO_WAIT_TICKS, &received);
    switch (result) {
        case NEXUS_TCP_RECEIVE_DATA:
            if (received == 0U || received > amount)
                return NEXUS_TLS_ERR_TRANSPORT;
            return (int)received;
        case NEXUS_TCP_RECEIVE_TIMEOUT:
            return MBEDTLS_ERR_SSL_WANT_READ;
        case NEXUS_TCP_RECEIVE_EOF:
            return 0;
        case NEXUS_TCP_RECEIVE_ERROR:
        default:
            return NEXUS_TLS_ERR_TRANSPORT;
    }
}

static uint64_t remaining_ticks(uint64_t start, uint64_t budget) {
    const uint64_t now = timer_ticks();
    if (now < start) return 0U;
    const uint64_t elapsed = now - start;
    return elapsed >= budget ? 0U : budget - elapsed;
}

void nexus_https_close(struct nexus_https_connection *connection) {
    if (connection == NULL) return;
    nexus_tls_client_free(&https_tls_client);
    if (connection->tcp_open) net_tcp_close();
    *connection = (struct nexus_https_connection){0};
}

int nexus_https_connect(struct nexus_https_connection *connection,
                        const char *hostname,
                        const unsigned char *ca_pem,
                        size_t ca_pem_length,
                        uint64_t timeout_ticks) {
    if (connection == NULL || hostname == NULL || hostname[0] == '\0' ||
        ca_pem == NULL || ca_pem_length < 2U ||
        ca_pem[ca_pem_length - 1U] != 0U || timeout_ticks == 0U) {
        return NEXUS_HTTPS_ERR_ARGUMENT;
    }

    *connection = (struct nexus_https_connection){0};
    const uint64_t start = timer_ticks();
    uint8_t remote_ipv4[4];
    if (!net_dns_resolve(hostname, remote_ipv4,
                         remaining_ticks(start, timeout_ticks))) {
        return remaining_ticks(start, timeout_ticks) == 0U
            ? NEXUS_HTTPS_ERR_TIMEOUT : NEXUS_HTTPS_ERR_DNS;
    }

    uint64_t remaining = remaining_ticks(start, timeout_ticks);
    if (remaining == 0U) return NEXUS_HTTPS_ERR_TIMEOUT;
    if (!net_tcp_connect(remote_ipv4, NEXUS_HTTPS_PORT, remaining)) {
        return remaining_ticks(start, timeout_ticks) == 0U
            ? NEXUS_HTTPS_ERR_TIMEOUT : NEXUS_HTTPS_ERR_CONNECT;
    }
    connection->tcp_open = true;

    const struct nexus_tls_io io = {
        .context = connection,
        .send = https_tcp_send,
        .receive = https_tcp_receive
    };
    int result = nexus_tls_client_init(
        &https_tls_client, hostname, ca_pem, ca_pem_length, &io);
    if (result != 0) {
        nexus_https_close(connection);
        return result;
    }
    connection->tls_ready = true;
    connection->private_state = &https_tls_client;

    for (;;) {
        result = nexus_tls_client_handshake(&https_tls_client);
        if (result == 0) break;
        if (result != MBEDTLS_ERR_SSL_WANT_READ &&
            result != MBEDTLS_ERR_SSL_WANT_WRITE) {
            nexus_https_close(connection);
            return result;
        }
        if (remaining_ticks(start, timeout_ticks) == 0U) {
            nexus_https_close(connection);
            return NEXUS_HTTPS_ERR_TIMEOUT;
        }
        net_poll();
        __asm__ volatile ("pause" ::: "memory");
    }

    if (!nexus_tls_client_is_verified(&https_tls_client)) {
        nexus_https_close(connection);
        return NEXUS_HTTPS_ERR_TLS;
    }
    connection->verified = true;
    return 0;
}

bool nexus_https_is_verified(
    const struct nexus_https_connection *connection) {
    return connection != NULL && connection->tcp_open &&
           connection->tls_ready && connection->verified &&
           nexus_tls_client_is_verified(&https_tls_client);
}

int nexus_https_write(struct nexus_https_connection *connection,
                      const unsigned char *buffer, size_t length) {
    if (!nexus_https_is_verified(connection))
        return NEXUS_TLS_ERR_NOT_CONNECTED;
    const int result = nexus_tls_client_write(&https_tls_client, buffer, length);
    if (result == MBEDTLS_ERR_SSL_WANT_READ) return NEXUS_HTTPS_WANT_READ;
    if (result == MBEDTLS_ERR_SSL_WANT_WRITE) return NEXUS_HTTPS_WANT_WRITE;
    return result;
}

int nexus_https_read(struct nexus_https_connection *connection,
                     unsigned char *buffer, size_t capacity) {
    if (!nexus_https_is_verified(connection))
        return NEXUS_TLS_ERR_NOT_CONNECTED;
    const int result = nexus_tls_client_read(&https_tls_client, buffer, capacity);
    if (result == MBEDTLS_ERR_SSL_WANT_READ) return NEXUS_HTTPS_WANT_READ;
    if (result == MBEDTLS_ERR_SSL_WANT_WRITE) return NEXUS_HTTPS_WANT_WRITE;
    return result;
}
