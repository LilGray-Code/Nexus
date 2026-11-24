#ifndef NEXUS_HTTP_H
#define NEXUS_HTTP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct nexus_http_response {
    uint16_t status_code;
    size_t body_length;
    bool chunked;
    bool has_content_length;
};

/*
 * Parse a complete HTTP/1.0 or HTTP/1.1 response into a caller-owned body
 * buffer. Supports identity/close-delimited bodies, Content-Length, and
 * Transfer-Encoding: chunked. Unsupported transfer codings and ambiguous
 * framing are rejected. The function performs no allocation.
 */
bool nexus_http_decode_response(const uint8_t *wire, size_t wire_length,
                                uint8_t *body, size_t body_capacity,
                                struct nexus_http_response *response);

#endif
