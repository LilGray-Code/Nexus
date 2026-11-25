#ifndef NEXUS_BROWSER_SEARCH_H
#define NEXUS_BROWSER_SEARCH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NEXUS_BROWSER_SEARCH_MAX_RESULTS 8U
#define NEXUS_BROWSER_SEARCH_TITLE_MAX 96U
#define NEXUS_BROWSER_SEARCH_URL_MAX 256U
#define NEXUS_BROWSER_SEARCH_SNIPPET_MAX 144U
#define NEXUS_BROWSER_SEARCH_PATH_MAX 800U

struct nexus_browser_search_result {
    char title[NEXUS_BROWSER_SEARCH_TITLE_MAX];
    char url[NEXUS_BROWSER_SEARCH_URL_MAX];
    char snippet[NEXUS_BROWSER_SEARCH_SNIPPET_MAX];
};

bool nexus_browser_search_build_path(
    const char *query, size_t query_length,
    char *path, size_t path_capacity, size_t *path_length);

/* Builds the no-JavaScript HTML search form body, including region defaults. */
bool nexus_browser_search_build_form_body(
    const char *query, size_t query_length,
    char *body, size_t body_capacity, size_t *body_length);

/* Detects a provider anti-bot/interstitial response instead of treating it
 * as an empty search-result page. */
bool nexus_browser_search_is_challenge(
    const uint8_t *html, size_t html_length);

bool nexus_browser_search_parse(
    const uint8_t *html, size_t html_length,
    struct nexus_browser_search_result *results,
    size_t result_capacity, size_t *result_count);

/* Maps rendered search-document lines to a result index.
 * The first two lines are the heading; each result occupies four lines.
 */
size_t nexus_browser_search_result_for_line(size_t document_line);

#endif
