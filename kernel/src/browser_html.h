#ifndef NEXUS_BROWSER_HTML_H
#define NEXUS_BROWSER_HTML_H

#include <stddef.h>
#include <stdint.h>
#include "browser_url.h"

#define NEXUS_BROWSER_HTML_LINK_MAX 256U

/* Extract readable ASCII text, skipping comments and script/style contents.
 * Returns bytes written (excluding the terminating NUL). */
size_t nexus_browser_html_to_text(const uint8_t *html, size_t length,
                                  char *output, size_t capacity);

/* Collect distinct absolute HTTP(S) links from anchor href attributes. */
size_t nexus_browser_html_extract_links(
    const uint8_t *html, size_t length,
    char (*links)[NEXUS_BROWSER_HTML_LINK_MAX], size_t link_capacity);

/* Resolve safe absolute and relative anchor hrefs against the loaded page. */
size_t nexus_browser_html_extract_links_with_base(
    const uint8_t *html, size_t length, const char *base_url,
    char (*links)[NEXUS_BROWSER_HTML_LINK_MAX], size_t link_capacity);

#endif
