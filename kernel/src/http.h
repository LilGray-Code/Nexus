#ifndef NEXUS_HTTP_H
#define NEXUS_HTTP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NEXUS_HTTP_LOCATION_MAX 255U
#define NEXUS_HTTP_CONTENT_TYPE_MAX 63U
#define NEXUS_HTTP_DISPOSITION_MAX 255U

#define NEXUS_HTTP_CHUNK_LINE_MAX 512U

struct nexus_http_chunked_decoder {
    uint32_t maximum_bytes;
    uint32_t decoded_bytes;
    uint32_t chunk_remaining;
    uint32_t chunk_value;
    uint16_t line_length;
    uint8_t state;
    bool line_has_digits;
    bool line_has_colon;
    bool line_extension;
};

struct nexus_http_response {
    uint16_t status_code;
    size_t body_length;
    size_t content_length;
    bool chunked;
    bool has_content_length;
    bool has_location;
    char location[NEXUS_HTTP_LOCATION_MAX + 1U];
    bool has_content_type;
    char content_type[NEXUS_HTTP_CONTENT_TYPE_MAX + 1U];
    bool has_content_disposition;
    char content_disposition[NEXUS_HTTP_DISPOSITION_MAX + 1U];
};

/*
 * Parse a complete HTTP/1.0 or HTTP/1.1 response into caller-owned storage.
 * Supports identity/close-delimited bodies, Content-Length and chunked
 * transfer encoding. Exposes bounded Location and Content-Type metadata for
 * redirect and MIME-aware browser handling. Unsupported transfer codings,
 * ambiguous framing, and malformed redirect metadata are rejected.
 */
/* Parses a complete HTTP header block without requiring its body in RAM. */
bool nexus_http_decode_headers(const uint8_t *wire, size_t wire_length,
                               size_t *header_length,
                               struct nexus_http_response *response);

void nexus_http_chunked_decoder_init(
    struct nexus_http_chunked_decoder *decoder, uint32_t maximum_bytes);

/* Incrementally decode one HTTP chunked body. Input and output may be split
 * at arbitrary byte boundaries. Call again with unconsumed input until
 * input_used reaches input_length; complete is true only after valid trailers.
 */
bool nexus_http_chunked_decoder_feed(
    struct nexus_http_chunked_decoder *decoder,
    const uint8_t *input, size_t input_length, size_t *input_used,
    uint8_t *output, size_t output_capacity, size_t *output_length,
    bool *complete);

bool nexus_http_decode_response(const uint8_t *wire, size_t wire_length,
                                uint8_t *body, size_t body_capacity,
                                struct nexus_http_response *response);

#endif
