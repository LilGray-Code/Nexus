#include "http.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static void test_content_length(void) {
    static const uint8_t wire[] =
        "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n"
        "Content-Type: text/plain\r\n\r\nhelloEXTRA";
    uint8_t body[16] = {0};
    struct nexus_http_response response;
    assert(nexus_http_decode_response(wire, sizeof(wire) - 1U,
                                      body, sizeof(body), &response));
    assert(response.status_code == 200U);
    assert(response.body_length == 5U);
    assert(response.has_content_length && !response.chunked);
    assert(body[0] == 'h' && body[4] == 'o');
}

static void test_chunked_extensions_and_trailers(void) {
    static const uint8_t wire[] =
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
        "4;name=value\r\nWiki\r\n5\r\npedia\r\n"
        "0\r\nX-Trace: ok\r\n\r\n";
    uint8_t body[16] = {0};
    struct nexus_http_response response;
    assert(nexus_http_decode_response(wire, sizeof(wire) - 1U,
                                      body, sizeof(body), &response));
    assert(response.status_code == 200U);
    assert(response.body_length == 9U && response.chunked);
    assert(body[0] == 'W' && body[8] == 'a');
}

static void test_close_delimited_and_error_status(void) {
    static const uint8_t wire[] =
        "HTTP/1.0 404 Not Found\r\nConnection: close\r\n\r\nmissing";
    uint8_t body[16] = {0};
    struct nexus_http_response response;
    assert(nexus_http_decode_response(wire, sizeof(wire) - 1U,
                                      body, sizeof(body), &response));
    assert(response.status_code == 404U);
    assert(response.body_length == 7U && !response.has_content_length);
    assert(body[0] == 'm' && body[6] == 'g');
}

static void test_reject_ambiguous_or_truncated(void) {
    static const uint8_t truncated[] =
        "HTTP/1.1 200 OK\r\nContent-Length: 8\r\n\r\nshort";
    static const uint8_t conflict[] =
        "HTTP/1.1 200 OK\r\nContent-Length: 1\r\n"
        "Transfer-Encoding: chunked\r\n\r\n0\r\n\r\n";
    static const uint8_t mismatch[] =
        "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n"
        "Content-Length: 3\r\n\r\nabc";
    static const uint8_t bad_chunk[] =
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
        "3\r\nabcXX0\r\n\r\n";
    uint8_t body[16] = {0};
    struct nexus_http_response response;
    assert(!nexus_http_decode_response(truncated, sizeof(truncated) - 1U,
                                       body, sizeof(body), &response));
    assert(!nexus_http_decode_response(conflict, sizeof(conflict) - 1U,
                                       body, sizeof(body), &response));
    assert(!nexus_http_decode_response(mismatch, sizeof(mismatch) - 1U,
                                       body, sizeof(body), &response));
    assert(!nexus_http_decode_response(bad_chunk, sizeof(bad_chunk) - 1U,
                                       body, sizeof(body), &response));
}

static void test_capacity_and_malformed_status(void) {
    static const uint8_t oversized[] =
        "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello";
    static const uint8_t bad_status[] =
        "HTTP/9.9 200 OK\r\nContent-Length: 0\r\n\r\n";
    uint8_t body[4] = {0};
    struct nexus_http_response response;
    assert(!nexus_http_decode_response(oversized, sizeof(oversized) - 1U,
                                       body, sizeof(body), &response));
    assert(!nexus_http_decode_response(bad_status, sizeof(bad_status) - 1U,
                                       body, sizeof(body), &response));
}

static void test_redirect_and_content_type_metadata(void) {
    static const uint8_t redirect[] =
        "HTTP/1.1 302 Found\r\n"
        "Location: https://www.example.test/new-page\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Content-Length: 0\r\n\r\n";
    static const uint8_t duplicate_location[] =
        "HTTP/1.1 301 Moved Permanently\r\n"
        "Location: /first\r\nLocation: /second\r\n"
        "Content-Length: 0\r\n\r\n";
    static const uint8_t unsafe_location[] =
        "HTTP/1.1 302 Found\r\n"
        "Location: /bad\001Injected\r\n"
        "Content-Length: 0\r\n\r\n";
    uint8_t body[8] = {0};
    struct nexus_http_response response;
    assert(nexus_http_decode_response(
        redirect, sizeof(redirect) - 1U, body, sizeof(body), &response));
    assert(response.status_code == 302U && response.has_location);
    assert(strcmp(response.location,
                  "https://www.example.test/new-page") == 0);
    assert(response.has_content_type);
    assert(strcmp(response.content_type,
                  "text/html; charset=utf-8") == 0);
    assert(!nexus_http_decode_response(
        duplicate_location, sizeof(duplicate_location) - 1U,
        body, sizeof(body), &response));
    assert(!nexus_http_decode_response(
        unsafe_location, sizeof(unsafe_location) - 1U,
        body, sizeof(body), &response));
}

static void test_content_disposition_metadata(void) {
    static const uint8_t download[] =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/pdf\r\n"
        "Content-Disposition: attachment; filename=Nexus_Guide.pdf\r\n"
        "Content-Length: 4\r\n\r\n%PDF";
    static const uint8_t unsafe_disposition[] =
        "HTTP/1.1 200 OK\r\n"
        "Content-Disposition: attachment; filename=bad\001name.exe\r\n"
        "Content-Length: 0\r\n\r\n";
    uint8_t body[8] = {0U};
    struct nexus_http_response response;
    assert(nexus_http_decode_response(
        download, sizeof(download) - 1U, body, sizeof(body), &response));
    assert(response.has_content_type);
    assert(response.has_content_disposition);
    assert(strcmp(response.content_disposition,
                  "attachment; filename=Nexus_Guide.pdf") == 0);
    assert(body[0] == '%' && body[3] == 'F');
    assert(!nexus_http_decode_response(
        unsafe_disposition, sizeof(unsafe_disposition) - 1U,
        body, sizeof(body), &response));
}


static void test_incremental_chunked_decoder(void) {
    static const uint8_t wire[] =
        "4;name=value\r\nWiki\r\n"
        "5\r\npedia\r\n"
        "0\r\nX-Trace: ok\r\n\r\n";
    struct nexus_http_chunked_decoder decoder;
    nexus_http_chunked_decoder_init(&decoder, 64U);
    uint8_t output[3] = {0U};
    char decoded[16] = {0};
    size_t decoded_length = 0U;
    size_t offset = 0U;
    bool complete = false;
    while (!complete) {
        const size_t fragment = sizeof(wire) - 1U - offset > 2U
            ? 2U : sizeof(wire) - 1U - offset;
        size_t used = 0U, produced = 0U;
        assert(nexus_http_chunked_decoder_feed(
            &decoder, wire + offset, fragment, &used,
            output, sizeof(output), &produced, &complete));
        offset += used;
        assert(decoded_length + produced <= sizeof(decoded));
        memcpy(decoded + decoded_length, output, produced);
        decoded_length += produced;
        assert(used != 0U || produced != 0U || complete);
    }
    assert(offset == sizeof(wire) - 1U);
    assert(decoded_length == 9U);
    assert(memcmp(decoded, "Wikipedia", 9U) == 0);
    assert(decoder.decoded_bytes == 9U);

    static const uint8_t oversized[] = "9\r\n123456789\r\n0\r\n\r\n";
    nexus_http_chunked_decoder_init(&decoder, 8U);
    size_t used = 0U, produced = 0U;
    assert(!nexus_http_chunked_decoder_feed(
        &decoder, oversized, sizeof(oversized) - 1U, &used,
        output, sizeof(output), &produced, &complete));

    static const uint8_t malformed[] = "3\r\nabcXX0\r\n\r\n";
    nexus_http_chunked_decoder_init(&decoder, 64U);
    assert(!nexus_http_chunked_decoder_feed(
        &decoder, malformed, sizeof(malformed) - 1U, &used,
        output, sizeof(output), &produced, &complete));
}

static void test_header_only_parse_for_streaming(void) {
    static const uint8_t header[] =
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 268435456\r\n"
        "Content-Type: application/zip\r\n"
        "Content-Disposition: attachment; filename=large.zip\r\n"
        "Connection: close\r\n\r\n";
    size_t header_length = 0U;
    struct nexus_http_response response = {0};
    assert(nexus_http_decode_headers(header, sizeof(header) - 1U,
                                     &header_length, &response));
    assert(header_length == sizeof(header) - 1U);
    assert(response.status_code == 200U);
    assert(response.has_content_length);
    assert(response.content_length == 268435456U);
    assert(response.has_content_type && response.has_content_disposition);
    assert(strcmp(response.content_type, "application/zip") == 0);
    assert(strcmp(response.content_disposition,
                  "attachment; filename=large.zip") == 0);
    assert(response.body_length == 0U);

    static const uint8_t conflicting[] =
        "HTTP/1.1 200 OK\r\nContent-Length: 1\r\n"
        "Transfer-Encoding: chunked\r\n\r\n";
    assert(!nexus_http_decode_headers(conflicting, sizeof(conflicting) - 1U,
                                      &header_length, &response));
}

int main(void) {
    test_content_length();
    test_chunked_extensions_and_trailers();
    test_close_delimited_and_error_status();
    test_reject_ambiguous_or_truncated();
    test_capacity_and_malformed_status();
    test_redirect_and_content_type_metadata();
    test_content_disposition_metadata();
    test_header_only_parse_for_streaming();
    test_incremental_chunked_decoder();
    return 0;
}
