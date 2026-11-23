#ifndef NEXUS_BROWSER_HTML_H
#define NEXUS_BROWSER_HTML_H

#include <stddef.h>
#include <stdint.h>

/* Extract readable ASCII text, skipping comments and script/style contents.
 * Returns bytes written (excluding the terminating NUL). */
size_t nexus_browser_html_to_text(const uint8_t *html, size_t length,
                                  char *output, size_t capacity);

#endif
