#include "browser_html.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void check(const char *html, const char *expected) {
    char output[512];
    size_t n = nexus_browser_html_to_text((const uint8_t *)html,
                                          strlen(html), output,
                                          sizeof(output));
    if (n != strlen(expected) || strcmp(output, expected) != 0) {
        fprintf(stderr, "HTML mismatch\\nexpected: [%s]\\nactual:   [%s]\\n",
                expected, output);
    }
    assert(n == strlen(expected));
    assert(strcmp(output, expected) == 0);
}


static void test_extract_absolute_source_links(void) {
    static const char html[] =
        "<a href='http://source.example/one'>One</a>"
        "<a class=\"primary\" href=\"https://source.example/two\">Two</a>"
        "<a href='/relative/path'>Relative</a>"
        "<a href='javascript:alert(1)'>Script</a>"
        "<a href='https://source.example/two'>Duplicate</a>"
        "<a href=\"https://source.example/query?a=1&amp;b=2\">Query</a>";
    char links[4][NEXUS_BROWSER_HTML_LINK_MAX] = {{0}};
    const size_t count = nexus_browser_html_extract_links(
        (const uint8_t *)html, sizeof(html) - 1U, links, 4U);
    assert(count == 3U);
    assert(strcmp(links[0], "http://source.example/one") == 0);
    assert(strcmp(links[1], "https://source.example/two") == 0);
    assert(strcmp(links[2], "https://source.example/query?a=1&b=2") == 0);
    assert(nexus_browser_html_extract_links(NULL, 0U, links, 4U) == 0U);
}

int main(void) {
    check("<html><head><title>Title</title><style>hidden css</style></head>"
          "<body><h1>Hello&nbsp;world</h1><script>bad()</script>"
          "<p>A &amp; B &lt; C &gt; D &quot;Q&quot; &#39;x&#39;</p>"
          "<!-- secret comment -->Done</body></html>",
          "Title\nHello world\nA & B < C > D \"Q\" 'x'\nDone");
    check("one\n  two\tthree<br>four", "one two three\nfour");
    check("<SCRIPT type='text/javascript'>x</SCRIPT>visible",
          "visible");
    check("<scripture>ordinary visible text</scripture>tail",
          "ordinary visible texttail");
    check("<styleguide>also visible</styleguide>tail",
          "also visibletail");
    check("<script>hidden </scripture> still hidden</script>shown",
          "shown");
    check("unknown &madeup; entity", "unknown &madeup; entity");
    check("<main><h1>Heading</h1><p>First paragraph.</p>"
          "<p>Second paragraph with <strong>emphasis</strong>.</p>"
          "<ul><li>One</li><li>Two</li></ul></main>",
          "Heading\nFirst paragraph.\nSecond paragraph with emphasis."
          "\n- One\n- Two");
    check("before<br>after<hr>last", "before\nafter\nlast");

    test_extract_absolute_source_links();

    char tiny[5];
    size_t n = nexus_browser_html_to_text((const uint8_t *)"abcdef", 6U,
                                          tiny, sizeof(tiny));
    assert(n == 4U && strcmp(tiny, "abcd") == 0);
    assert(nexus_browser_html_to_text(NULL, 10U, tiny, sizeof(tiny)) == 0U);
    assert(tiny[0] == '\0');
    puts("NEXUS BROWSER HTML TESTS PASSED");
    return 0;
}
