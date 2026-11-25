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

bool nexus_browser_search_parse(
    const uint8_t *html, size_t html_length,
    struct nexus_browser_search_result *results,
    size_t result_capacity, size_t *result_count);

#endif
