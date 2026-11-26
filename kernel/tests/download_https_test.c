#include "download_https.h"
#include "http.h"
#include "net.h"
#include "net_https.h"
#include "timer.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const unsigned char fake_ca[] = "test CA bundle";
static uint8_t fake_wire[4096];
static size_t fake_wire_length;
static size_t fake_wire_offset;
static char fake_host[129];
static char fake_request[1024];
static size_t fake_request_length;
static bool fake_verified = true;
static uint16_t fake_status = 200U;
static bool fake_has_content_length = true;
static size_t fake_content_length = 10U;
static bool fake_chunked;
static uint64_t fake_ticks;

const unsigned char *nexus_builtin_ca_bundle_get(size_t *length) {
    if (length != NULL) *length = sizeof(fake_ca);
    return fake_ca;
}
int nexus_https_connect(struct nexus_https_connection *connection,
                        const char *hostname,
                        const unsigned char *ca_pem, size_t ca_pem_length,
                        uint64_t timeout_ticks) {
    (void)connection;
    assert(hostname != NULL && ca_pem == fake_ca);
    assert(ca_pem_length == sizeof(fake_ca));
    assert(timeout_ticks == NEXUS_DOWNLOAD_HTTPS_TIMEOUT_TICKS);
    strcpy(fake_host, hostname);
    return fake_verified ? 0 : NEXUS_HTTPS_ERR_TLS;
}
bool nexus_https_is_verified(
    const struct nexus_https_connection *connection) {
    (void)connection;
    return fake_verified;
}
int nexus_https_write(struct nexus_https_connection *connection,
                      const unsigned char *buffer, size_t length) {
    (void)connection;
    size_t amount = length > 13U ? 13U : length;
    assert(fake_request_length + amount < sizeof(fake_request));
    memcpy(fake_request + fake_request_length, buffer, amount);
    fake_request_length += amount;
    fake_request[fake_request_length] = '\0';
    return (int)amount;
}
int nexus_https_read(struct nexus_https_connection *connection,
                     unsigned char *buffer, size_t capacity) {
    (void)connection;
    if (fake_wire_offset >= fake_wire_length) return 0;
    size_t amount = fake_wire_length - fake_wire_offset;
    if (amount > capacity) amount = capacity;
    if (amount > 37U) amount = 37U; /* Force split status/header and body. */
    memcpy(buffer, fake_wire + fake_wire_offset, amount);
    fake_wire_offset += amount;
    return (int)amount;
}
void nexus_https_close(struct nexus_https_connection *connection) {
    (void)connection;
}
void net_poll(void) { ++fake_ticks; }
uint64_t timer_ticks(void) { return fake_ticks; }

static void make_response(void) {
    char headers[512];
    char length_text[16];
    const int length_count = snprintf(length_text, sizeof(length_text), "%u",
                                      (unsigned)fake_content_length);
    assert(length_count > 0 && (size_t)length_count < sizeof(length_text));
    const int count = snprintf(headers, sizeof(headers),
        "HTTP/1.1 %u Test\r\n%s%s%s%s\r\n",
        (unsigned)fake_status,
        fake_has_content_length ? "Content-Length: " : "",
        fake_has_content_length ? length_text : "",
        fake_has_content_length ? "\r\n" : "",
        fake_chunked ? "Transfer-Encoding: chunked\r\n" : "");
    assert(count > 0 && (size_t)count < sizeof(headers));
    fake_wire_length = (size_t)count + 10U;
    memcpy(fake_wire, headers, (size_t)count);
    memcpy(fake_wire + count, "1234567890", 10U);
    fake_wire_offset = 0U;
}

static void reset(void) {
    fake_wire_length = 0U;
    fake_wire_offset = 0U;
    fake_host[0] = '\0';
    fake_request[0] = '\0';
    fake_request_length = 0U;
    fake_verified = true;
    fake_status = 200U;
    fake_has_content_length = true;
    fake_content_length = 10U;
    fake_chunked = false;
    fake_ticks = 0U;
    make_response();
}

static void test_url_parser(void) {
    char host[129], path[193];
    assert(nexus_download_https_parse_url(
        "https://repo.example/packages/paint.nup?channel=stable",
        host, path));
    assert(strcmp(host, "repo.example") == 0);
    assert(strcmp(path, "/packages/paint.nup?channel=stable") == 0);
    assert(nexus_download_https_parse_url("https://repo.example", host, path));
    assert(strcmp(path, "/") == 0);
    assert(nexus_download_https_parse_url("https://repo.example?q=1",
                                          host, path));
    assert(strcmp(path, "/?q=1") == 0);
    assert(!nexus_download_https_parse_url("http://repo.example/a", host, path));
    assert(!nexus_download_https_parse_url("https://repo.example:443/a", host, path));
    assert(!nexus_download_https_parse_url("https://user@repo.example/a", host, path));
    assert(!nexus_download_https_parse_url("https://repo.example/a#fragment",
                                           host, path));
    assert(!nexus_download_https_parse_url("https://repo.example/a\r\nHost:x",
                                           host, path));
}

static void test_verified_content_length_download(void) {
    reset();
    struct nexus_download_https_context context;
    assert(nexus_download_https_context_init(&context, 10U));
    const struct nexus_download_source_ops *source =
        nexus_download_https_source_ops();
    void *session = NULL;
    assert(source->open("https://repo.example/apps/demo.nup", &session,
                        &context));
    assert(session == &context);
    assert(strcmp(fake_host, "repo.example") == 0);
    assert(strstr(fake_request,
        "GET /apps/demo.nup HTTP/1.1\r\nHost: repo.example\r\n") != NULL);
    assert(strstr(fake_request, "Accept-Encoding: identity") != NULL);
    uint8_t result[10];
    size_t received = 0U;
    while (received < sizeof(result)) {
        const int amount = source->read(session, result + received,
            sizeof(result) - received, &context);
        assert(amount > 0);
        received += (size_t)amount;
    }
    assert(memcmp(result, "1234567890", sizeof(result)) == 0);
    assert(source->read(session, result, sizeof(result), &context) == 0);
    source->close(session, &context);
    assert(!context.connected);
}

static void test_reject_bad_status_length_framing_and_tls(void) {
    struct nexus_download_https_context context;
    void *session = NULL;
    const struct nexus_download_source_ops *source =
        nexus_download_https_source_ops();

    reset();
    fake_status = 302U;
    make_response();
    assert(nexus_download_https_context_init(&context, 10U));
    assert(!source->open("https://repo.example/demo.nup", &session,
                         &context));

    reset();
    fake_has_content_length = false;
    make_response();
    assert(nexus_download_https_context_init(&context, 10U));
    assert(!source->open("https://repo.example/demo.nup", &session,
                         &context));

    reset();
    fake_content_length = 11U;
    make_response();
    assert(nexus_download_https_context_init(&context, 10U));
    assert(!source->open("https://repo.example/demo.nup", &session,
                         &context));

    reset();
    fake_chunked = true;
    make_response();
    assert(nexus_download_https_context_init(&context, 10U));
    assert(!source->open("https://repo.example/demo.nup", &session,
                         &context));

    reset();
    fake_verified = false;
    assert(nexus_download_https_context_init(&context, 10U));
    assert(!source->open("https://repo.example/demo.nup", &session,
                         &context));
}

int main(void) {
    test_url_parser();
    test_verified_content_length_download();
    test_reject_bad_status_length_framing_and_tls();
    puts("NEXUS HTTPS PACKAGE DOWNLOAD SOURCE TESTS PASSED");
    return 0;
}
