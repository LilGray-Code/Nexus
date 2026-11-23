#include "window_manager.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_creation_focus_and_stacking(void) {
    nexus_window_id a, b, c, order;
    struct nexus_window_info info;
    assert(nexus_wm_init(1280U, 720U));
    assert(nexus_wm_count() == 0U);
    assert(!nexus_wm_create(1U, "bad", 0, 0, 0U, 10U, &a));
    assert(!nexus_wm_create(1U, "bad", 0, 0, 10U, 10U, NULL));
    assert(nexus_wm_create(1U, "Files", 40, 50, 600U, 420U, &a));
    assert(nexus_wm_create(2U, "Browser", 80, 70, 800U, 540U, &b));
    assert(nexus_wm_create(3U, "Terminal", 100, 90, 500U, 300U, &c));
    assert(nexus_wm_count() == 3U);
    assert(nexus_wm_focused() == c);
    assert(nexus_wm_z_order(0U, &order) && order == a);
    assert(nexus_wm_z_order(1U, &order) && order == b);
    assert(nexus_wm_z_order(2U, &order) && order == c);
    assert(nexus_wm_hit_test(110U, 100U) == c);
    assert(nexus_wm_focus(a));
    assert(nexus_wm_focused() == a);
    assert(nexus_wm_hit_test(110U, 100U) == a);
    assert(nexus_wm_get(a, &info));
    assert((info.flags & NEXUS_WINDOW_FOCUSED) != 0U);
    assert(strcmp(info.title, "Files") == 0);
}

static void test_geometry_and_bounds(void) {
    nexus_window_id id;
    struct nexus_window_info info;
    assert(nexus_wm_init(800U, 600U));
    assert(nexus_wm_create(4U, "Geometry", -20, 900, 1000U, 900U, &id));
    assert(nexus_wm_get(id, &info));
    assert(info.x == 0 && info.y == 0);
    assert(info.width == 800U && info.height == 600U);
    assert(nexus_wm_move(id, 700, 550));
    assert(nexus_wm_get(id, &info));
    assert(info.x == 640 && info.y == 504);
    assert(nexus_wm_resize(id, 320U, 200U));
    assert(nexus_wm_get(id, &info));
    assert(info.width == 320U && info.height == 200U);
    assert(info.x == 480 && info.y == 400);
    assert(nexus_wm_resize(id, 1U, 1U));
    assert(nexus_wm_get(id, &info));
    assert(info.width == 160U && info.height == 96U);
    assert(!nexus_wm_resize(id, 0U, 20U));
    assert(nexus_wm_move(id, -10, -10));
    assert(nexus_wm_get(id, &info));
    assert(info.x == 0 && info.y == 0);
}

static void test_minimize_restore_maximize(void) {
    nexus_window_id a, b;
    struct nexus_window_info info;
    assert(nexus_wm_init(1024U, 768U));
    assert(nexus_wm_create(1U, "A", 20, 30, 500U, 400U, &a));
    assert(nexus_wm_create(2U, "B", 100, 110, 600U, 450U, &b));
    assert(nexus_wm_minimize(b));
    assert(nexus_wm_focused() == a);
    assert(nexus_wm_hit_test(150U, 160U) == a);
    assert(nexus_wm_get(b, &info));
    assert((info.flags & NEXUS_WINDOW_MINIMIZED) != 0U);
    assert((info.flags & NEXUS_WINDOW_VISIBLE) == 0U);
    assert(!nexus_wm_focus(b));
    assert(nexus_wm_restore(b));
    assert(nexus_wm_focused() == b);
    assert(nexus_wm_maximize(b));
    assert(nexus_wm_get(b, &info));
    assert(info.x == 0 && info.y == 0);
    assert(info.width == 1024U && info.height == 768U);
    assert((info.flags & NEXUS_WINDOW_MAXIMIZED) != 0U);
    assert(!nexus_wm_move(b, 10, 10));
    assert(!nexus_wm_resize(b, 300U, 200U));
    assert(nexus_wm_unmaximize(b));
    assert(nexus_wm_get(b, &info));
    assert(info.x == 100 && info.y == 110);
    assert(info.width == 600U && info.height == 450U);
    assert((info.flags & NEXUS_WINDOW_MAXIMIZED) == 0U);
}

static void test_lifetime_capacity_and_stale_handles(void) {
    nexus_window_id ids[NEXUS_WM_MAX_WINDOWS];
    nexus_window_id replacement, ignored;
    assert(nexus_wm_init(1920U, 1080U));
    for (size_t i = 0U; i < NEXUS_WM_MAX_WINDOWS; ++i)
        assert(nexus_wm_create((uint32_t)i, "slot", 0, 0, 320U, 200U, &ids[i]));
    assert(!nexus_wm_create(99U, "overflow", 0, 0, 320U, 200U, &ignored));
    assert(nexus_wm_count() == NEXUS_WM_MAX_WINDOWS);
    assert(nexus_wm_destroy(ids[4]));
    assert(!nexus_wm_get(ids[4], &(struct nexus_window_info){0}));
    assert(nexus_wm_create(100U, "reused", 0, 0, 320U, 200U, &replacement));
    assert(replacement != ids[4]);
    assert(!nexus_wm_destroy(ids[4]));
    assert(nexus_wm_count() == NEXUS_WM_MAX_WINDOWS);
    assert(nexus_wm_destroy(replacement));
    assert(nexus_wm_count() == NEXUS_WM_MAX_WINDOWS - 1U);
    assert(nexus_wm_destroy(ids[0]));
    assert(nexus_wm_focused() != ids[0]);
}

static void test_small_display_and_titles(void) {
    nexus_window_id id;
    struct nexus_window_info info;
    assert(!nexus_wm_init(0U, 100U));
    assert(!nexus_wm_init(100U, 0U));
    assert(nexus_wm_init(96U, 64U));
    assert(nexus_wm_create(1U,
        "A deliberately long window title that must be truncated safely",
        20, 20, 200U, 200U, &id));
    assert(nexus_wm_get(id, &info));
    assert(info.x == 0 && info.y == 0);
    assert(info.width == 96U && info.height == 64U);
    assert(strlen(info.title) == NEXUS_WM_TITLE_MAX);
    assert(nexus_wm_set_title(id, "Tiny"));
    assert(nexus_wm_get(id, &info));
    assert(strcmp(info.title, "Tiny") == 0);
    assert(!nexus_wm_set_title(id, NULL));
    assert(nexus_wm_hit_test(96U, 0U) == 0U);
    assert(nexus_wm_hit_test(95U, 63U) == id);
}

int main(void) {
    test_creation_focus_and_stacking();
    test_geometry_and_bounds();
    test_minimize_restore_maximize();
    test_lifetime_capacity_and_stale_handles();
    test_small_display_and_titles();
    puts("Nexus window manager core tests passed");
    return 0;
}
