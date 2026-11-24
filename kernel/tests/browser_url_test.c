#include "browser_url.h"

#include <assert.h>
#include <string.h>

static void expect_redirect(const char *host, const char *path,
                            bool current_https, const char *location,
                            const char *expected) {
    char output[128];
    size_t output_length = 0U;
    assert(nexus_browser_resolve_redirect(
        host, strlen(host), path, strlen(path), current_https,
        location, strlen(location), output, sizeof(output), &output_length));
    assert(output_length == strlen(expected));
    assert(strcmp(output, expected) == 0);
}

static void test_absolute_and_scheme_relative(void) {
    expect_redirect("origin.test", "/a/page", true,
                    "https://other.test/new",
                    "https://other.test/new");
    expect_redirect("origin.test", "/a/page", false,
                    "HTTPS://other.test/new",
                    "https://other.test/new");
    expect_redirect("origin.test", "/a/page", true,
                    "//cdn.test/asset",
                    "https://cdn.test/asset");
    expect_redirect("origin.test", "/a/page", false,
                    "//cdn.test/asset",
                    "http://cdn.test/asset");
}

static void test_relative_targets(void) {
    expect_redirect("origin.test", "/a/page", true,
                    "/root",
                    "https://origin.test/root");
    expect_redirect("origin.test", "/a/page?old=1", true,
                    "?new=2",
                    "https://origin.test/a/page?new=2");
    expect_redirect("origin.test", "/a/page", true,
                    "next",
                    "https://origin.test/a/next");
    expect_redirect("origin.test", "/a/b/page", true,
                    "../next",
                    "https://origin.test/a/b/../next");
}

static void test_security_and_bounds(void) {
    char output[32];
    size_t output_length = 0U;
    assert(!nexus_browser_resolve_redirect(
        "origin.test", strlen("origin.test"), "/page", 5U, true,
        "http://insecure.test/", strlen("http://insecure.test/"), output, sizeof(output),
        &output_length));
    assert(!nexus_browser_resolve_redirect(
        "origin.test", strlen("origin.test"), "/page", 5U, false,
        "javascript:alert(1)", strlen("javascript:alert(1)"), output, sizeof(output),
        &output_length));
    assert(!nexus_browser_resolve_redirect(
        "origin.test", strlen("origin.test"), "/page", 5U, true,
        "/bad\001path", strlen("/bad\001path"), output, sizeof(output), &output_length));
    assert(!nexus_browser_resolve_redirect(
        "origin.test", strlen("origin.test"), "/page", 5U, true,
        "https://long.example/path/that/will/not/fit",
        strlen("https://long.example/path/that/will/not/fit"),
        output, sizeof(output), &output_length));
}

static void test_authority_ports(void) {
    struct nexus_browser_authority parsed = {0};
    assert(nexus_browser_parse_authority(
        "example.test", strlen("example.test"), 443U, &parsed));
    assert(strcmp(parsed.hostname, "example.test") == 0);
    assert(parsed.port == 443U && !parsed.explicit_port);

    assert(nexus_browser_parse_authority(
        "example.test:8443", strlen("example.test:8443"), 443U, &parsed));
    assert(strcmp(parsed.hostname, "example.test") == 0);
    assert(parsed.port == 8443U && parsed.explicit_port);

    assert(nexus_browser_parse_authority(
        "192.0.2.10:8080", strlen("192.0.2.10:8080"), 80U, &parsed));
    assert(strcmp(parsed.hostname, "192.0.2.10") == 0);
    assert(parsed.port == 8080U && parsed.explicit_port);

    assert(!nexus_browser_parse_authority(
        "example.test:", strlen("example.test:"), 443U, &parsed));
    assert(!nexus_browser_parse_authority(
        "example.test:0", strlen("example.test:0"), 443U, &parsed));
    assert(!nexus_browser_parse_authority(
        "example.test:65536", strlen("example.test:65536"), 443U, &parsed));
    assert(!nexus_browser_parse_authority(
        "example.test:abc", strlen("example.test:abc"), 443U, &parsed));
    assert(!nexus_browser_parse_authority(
        "example.test:80:90", strlen("example.test:80:90"), 443U, &parsed));
    assert(!nexus_browser_parse_authority(
        "[2001:db8::1]", strlen("[2001:db8::1]"), 443U, &parsed));
    assert(!nexus_browser_parse_authority(
        "user@example.test", strlen("user@example.test"), 443U, &parsed));
}

int main(void) {
    test_absolute_and_scheme_relative();
    test_relative_targets();
    test_security_and_bounds();
    test_authority_ports();
    return 0;
}
