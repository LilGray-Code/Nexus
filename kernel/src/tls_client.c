#include "tls_client.h"

#include "mbedtls_heap.h"
#include "mbedtls_time.h"
#include "sha256.h"

#include "psa/crypto.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

/*
 * TLS 1.3 servers may send post-handshake NewSessionTicket messages before
 * or between application records. Nexus does not currently cache tickets
 * for session resumption, so consume the notification and continue normal
 * I/O. Bound the number handled in one call to avoid a peer monopolizing the
 * caller with an unending stream of control messages.
 */
#define NEXUS_TLS_POST_HANDSHAKE_TICKET_LIMIT 8U

static void secure_zero(void *memory, size_t length) {
    volatile unsigned char *bytes = (volatile unsigned char *)memory;
    for (size_t i = 0U; i < length; ++i) bytes[i] = 0U;
}

static int tls_send_bridge(void *context, const unsigned char *buffer,
                           size_t length) {
    struct nexus_tls_client *client = (struct nexus_tls_client *)context;
    if (client == NULL || client->io.send == NULL ||
        (length != 0U && buffer == NULL) || length > (size_t)INT_MAX) {
        return MBEDTLS_ERR_SSL_BAD_INPUT_DATA;
    }
    if (length == 0U) return 0;

    const int result = client->io.send(client->io.context, buffer, length);
    if (result <= 0 || (size_t)result > length)
        return result < 0 ? result : NEXUS_TLS_ERR_TRANSPORT;
    return result;
}

static int tls_receive_bridge(void *context, unsigned char *buffer,
                              size_t capacity) {
    struct nexus_tls_client *client = (struct nexus_tls_client *)context;
    if (client == NULL || client->io.receive == NULL ||
        buffer == NULL || capacity == 0U || capacity > (size_t)INT_MAX) {
        return MBEDTLS_ERR_SSL_BAD_INPUT_DATA;
    }

    const int result =
        client->io.receive(client->io.context, buffer, capacity);
    if (result < 0) return result;
    if ((size_t)result > capacity) return NEXUS_TLS_ERR_TRANSPORT;
    return result; /* Zero is an orderly transport EOF. */
}

void nexus_tls_client_free(struct nexus_tls_client *client) {
    if (client == NULL) return;
    if (client->ssl_initialized) {
        if (client->handshake_complete)
            (void)mbedtls_ssl_close_notify(&client->ssl);
        mbedtls_ssl_free(&client->ssl);
    }
    if (client->config_initialized)
        mbedtls_ssl_config_free(&client->config);
    secure_zero(client, sizeof(*client));
}

/*
 * Mbed TLS 4 imports certificate public keys into the PSA subsystem. Keep
 * the parsed immutable root store for the lifetime of the kernel instead of
 * repeatedly importing hundreds of roots on every HTTPS connection. This
 * also means a changed custom NEXUS.CRT bundle requires a reboot to take
 * effect; it can never silently replace active trust anchors.
 */
static mbedtls_x509_crt tls_trust_store;
static uint8_t tls_trust_store_digest[NEXUS_SHA256_SIZE];
static bool tls_trust_store_initialized;
static bool tls_trust_store_loading;

static bool tls_digest_equal(const uint8_t left[NEXUS_SHA256_SIZE],
                             const uint8_t right[NEXUS_SHA256_SIZE]) {
    uint8_t difference = 0U;
    for (size_t i = 0U; i < NEXUS_SHA256_SIZE; ++i)
        difference |= (uint8_t)(left[i] ^ right[i]);
    return difference == 0U;
}

static int tls_trust_store_ensure(const unsigned char *ca_pem,
                                  size_t ca_pem_length) {
    uint8_t digest[NEXUS_SHA256_SIZE];
    nexus_sha256(ca_pem, ca_pem_length, digest);

    if (tls_trust_store_initialized) {
        const bool matches = tls_digest_equal(
            digest, tls_trust_store_digest);
        secure_zero(digest, sizeof(digest));
        return matches ? 0 : NEXUS_TLS_ERR_TRUST_STORE_MISMATCH;
    }
    if (tls_trust_store_loading) {
        secure_zero(digest, sizeof(digest));
        return NEXUS_TLS_ERR_TRUST_STORE_BUSY;
    }

    tls_trust_store_loading = true;
    mbedtls_x509_crt_init(&tls_trust_store);
    const int result = mbedtls_x509_crt_parse(
        &tls_trust_store, ca_pem, ca_pem_length);
    if (result != 0) {
        mbedtls_x509_crt_free(&tls_trust_store);
        secure_zero(digest, sizeof(digest));
        tls_trust_store_loading = false;
        return result;
    }

    for (size_t i = 0U; i < NEXUS_SHA256_SIZE; ++i)
        tls_trust_store_digest[i] = digest[i];
    secure_zero(digest, sizeof(digest));
    tls_trust_store_initialized = true;
    tls_trust_store_loading = false;
    return 0;
}

int nexus_tls_client_init(struct nexus_tls_client *client,
                          const char *hostname,
                          const unsigned char *ca_pem,
                          size_t ca_pem_length,
                          const struct nexus_tls_io *io) {
    if (client == NULL || hostname == NULL || hostname[0] == '\0' ||
        ca_pem == NULL || ca_pem_length < 2U ||
        ca_pem[ca_pem_length - 1U] != 0U || io == NULL ||
        io->send == NULL || io->receive == NULL) {
        return NEXUS_TLS_ERR_ARGUMENT;
    }

    *client = (struct nexus_tls_client){0};

#if !defined(MBEDTLS_SSL_PROTO_TLS1_3)
    return NEXUS_TLS_ERR_TLS13_UNAVAILABLE;
#else
    if (!nexus_mbedtls_heap_init())
        return NEXUS_TLS_ERR_HEAP_UNAVAILABLE;
    if (!nexus_mbedtls_time_init())
        return NEXUS_TLS_ERR_CLOCK_UNAVAILABLE;
    if (psa_crypto_init() != PSA_SUCCESS)
        return NEXUS_TLS_ERR_PSA_UNAVAILABLE;

    const int trust_result =
        tls_trust_store_ensure(ca_pem, ca_pem_length);
    if (trust_result != 0) return trust_result;

    client->io = *io;
    mbedtls_ssl_init(&client->ssl);
    client->ssl_initialized = true;
    mbedtls_ssl_config_init(&client->config);
    client->config_initialized = true;

    int result = mbedtls_ssl_config_defaults(
        &client->config, MBEDTLS_SSL_IS_CLIENT,
        MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
    if (result != 0) {
        nexus_tls_client_free(client);
        return result;
    }

    mbedtls_ssl_conf_authmode(&client->config, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&client->config, &tls_trust_store, NULL);
    mbedtls_ssl_conf_min_tls_version(
        &client->config, MBEDTLS_SSL_VERSION_TLS1_3);
    mbedtls_ssl_conf_max_tls_version(
        &client->config, MBEDTLS_SSL_VERSION_TLS1_3);

    result = mbedtls_ssl_setup(&client->ssl, &client->config);
    if (result != 0) {
        nexus_tls_client_free(client);
        return result;
    }
    result = mbedtls_ssl_set_hostname(&client->ssl, hostname);
    if (result != 0) {
        nexus_tls_client_free(client);
        return result;
    }

    mbedtls_ssl_set_bio(&client->ssl, client, tls_send_bridge,
                        tls_receive_bridge, NULL);
    client->initialized = true;
    return 0;
#endif
}

int nexus_tls_client_handshake(struct nexus_tls_client *client) {
    if (client == NULL || !client->initialized)
        return NEXUS_TLS_ERR_ARGUMENT;
    if (client->handshake_complete) return 0;

    const int result = mbedtls_ssl_handshake(&client->ssl);
    if (result != 0) return result;
    if (mbedtls_ssl_get_verify_result(&client->ssl) != 0U)
        return MBEDTLS_ERR_X509_CERT_VERIFY_FAILED;

    client->handshake_complete = true;
    return 0;
}

bool nexus_tls_client_is_verified(const struct nexus_tls_client *client) {
    return client != NULL && client->initialized &&
           client->handshake_complete &&
           mbedtls_ssl_get_verify_result(&client->ssl) == 0U;
}

int nexus_tls_client_write(struct nexus_tls_client *client,
                           const unsigned char *buffer, size_t length) {
    if (client == NULL || !client->handshake_complete ||
        (length != 0U && buffer == NULL) || length > (size_t)INT_MAX) {
        return NEXUS_TLS_ERR_NOT_CONNECTED;
    }
    for (unsigned int attempt = 0U;
         attempt < NEXUS_TLS_POST_HANDSHAKE_TICKET_LIMIT; ++attempt) {
        const int result = mbedtls_ssl_write(&client->ssl, buffer, length);
        if (result != MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET)
            return result;
        /* The ticket is parsed and safely retained by Mbed TLS. Nexus does
         * not implement session resumption yet, so continue the write.
         */
    }
    return MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET;
}

int nexus_tls_client_read(struct nexus_tls_client *client,
                          unsigned char *buffer, size_t capacity) {
    if (client == NULL || !client->handshake_complete || buffer == NULL ||
        capacity == 0U || capacity > (size_t)INT_MAX) {
        return NEXUS_TLS_ERR_NOT_CONNECTED;
    }
    for (unsigned int attempt = 0U;
         attempt < NEXUS_TLS_POST_HANDSHAKE_TICKET_LIMIT; ++attempt) {
        const int result = mbedtls_ssl_read(&client->ssl, buffer, capacity);
        if (result != MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET)
            return result;
        /* This is a valid TLS 1.3 post-handshake control message, not
         * application data or a transport failure. Continue to the next
         * record; a later call will deliver HTTP bytes or WANT_READ.
         */
    }
    return MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET;
}
