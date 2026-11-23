#include "browser_http.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_success_response(void) {
    static const uint8_t response[] =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Content-Length: 18\r\n"
        "Content-Disposition: attachment; filename=\"demo-file.txt\"\r\n"
        "Connection: close\r\n"
        "\r\n"
        "<html>hello</html>";
    struct nexus_browser_http_response parsed;
    assert(nexus_browser_http_parse(response, sizeof(response) - 1U, &parsed));
    assert(parsed.status_code == 200U);
    assert(strcmp(parsed.content_type, "text/html; charset=utf-8") == 0);
    assert(parsed.location[0] == '\0');
    assert(parsed.has_content_length);
    assert(parsed.content_length == 18U);
    assert(strcmp(parsed.filename, "demo-file.txt") == 0);
    assert(parsed.body_offset < sizeof(response) - 1U);
    assert(memcmp(response + parsed.body_offset, "<html>", 6U) == 0);
}

static void test_redirect_response(void) {
    static const uint8_t response[] =
        "HTTP/1.0 302 Found\r\n"
        "Location: https://example.com/search\r\n"
        "Content-Length: 0\r\n"
        "\r\n";
    struct nexus_browser_http_response parsed;
    assert(nexus_browser_http_parse(response, sizeof(response) - 1U, &parsed));
    assert(parsed.status_code == 302U);
    assert(strcmp(parsed.location, "https://example.com/search") == 0);
    assert(parsed.has_content_length && parsed.content_length == 0U);
}

static void test_chunked_response(void) {
    static const uint8_t response[] =
        "HTTP/1.1 200 OK\r\n"
        "Transfer-Encoding: chunked\r\n"
        "\r\n"
        "5\r\nhello\r\n0\r\n\r\n";
    struct nexus_browser_http_response parsed;
    assert(nexus_browser_http_parse(response, sizeof(response) - 1U, &parsed));
    assert(parsed.status_code == 200U);
    assert(parsed.chunked_transfer);
    assert(!parsed.has_content_length);
}

static void test_rejects_bad_content_length(void) {
    struct nexus_browser_http_response parsed;
    static const uint8_t malformed[] =
        "HTTP/1.1 200 OK\r\nContent-Length: 12x\r\n\r\n";
    static const uint8_t conflict[] =
        "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n"
        "Content-Length: 3\r\n\r\n";
    assert(!nexus_browser_http_parse(malformed, sizeof(malformed) - 1U,
                                     &parsed));
    assert(!nexus_browser_http_parse(conflict, sizeof(conflict) - 1U,
                                     &parsed));
}

static void test_rejects_malformed_or_unterminated_headers(void) {
    struct nexus_browser_http_response parsed;
    static const uint8_t bad_status[] = "HTTP/2 200 OK\r\n\r\n";
    static const uint8_t no_separator[] =
        "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n";
    static const uint8_t bad_header[] =
        "HTTP/1.1 200 OK\r\nContent-Type text/plain\r\n\r\n";
    assert(!nexus_browser_http_parse(bad_status, sizeof(bad_status) - 1U,
                                     &parsed));
    assert(!nexus_browser_http_parse(no_separator, sizeof(no_separator) - 1U,
                                     &parsed));
    assert(!nexus_browser_http_parse(bad_header, sizeof(bad_header) - 1U,
                                     &parsed));
    assert(!nexus_browser_http_parse(NULL, 0U, &parsed));
}

int main(void) {
    test_success_response();
    test_redirect_response();
    test_chunked_response();
    test_rejects_bad_content_length();
    test_rejects_malformed_or_unterminated_headers();
    puts("NEXUS BROWSER HTTP RESPONSE TESTS PASSED");
    return 0;
}
