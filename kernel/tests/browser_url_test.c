#include "browser_url.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_http_defaults_and_path(void) {
    struct nexus_browser_url url;
    assert(nexus_browser_url_parse("http://example.com/docs/index.html?q=one#top", &url));
    assert(url.scheme == NEXUS_BROWSER_SCHEME_HTTP);
    assert(strcmp(url.host, "example.com") == 0);
    assert(url.port == 80U && !url.explicit_port);
    assert(strcmp(url.path, "/docs/index.html?q=one") == 0);
}

static void test_https_and_explicit_port(void) {
    struct nexus_browser_url url;
    assert(nexus_browser_url_parse("https://nexus.example:8443/search?q=x", &url));
    assert(url.scheme == NEXUS_BROWSER_SCHEME_HTTPS);
    assert(strcmp(url.host, "nexus.example") == 0);
    assert(url.port == 8443U && url.explicit_port);
    assert(strcmp(url.path, "/search?q=x") == 0);
}

static void test_empty_path_and_query(void) {
    struct nexus_browser_url url;
    assert(nexus_browser_url_parse("https://example.org", &url));
    assert(strcmp(url.path, "/") == 0);
    assert(nexus_browser_url_parse("https://example.org?x=1", &url));
    assert(strcmp(url.path, "/?x=1") == 0);
}

static void test_reject_unsafe_or_ambiguous_inputs(void) {
    struct nexus_browser_url url;
    assert(!nexus_browser_url_parse(NULL, &url));
    assert(!nexus_browser_url_parse("example.com", &url));
    assert(!nexus_browser_url_parse("ftp://example.com/", &url));
    assert(!nexus_browser_url_parse("http://user@example.com/", &url));
    assert(!nexus_browser_url_parse("http://[::1]/", &url));
    assert(!nexus_browser_url_parse("http://example.com:0/", &url));
    assert(!nexus_browser_url_parse("http://example.com:65536/", &url));
    assert(!nexus_browser_url_parse("http://example.com:abc/", &url));
    assert(!nexus_browser_url_parse("http://bad_host/", &url));
    assert(!nexus_browser_url_parse("http://example.com/a b", &url));
}

int main(void) {
    test_http_defaults_and_path();
    test_https_and_explicit_port();
    test_empty_path_and_query();
    test_reject_unsafe_or_ambiguous_inputs();
    puts("NEXUS BROWSER URL TESTS PASSED");
    return 0;
}
