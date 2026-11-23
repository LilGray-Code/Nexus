#ifndef NEXUS_BROWSER_SEARCH_H
#define NEXUS_BROWSER_SEARCH_H

#include <stdbool.h>
#include <stddef.h>

#define NEXUS_BROWSER_SEARCH_QUERY_MAX 192U
#define NEXUS_BROWSER_SEARCH_URL_MAX 640U

/*
 * Build a privacy-conscious DuckDuckGo Lite search URL. This only constructs
 * the request URL; HTTPS transport and certificate validation are separate
 * requirements and are deliberately not bypassed.
 */
bool nexus_browser_search_url(const char *query, char *output,
                              size_t output_capacity);

#endif
