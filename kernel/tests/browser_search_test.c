#include "browser_search.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_query_encoding(void) {
    char output[NEXUS_BROWSER_SEARCH_URL_MAX];
    assert(nexus_browser_search_url("nexus operating system", output,
                                    sizeof(output)));
    assert(strcmp(output,
        "https://lite.duckduckgo.com/lite/?q=nexus%20operating%20system") == 0);
    assert(nexus_browser_search_url("a&b=c", output, sizeof(output)));
    assert(strcmp(output,
        "https://lite.duckduckgo.com/lite/?q=a%26b%3Dc") == 0);
    assert(nexus_browser_search_url("C++ kernel", output, sizeof(output)));
    assert(strcmp(output,
        "https://lite.duckduckgo.com/lite/?q=C%2B%2B%20kernel") == 0);
}

static void test_rejects_bad_or_truncated_inputs(void) {
    char output[8];
    char too_long[NEXUS_BROWSER_SEARCH_QUERY_MAX + 2U];
    memset(too_long, 'a', sizeof(too_long) - 1U);
    too_long[sizeof(too_long) - 1U] = '\0';
    assert(!nexus_browser_search_url("", output, sizeof(output)));
    assert(!nexus_browser_search_url(NULL, output, sizeof(output)));
    assert(!nexus_browser_search_url("query", output, sizeof(output)));
    assert(!nexus_browser_search_url(too_long, output, sizeof(too_long)));
}

int main(void) {
    test_query_encoding();
    test_rejects_bad_or_truncated_inputs();
    puts("NEXUS BROWSER SEARCH URL TESTS PASSED");
    return 0;
}
