#include "desktop_services.h"

#include <assert.h>
#include <string.h>

static void test_workspaces_and_window_lifecycle(void) {
    nexus_desktop_services_init(1280U, 720U);
    const struct nexus_desktop_rect initial = {90U, 80U, 640U, 480U};
    assert(nexus_desktop_window_open(1U, 0U, "Files", &initial));
    assert(nexus_desktop_window_open(2U, 0U, "Browser", &initial));
    assert(nexus_desktop_window_open(3U, 1U, "Settings", &initial));
    assert(!nexus_desktop_window_open(0U, 0U, "Bad", &initial));
    assert(!nexus_desktop_window_open(4U, 4U, "Bad", &initial));
    assert(!nexus_desktop_window_open(1U, 0U, "Duplicate", &initial));

    struct nexus_desktop_window_info info;
    assert(nexus_desktop_window_get_by_id(1U, &info));
    assert(info.visible && !info.minimized);
    assert(nexus_desktop_window_get_by_id(3U, &info));
    assert(!info.visible);

    assert(nexus_desktop_window_focus(1U));
    assert(!nexus_desktop_window_focus(3U));
    assert(nexus_desktop_window_move(1U, -50, 900));
    assert(nexus_desktop_window_get_by_id(1U, &info));
    assert(info.bounds.x == 0U && info.bounds.y == 240U);
    assert(nexus_desktop_window_resize(1U, 10U, 10U));
    assert(nexus_desktop_window_get_by_id(1U, &info));
    assert(info.bounds.width == 180U && info.bounds.height == 120U);

    assert(nexus_desktop_window_snap(1U, NEXUS_DESKTOP_SNAP_LEFT));
    assert(nexus_desktop_window_get_by_id(1U, &info));
    assert(info.bounds.x == 0U && info.bounds.width == 640U);
    assert(info.bounds.height == 720U && info.snap == NEXUS_DESKTOP_SNAP_LEFT);
    assert(nexus_desktop_window_snap(1U, NEXUS_DESKTOP_SNAP_NONE));
    assert(nexus_desktop_window_get_by_id(1U, &info));
    assert(info.bounds.width == 180U && info.bounds.height == 120U);

    assert(nexus_desktop_window_snap(2U, NEXUS_DESKTOP_SNAP_RIGHT));
    assert(nexus_desktop_window_get_by_id(2U, &info));
    assert(info.bounds.x == 640U && info.bounds.width == 640U);
    assert(nexus_desktop_window_snap(2U, NEXUS_DESKTOP_SNAP_MAXIMIZED));
    assert(nexus_desktop_window_get_by_id(2U, &info));
    assert(info.maximized && info.bounds.width == 1280U);

    assert(nexus_desktop_workspace_activate(1U));
    assert(nexus_desktop_workspace_active() == 1U);
    assert(nexus_desktop_window_get_by_id(1U, &info));
    assert(!info.visible);
    assert(nexus_desktop_window_get_by_id(3U, &info));
    assert(info.visible);
    assert(nexus_desktop_window_restore(3U));
    assert(nexus_desktop_window_minimize(3U));
    assert(nexus_desktop_window_get_by_id(3U, &info));
    assert(!info.visible && info.minimized);
    assert(nexus_desktop_window_restore(3U));
    assert(nexus_desktop_window_get_by_id(3U, &info));
    assert(info.visible && info.focused);

    assert(nexus_desktop_workspace_next(1));
    assert(nexus_desktop_workspace_active() == 2U);
    assert(nexus_desktop_workspace_next(-1));
    assert(nexus_desktop_workspace_active() == 1U);
    assert(!nexus_desktop_workspace_next(0));
    assert(!nexus_desktop_workspace_activate(9U));
    assert(nexus_desktop_window_close(1U));
    assert(nexus_desktop_window_close(2U));
    assert(nexus_desktop_window_close(3U));
    assert(nexus_desktop_window_count() == 0U);
    assert(!nexus_desktop_window_close(3U));

    for (uint32_t id = 1U; id <= NEXUS_DESKTOP_WINDOW_MAX; ++id)
        assert(nexus_desktop_window_open(id, 0U, "Reusable", &initial));
    assert(!nexus_desktop_window_open(99U, 0U, "Overflow", &initial));
    assert(nexus_desktop_window_close(5U));
    assert(nexus_desktop_window_open(99U, 0U, "Reused slot", &initial));
}

static void test_notification_center_ring(void) {
    nexus_desktop_services_init(800U, 600U);
    assert(nexus_desktop_notification_push("Download complete",
        "file.iso saved", NEXUS_DESKTOP_NOTIFICATION_SUCCESS, 100U));
    assert(nexus_desktop_notification_push("Network",
        "Waiting for DHCP", NEXUS_DESKTOP_NOTIFICATION_WARNING, 200U));
    assert(nexus_desktop_notification_count() == 2U);
    assert(nexus_desktop_notification_unread_count() == 2U);

    struct nexus_desktop_notification note;
    assert(nexus_desktop_notification_get(0U, &note));
    assert(strcmp(note.title, "Network") == 0 && note.timestamp == 200U);
    assert(note.unread && note.level == NEXUS_DESKTOP_NOTIFICATION_WARNING);
    assert(nexus_desktop_notification_get(1U, &note));
    assert(strcmp(note.title, "Download complete") == 0);

    nexus_desktop_notification_mark_read();
    assert(nexus_desktop_notification_unread_count() == 0U);
    for (uint64_t i = 0U; i < NEXUS_DESKTOP_NOTIFICATION_MAX + 3U; ++i)
        assert(nexus_desktop_notification_push("Test", "bounded",
            NEXUS_DESKTOP_NOTIFICATION_INFO, 300U + i));
    assert(nexus_desktop_notification_count() ==
           NEXUS_DESKTOP_NOTIFICATION_MAX);
    assert(nexus_desktop_notification_get(0U, &note));
    assert(note.timestamp == 300U + NEXUS_DESKTOP_NOTIFICATION_MAX + 2U);
    assert(!nexus_desktop_notification_push("", "ok",
        NEXUS_DESKTOP_NOTIFICATION_INFO, 1U));
    assert(!nexus_desktop_notification_push("bad", "bad",
        (enum nexus_desktop_notification_level)99, 1U));
    nexus_desktop_notification_clear();
    assert(nexus_desktop_notification_count() == 0U);
}

static void test_clipboard_history(void) {
    nexus_desktop_services_init(800U, 600U);
    assert(!nexus_desktop_clipboard_set(NULL));
    assert(!nexus_desktop_clipboard_set(""));
    assert(nexus_desktop_clipboard_set("first copied value"));
    assert(nexus_desktop_clipboard_set("second copied value"));
    assert(nexus_desktop_clipboard_set("second copied value"));
    assert(nexus_desktop_clipboard_count() == 2U);

    char text[NEXUS_DESKTOP_CLIPBOARD_TEXT_MAX];
    size_t length = 0U;
    assert(nexus_desktop_clipboard_get(0U, text, sizeof(text), &length));
    assert(strcmp(text, "second copied value") == 0);
    assert(length == strlen(text));
    assert(!nexus_desktop_clipboard_get(0U, text, 4U, &length));
    for (size_t i = 0U; i < NEXUS_DESKTOP_CLIPBOARD_HISTORY_MAX + 2U; ++i) {
        char entry[32];
        (void)snprintf(entry, sizeof(entry), "entry-%u", (unsigned)i);
        assert(nexus_desktop_clipboard_set(entry));
    }
    assert(nexus_desktop_clipboard_count() ==
           NEXUS_DESKTOP_CLIPBOARD_HISTORY_MAX);
    assert(nexus_desktop_clipboard_get(0U, text, sizeof(text), &length));
    assert(strcmp(text, "entry-9") == 0);
    nexus_desktop_clipboard_clear();
    assert(nexus_desktop_clipboard_count() == 0U);
}

static void test_cross_app_file_and_setting_search(void) {
    const struct nexus_desktop_search_item items[] = {
        {NEXUS_DESKTOP_SEARCH_APP, "Settings", "Display and personalization"},
        {NEXUS_DESKTOP_SEARCH_FILE, "wallpaper.png", "Pictures folder"},
        {NEXUS_DESKTOP_SEARCH_APP, "Files", "Browse folders and downloads"},
        {NEXUS_DESKTOP_SEARCH_SETTING, "High contrast", "Accessibility"}
    };
    size_t indices[4] = {0U};
    size_t matched = 0U;
    assert(nexus_desktop_search(items, 4U, "dis", indices, 4U, &matched));
    assert(matched == 2U);
    assert(indices[0] == 0U); /* Prefix/title relevance ranks Settings first. */
    assert(indices[1] == 1U); /* detail search for Pictures? no "dis" title? */
    assert(nexus_desktop_search(items, 4U, "FILES",
                                indices, 4U, &matched));
    assert(matched == 1U && indices[0] == 2U);
    assert(nexus_desktop_search(items, 4U, "",
                                indices, 4U, &matched));
    assert(matched == 4U);
    assert(!nexus_desktop_search(items, 4U, "x", indices, 3U, &matched));
    assert(!nexus_desktop_search(items, 4U, NULL, indices, 4U, &matched));
    assert(nexus_desktop_search(NULL, 0U, "missing", NULL, 0U, &matched));
    assert(matched == 0U);
}

int main(void) {
    test_workspaces_and_window_lifecycle();
    test_notification_center_ring();
    test_clipboard_history();
    test_cross_app_file_and_setting_search();
    return 0;
}
