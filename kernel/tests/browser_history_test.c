#include "browser_history.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static void expect_url(struct nexus_browser_history *history,
                       bool forward, const char *expected) {
    char actual[NEXUS_BROWSER_HISTORY_URL_MAX];
    const bool moved = forward
        ? nexus_browser_history_forward(history, actual, sizeof(actual))
        : nexus_browser_history_back(history, actual, sizeof(actual));
    assert(moved);
    assert(strcmp(actual, expected) == 0);
}

static void test_basic_navigation_and_branching(void) {
    struct nexus_browser_history history;
    nexus_browser_history_init(&history);
    assert(!nexus_browser_history_can_back(&history));
    assert(!nexus_browser_history_can_forward(&history));

    assert(nexus_browser_history_push(&history, "http://one.test/"));
    assert(!nexus_browser_history_can_back(&history));
    assert(nexus_browser_history_push(&history, "http://two.test/page"));
    assert(nexus_browser_history_can_back(&history));
    expect_url(&history, false, "http://one.test/");
    assert(nexus_browser_history_can_forward(&history));
    expect_url(&history, true, "http://two.test/page");
    assert(!nexus_browser_history_can_forward(&history));

    expect_url(&history, false, "http://one.test/");
    assert(nexus_browser_history_push(&history, "http://three.test/"));
    assert(!nexus_browser_history_can_forward(&history));
    expect_url(&history, false, "http://one.test/");
    expect_url(&history, true, "http://three.test/");
    assert(!nexus_browser_history_can_forward(&history));
}

static void test_duplicate_and_capacity_eviction(void) {
    struct nexus_browser_history history;
    nexus_browser_history_init(&history);
    assert(nexus_browser_history_push(&history, "http://page00.test/"));
    assert(nexus_browser_history_push(&history, "http://page00.test/"));
    for (size_t i = 1U; i <= NEXUS_BROWSER_HISTORY_CAPACITY; ++i) {
        char url[NEXUS_BROWSER_HISTORY_URL_MAX];
        const int written = snprintf(url, sizeof(url),
                                     "http://page%02u.test/", (unsigned)i);
        assert(written > 0 && (size_t)written < sizeof(url));
        assert(nexus_browser_history_push(&history, url));
    }

    assert(nexus_browser_history_can_back(&history));
    for (size_t i = 0U; i < NEXUS_BROWSER_HISTORY_CAPACITY - 1U; ++i) {
        assert(nexus_browser_history_can_back(&history));
        char actual[NEXUS_BROWSER_HISTORY_URL_MAX];
        assert(nexus_browser_history_back(&history, actual, sizeof(actual)));
    }
    assert(!nexus_browser_history_can_back(&history));
    expect_url(&history, true, "http://page02.test/");
}

static void test_invalid_input_and_small_destination(void) {
    struct nexus_browser_history history;
    nexus_browser_history_init(&history);
    assert(!nexus_browser_history_push(&history, NULL));
    assert(!nexus_browser_history_push(&history, ""));
    assert(!nexus_browser_history_push(&history, "http://bad.test/\n"));

    char too_long[NEXUS_BROWSER_HISTORY_URL_MAX + 1U];
    memset(too_long, 'a', sizeof(too_long));
    too_long[sizeof(too_long) - 1U] = '\0';
    assert(!nexus_browser_history_push(&history, too_long));

    assert(nexus_browser_history_push(&history, "http://one.test/"));
    assert(nexus_browser_history_push(&history, "http://two.test/"));
    char short_buffer[4];
    assert(!nexus_browser_history_back(&history, short_buffer,
                                       sizeof(short_buffer)));
    assert(nexus_browser_history_can_back(&history));
    expect_url(&history, false, "http://one.test/");
}

int main(void) {
    test_basic_navigation_and_branching();
    test_duplicate_and_capacity_eviction();
    test_invalid_input_and_small_destination();
    return 0;
}
