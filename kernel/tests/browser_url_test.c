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
        "origin.test", 11U, "/page", 5U, true,
        "http://insecure.test/", 21U, output, sizeof(output),
        &output_length));
    assert(!nexus_browser_resolve_redirect(
        "origin.test", 11U, "/page", 5U, false,
        "javascript:alert(1)", 19U, output, sizeof(output),
        &output_length));
    assert(!nexus_browser_resolve_redirect(
        "origin.test", 11U, "/page", 5U, true,
        "/bad\001path", 9U, output, sizeof(output), &output_length));
    assert(!nexus_browser_resolve_redirect(
        "origin.test", 11U, "/page", 5U, true,
        "https://long.example/path/that/will/not/fit", 44U,
        output, sizeof(output), &output_length));
}

int main(void) {
    test_absolute_and_scheme_relative();
    test_relative_targets();
    test_security_and_bounds();
    return 0;
}
