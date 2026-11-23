#ifndef NEXUS_BROWSER_HTTP_H
#define NEXUS_BROWSER_HTTP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NEXUS_BROWSER_HTTP_LOCATION_MAX 256U
#define NEXUS_BROWSER_HTTP_CONTENT_TYPE_MAX 80U
#define NEXUS_BROWSER_HTTP_FILENAME_MAX 128U
#define NEXUS_BROWSER_HTTP_HEADER_MAX 2048U

struct nexus_browser_http_response {
    uint16_t status_code;
    size_t body_offset;
    uint32_t content_length;
    bool has_content_length;
    bool chunked_transfer;
    char location[NEXUS_BROWSER_HTTP_LOCATION_MAX];
    char content_type[NEXUS_BROWSER_HTTP_CONTENT_TYPE_MAX];
    char filename[NEXUS_BROWSER_HTTP_FILENAME_MAX];
};

/* Parse bounded HTTP/1.x response headers without assuming NUL termination. */
bool nexus_browser_http_parse(const uint8_t *response, size_t response_length,
                              struct nexus_browser_http_response *parsed);

#endif
