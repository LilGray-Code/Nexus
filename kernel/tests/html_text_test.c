#include "html_text.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

static void expect_equal(const char *actual, size_t length,
                         const char *expected) {
    size_t i = 0U;
    while (expected[i] != '\0') {
        assert(i < length);
        assert(actual[i] == expected[i]);
        ++i;
    }
    assert(i == length);
    assert(actual[i] == '\0');
}

static void test_blocks_lists_and_inline_markup(void) {
    static const uint8_t html[] =
        "<!doctype html><html><body><h1>Nexus &amp; OS</h1>"
        "<p>Hello <strong>world</strong>.</p>"
        "<ul><li>One</li><li>Two</li></ul></body></html>";
    char output[256];
    struct nexus_html_text_result result;
    assert(nexus_html_to_text(html, sizeof(html) - 1U,
                              output, sizeof(output), &result));
    expect_equal(output, result.output_length,
                 "Nexus & OS\nHello world.\n- One\n- Two");
    assert(!result.truncated);
}

static void test_entities_and_whitespace(void) {
    static const uint8_t html[] =
        "A&nbsp;B &#65; &#x41; &copy; &unknown; "
        "line\n\t two";
    char output[128];
    struct nexus_html_text_result result;
    assert(nexus_html_to_text(html, sizeof(html) - 1U,
                              output, sizeof(output), &result));
    expect_equal(output, result.output_length,
                 "A B A A ? &unknown; line two");
}

static void test_comments_and_active_content_are_omitted(void) {
    static const uint8_t html[] =
        "<p>Visible</p><!-- private comment -->"
        "<script>document.write('unsafe')</script>"
        "<style>.secret { display:none }</style><p>Safe</p>";
    char output[128];
    struct nexus_html_text_result result;
    assert(nexus_html_to_text(html, sizeof(html) - 1U,
                              output, sizeof(output), &result));
    expect_equal(output, result.output_length, "Visible\nSafe");
}

static void test_table_cells_and_horizontal_rule(void) {
    static const uint8_t html[] =
        "<table><tr><th>Name</th><th>State</th></tr>"
        "<tr><td>Nexus</td><td>Ready</td></tr></table><hr>End";
    char output[128];
    struct nexus_html_text_result result;
    assert(nexus_html_to_text(html, sizeof(html) - 1U,
                              output, sizeof(output), &result));
    expect_equal(output, result.output_length,
                 "Name State\nNexus Ready\n---\nEnd");
}

static void test_bounded_output_and_malformed_input(void) {
    static const uint8_t html[] = "<p>this text is longer than the buffer</p>";
    char output[8];
    struct nexus_html_text_result result;
    assert(nexus_html_to_text(html, sizeof(html) - 1U,
                              output, sizeof(output), &result));
    assert(result.truncated);
    assert(result.output_length == sizeof(output) - 1U);
    assert(output[sizeof(output) - 1U] == '\0');

    static const uint8_t malformed[] = "<p>unfinished";
    char malformed_output[64];
    assert(nexus_html_to_text(malformed, sizeof(malformed) - 1U,
                              malformed_output, sizeof(malformed_output),
                              &result));
    assert(result.malformed);
    expect_equal(malformed_output, result.output_length, "unfinished");
}

static void test_invalid_arguments(void) {
    static const uint8_t html[] = "text";
    char output[16];
    struct nexus_html_text_result result;
    assert(!nexus_html_to_text(NULL, 1U, output, sizeof(output), &result));
    assert(!nexus_html_to_text(html, sizeof(html) - 1U, NULL, 0U, &result));
    assert(!nexus_html_to_text(html, sizeof(html) - 1U,
                               output, sizeof(output), NULL));
    assert(!nexus_html_to_text(html, sizeof(html) - 1U,
                               output, 0U, &result));
}

int main(void) {
    test_blocks_lists_and_inline_markup();
    test_entities_and_whitespace();
    test_comments_and_active_content_are_omitted();
    test_table_cells_and_horizontal_rule();
    test_bounded_output_and_malformed_input();
    test_invalid_arguments();
    return 0;
}
