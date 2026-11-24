#include "net_https.h"
#include "tls_client.h"

#include "net.h"
#include "timer.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

static uint64_t fake_ticks;
static bool dns_success = true;
static bool tcp_success = true;
static bool tls_init_success = true;
static bool tls_verified;
static bool close_called;
static unsigned handshake_calls;
static unsigned send_calls;
static unsigned receive_calls;
static uint16_t connected_port;
static uint8_t connected_ip[4];
static const char *resolved_hostname;
static uint8_t received_payload[] = {0x54U, 0x4CU, 0x53U};
static struct nexus_tls_io saved_io;

uint64_t timer_ticks(void) { return ++fake_ticks; }
void net_poll(void) {}

bool net_dns_resolve(const char *hostname, uint8_t result[4],
                     uint64_t timeout_ticks) {
    assert(timeout_ticks != 0U);
    resolved_hostname = hostname;
    if (!dns_success) return false;
    result[0] = 203U; result[1] = 0U; result[2] = 113U; result[3] = 9U;
    return true;
}

bool net_tcp_connect(const uint8_t remote_ipv4[4], uint16_t remote_port,
                     uint64_t timeout_ticks) {
    assert(timeout_ticks != 0U);
    connected_port = remote_port;
    for (size_t i = 0U; i < 4U; ++i) connected_ip[i] = remote_ipv4[i];
    return tcp_success;
}

bool net_tcp_send(const void *payload, uint16_t payload_length) {
    assert(payload != NULL && payload_length > 0U);
    ++send_calls;
    return true;
}

enum nexus_tcp_receive_result net_tcp_receive_wait(
    void *buffer, size_t capacity, uint64_t timeout_ticks, size_t *received) {
    assert(buffer != NULL && capacity > 0U && timeout_ticks > 0U);
    ++receive_calls;
    if (receive_calls == 1U) {
        *received = 0U;
        return NEXUS_TCP_RECEIVE_TIMEOUT;
    }
    size_t amount = sizeof(received_payload);
    if (amount > capacity) amount = capacity;
    for (size_t i = 0U; i < amount; ++i)
        ((uint8_t *)buffer)[i] = received_payload[i];
    *received = amount;
    return NEXUS_TCP_RECEIVE_DATA;
}

void net_tcp_close(void) { close_called = true; }

int nexus_tls_client_init(struct nexus_tls_client *client,
                          const char *hostname,
                          const unsigned char *ca_pem,
                          size_t ca_pem_length,
                          const struct nexus_tls_io *io) {
    assert(hostname != NULL && ca_pem != NULL && ca_pem_length > 1U);
    assert(io != NULL && io->send != NULL && io->receive != NULL);
    if (!tls_init_success) return -77;
    client->initialized = true;
    saved_io = *io;
    tls_verified = false;
    return 0;
}

int nexus_tls_client_handshake(struct nexus_tls_client *client) {
    assert(client->initialized);
    ++handshake_calls;
    if (handshake_calls == 1U) {
        unsigned char output[16];
        assert(saved_io.receive(saved_io.context, output, sizeof(output)) ==
               MBEDTLS_ERR_SSL_WANT_READ);
        return MBEDTLS_ERR_SSL_WANT_READ;
    }
    unsigned char payload[] = {1U, 2U, 3U};
    assert(saved_io.send(saved_io.context, payload, sizeof(payload)) == 3);
    tls_verified = true;
    client->handshake_complete = true;
    return 0;
}

bool nexus_tls_client_is_verified(const struct nexus_tls_client *client) {
    return client != NULL && client->initialized && tls_verified;
}

int nexus_tls_client_write(struct nexus_tls_client *client,
                           const unsigned char *buffer, size_t length) {
    assert(client->handshake_complete && buffer != NULL && length > 0U);
    return (int)length;
}

int nexus_tls_client_read(struct nexus_tls_client *client,
                          unsigned char *buffer, size_t capacity) {
    assert(client->handshake_complete && buffer != NULL && capacity > 0U);
    return 7;
}

void nexus_tls_client_free(struct nexus_tls_client *client) {
    if (client != NULL) {
        client->initialized = false;
        client->handshake_complete = false;
        tls_verified = false;
    }
}

static void reset_test(void) {
    fake_ticks = 0U;
    dns_success = true;
    tcp_success = true;
    tls_init_success = true;
    tls_verified = false;
    close_called = false;
    handshake_calls = 0U;
    send_calls = 0U;
    receive_calls = 0U;
    connected_port = 0U;
    connected_ip[0] = connected_ip[1] = connected_ip[2] = connected_ip[3] = 0U;
    resolved_hostname = NULL;
    saved_io = (struct nexus_tls_io){0};
}

int main(void) {
    static const unsigned char trust[] = "test trust bundle";
    struct nexus_https_connection connection = {0};
    reset_test();
    assert(nexus_https_connect(&connection, "example.test", trust,
                               sizeof(trust), 500U) == 0);
    assert(resolved_hostname != NULL);
    assert(connected_port == 443U);
    assert(connected_ip[0] == 203U && connected_ip[3] == 9U);
    assert(handshake_calls == 2U);
    assert(receive_calls == 1U && send_calls == 1U);
    assert(nexus_https_is_verified(&connection));

    const unsigned char request[] = "GET / HTTP/1.1";
    assert(nexus_https_write(&connection, request, sizeof(request) - 1U) ==
           (int)(sizeof(request) - 1U));
    unsigned char response[16];
    assert(nexus_https_read(&connection, response, sizeof(response)) == 7);

    nexus_https_close(&connection);
    assert(close_called);
    assert(!nexus_https_is_verified(&connection));

    reset_test();
    assert(nexus_https_connect_port(&connection, "example.test", 8443U,
                                    trust, sizeof(trust), 500U) == 0);
    assert(connected_port == 8443U);
    assert(resolved_hostname != NULL);
    nexus_https_close(&connection);

    reset_test();
    assert(nexus_https_connect_port(&connection, "example.test", 0U,
                                    trust, sizeof(trust), 500U) ==
           NEXUS_HTTPS_ERR_ARGUMENT);
    assert(connected_port == 0U);

    reset_test();
    dns_success = false;
    assert(nexus_https_connect(&connection, "missing.test", trust,
                               sizeof(trust), 500U) == NEXUS_HTTPS_ERR_DNS);
    assert(!close_called);

    reset_test();
    tcp_success = false;
    assert(nexus_https_connect(&connection, "example.test", trust,
                               sizeof(trust), 500U) == NEXUS_HTTPS_ERR_CONNECT);

    reset_test();
    tls_init_success = false;
    assert(nexus_https_connect(&connection, "example.test", trust,
                               sizeof(trust), 500U) == -77);
    assert(close_called);

    reset_test();
    assert(nexus_https_connect(&connection, "example.test", trust,
                               sizeof(trust), 0U) == NEXUS_HTTPS_ERR_ARGUMENT);
    return 0;
}
