#include "tls_client.h"

#include "random.h"
#include "rtc.h"
#include "timer.h"
#include "mbedtls/platform_time.h"
#include "mbedtls/platform_util.h"
#include "tls_heap.h"
#include "mbedtls_time.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static mbedtls_time_t (*registered_time)(mbedtls_time_t *);
static uint64_t fake_epoch = 1795500000ULL;
static uint64_t fake_ticks;

bool rtc_get_unix_time(uint64_t *seconds) {
    if (seconds == NULL || fake_epoch == 0U) return false;
    *seconds = fake_epoch;
    return true;
}

uint64_t timer_ticks(void) { return ++fake_ticks; }

int mbedtls_platform_set_time(mbedtls_time_t (*callback)(mbedtls_time_t *)) {
    registered_time = callback;
    return 0;
}

bool nexus_rng_fill(void *output, size_t length) {
    /* Deterministic entropy is permitted only in this host adapter test. */
    unsigned char *bytes = (unsigned char *)output;
    if (length != 0U && output == NULL) return false;
    for (size_t i = 0U; i < length; ++i)
        bytes[i] = (unsigned char)(0x5AU ^ (unsigned char)(i * 29U));
    return true;
}

static int blocked_send(void *context, const unsigned char *buffer,
                        size_t length) {
    (void)context;
    (void)buffer;
    (void)length;
    return MBEDTLS_ERR_SSL_WANT_WRITE;
}

static int blocked_receive(void *context, unsigned char *buffer,
                           size_t capacity) {
    (void)context;
    (void)buffer;
    (void)capacity;
    return MBEDTLS_ERR_SSL_WANT_READ;
}

static unsigned char *read_pem(const char *path, size_t *length) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) return NULL;
    if (fseek(file, 0L, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    const long file_size = ftell(file);
    if (file_size <= 0L || file_size > 1024L * 1024L ||
        fseek(file, 0L, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    unsigned char *buffer = (unsigned char *)malloc((size_t)file_size + 1U);
    if (buffer == NULL) {
        fclose(file);
        return NULL;
    }
    const size_t read_size = fread(buffer, 1U, (size_t)file_size, file);
    const int close_result = fclose(file);
    if (read_size != (size_t)file_size || close_result != 0) {
        free(buffer);
        return NULL;
    }
    buffer[read_size] = 0U;
    *length = read_size + 1U;
    return buffer;
}

int main(int argc, char **argv) {
    assert(argc == 2);
    size_t pem_length = 0U;
    unsigned char *pem = read_pem(argv[1], &pem_length);
    assert(pem != NULL && pem_length > 1U);

    assert(nexus_tls_heap_init());
    assert(registered_time == NULL);
    struct nexus_tls_io io = {
        .context = NULL,
        .send = blocked_send,
        .receive = blocked_receive
    };
    struct nexus_tls_client client = {0};

    const int initialized = nexus_tls_client_init(
        &client, "localhost", pem, pem_length, &io);
    assert(initialized == 0);
    assert(client.initialized);
    assert(!nexus_tls_client_is_verified(&client));
    assert(nexus_tls_client_write(&client, pem, 1U) ==
           NEXUS_TLS_ERR_NOT_CONNECTED);

    const int handshake = nexus_tls_client_handshake(&client);
    assert(handshake == MBEDTLS_ERR_SSL_WANT_WRITE ||
           handshake == MBEDTLS_ERR_SSL_WANT_READ);
    assert(!nexus_tls_client_is_verified(&client));
    nexus_tls_client_free(&client);
    assert(!client.initialized);

    static const unsigned char invalid_pem[] = "not a certificate";
    const int rejected = nexus_tls_client_init(
        &client, "localhost", invalid_pem, sizeof(invalid_pem), &io);
    assert(rejected != 0);
    assert(!client.initialized);
    nexus_tls_client_free(&client);

    free(pem);
    return 0;
}
