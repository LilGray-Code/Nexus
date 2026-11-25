#include "browser_search.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static void test_query_encoding(void) {
    char path[NEXUS_BROWSER_SEARCH_PATH_MAX];
    size_t length = 0U;
    static const char query[] = "Nexus OS & Wi-Fi?";
    assert(nexus_browser_search_build_path(
        query, sizeof(query) - 1U, path, sizeof(path), &length));
    assert(strcmp(path,
        "/html/?q=Nexus%20OS%20%26%20Wi-Fi%3F") == 0);
    assert(length == strlen(path));
    char form_body[NEXUS_BROWSER_SEARCH_PATH_MAX];
    size_t form_length = 0U;
    assert(nexus_browser_search_build_form_body(
        query, sizeof(query) - 1U, form_body, sizeof(form_body), &form_length));
    assert(strcmp(form_body,
        "q=Nexus%20OS%20%26%20Wi-Fi%3F&b=&kl=us-en") == 0);
    assert(form_length == strlen(form_body));
    assert(!nexus_browser_search_build_path(
        "", 0U, path, sizeof(path), &length));
    assert(!nexus_browser_search_build_path(
        "bad\nquery", 9U, path, sizeof(path), &length));
    char tiny[8];
    assert(!nexus_browser_search_build_path(
        query, sizeof(query) - 1U, tiny, sizeof(tiny), &length));
}

static void test_result_parse_and_redirect_decode(void) {
    static const uint8_t html[] =
        "<div class=\"result results_links\">"
        "<a class=\"result__a\" rel=\"nofollow\" "
        "href=\"//duckduckgo.com/l/?uddg=https%3A%2F%2Fexample.com%2Fdocs"
        "%3Fx%3D1%26y%3D2&amp;rut=abc\">Example &amp; Documentation</a>"
        "<a class=\"result__snippet\">A useful <b>documentation</b> page "
        "with &quot;details&quot;.</a></div>"
        "<div class=\"result\">"
        "<a href=\"https://nexus.example/\" class=\"result__a\">"
        "Nexus Site</a>"
        "<div class=\"result__snippet\">Original OS project.</div>"
        "</div>";
    struct nexus_browser_search_result results[NEXUS_BROWSER_SEARCH_MAX_RESULTS];
    size_t count = 0U;
    assert(nexus_browser_search_parse(
        html, sizeof(html) - 1U, results,
        NEXUS_BROWSER_SEARCH_MAX_RESULTS, &count));
    assert(count == 2U);
    assert(strcmp(results[0].title, "Example & Documentation") == 0);
    assert(strcmp(results[0].url,
        "https://example.com/docs?x=1&y=2") == 0);
    assert(strstr(results[0].snippet, "documentation") != NULL);
    assert(strcmp(results[1].title, "Nexus Site") == 0);
    assert(strcmp(results[1].url, "https://nexus.example/") == 0);
    assert(strcmp(results[1].snippet, "Original OS project.") == 0);
}

static void test_reject_unsafe_links_and_deduplicate(void) {
    static const uint8_t html[] =
        "<a class=\"result__a\" href=\"javascript:alert(1)\">Bad</a>"
        "<a class=\"result__a\" href=\"data:text/html,bad\">Bad too</a>"
        "<a class=\"result__a\" href=\"https://safe.example/\">"
        "Safe page</a>"
        "<a class=\"result__a\" href=\"https://safe.example/\">"
        "Duplicate</a>";
    struct nexus_browser_search_result results[NEXUS_BROWSER_SEARCH_MAX_RESULTS];
    size_t count = 0U;
    assert(nexus_browser_search_parse(
        html, sizeof(html) - 1U, results,
        NEXUS_BROWSER_SEARCH_MAX_RESULTS, &count));
    assert(count == 1U);
    assert(strcmp(results[0].title, "Safe page") == 0);
    assert(strcmp(results[0].url, "https://safe.example/") == 0);
}

static void test_bounds_and_missing_results(void) {
    static const uint8_t no_results[] =
        "<html><body>No matching result anchors</body></html>";
    static const uint8_t malformed[] =
        "<a class=\"result__a\" href=\"https://bad.example/\">truncated";
    struct nexus_browser_search_result results[NEXUS_BROWSER_SEARCH_MAX_RESULTS];
    size_t count = 99U;
    assert(!nexus_browser_search_parse(
        no_results, sizeof(no_results) - 1U, results,
        NEXUS_BROWSER_SEARCH_MAX_RESULTS, &count));
    assert(count == 0U);
    assert(!nexus_browser_search_parse(
        malformed, sizeof(malformed) - 1U, results,
        NEXUS_BROWSER_SEARCH_MAX_RESULTS, &count));
}

static void test_provider_challenge_detection(void) {
    static const uint8_t challenge[] =
        "<html><div id=\"anomaly-modal\">Unusual traffic detected</div></html>";
    static const uint8_t ordinary[] =
        "<html><div class=\"results\">No results</div></html>";
    assert(nexus_browser_search_is_challenge(
        challenge, sizeof(challenge) - 1U));
    assert(!nexus_browser_search_is_challenge(
        ordinary, sizeof(ordinary) - 1U));
}

static void test_rendered_line_to_result_mapping(void) {
    assert(nexus_browser_search_result_for_line(0U)==SIZE_MAX);
    assert(nexus_browser_search_result_for_line(1U)==SIZE_MAX);
    for(size_t line=2U;line<6U;++line)
        assert(nexus_browser_search_result_for_line(line)==0U);
    for(size_t line=6U;line<10U;++line)
        assert(nexus_browser_search_result_for_line(line)==1U);
    assert(nexus_browser_search_result_for_line(10U)==2U);
}

int main(void) {
    test_query_encoding();
    test_result_parse_and_redirect_decode();
    test_reject_unsafe_links_and_deduplicate();
    test_bounds_and_missing_results();
    test_provider_challenge_detection();
    test_rendered_line_to_result_mapping();
    return 0;
}
