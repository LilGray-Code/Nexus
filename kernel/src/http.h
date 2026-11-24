#ifndef NEXUS_HTTP_H
#define NEXUS_HTTP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NEXUS_HTTP_LOCATION_MAX 255U
#define NEXUS_HTTP_CONTENT_TYPE_MAX 63U

struct nexus_http_response {
    uint16_t status_code;
    size_t body_length;
    bool chunked;
    bool has_content_length;
    bool has_location;
    char location[NEXUS_HTTP_LOCATION_MAX + 1U];
    bool has_content_type;
    char content_type[NEXUS_HTTP_CONTENT_TYPE_MAX + 1U];
};

/*
 * Parse a complete HTTP/1.0 or HTTP/1.1 response into caller-owned storage.
 * Supports identity/close-delimited bodies, Content-Length and chunked
 * transfer encoding. Exposes bounded Location and Content-Type metadata for
 * redirect and MIME-aware browser handling. Unsupported transfer codings,
 * ambiguous framing, and malformed redirect metadata are rejected.
 */
bool nexus_http_decode_response(const uint8_t *wire, size_t wire_length,
                                uint8_t *body, size_t body_capacity,
                                struct nexus_http_response *response);

#endif
