#include "accounts.h"
#include "security.h"
#include "nxfs.h"
#include "random.h"
#include "timer.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

struct mock_store {
    uint8_t bytes[2048];
    size_t length;
    bool present;
    bool writable;
    uint64_t ticks;
    uint64_t random_counter;
};

static enum nexus_account_load_result mock_load(
    void *context, uint8_t *buffer, size_t capacity, size_t *length) {
    struct mock_store *store = context;
    if (!store->present) return NEXUS_ACCOUNT_LOAD_MISSING;
    if (store->length > capacity) return NEXUS_ACCOUNT_LOAD_ERROR;
    memcpy(buffer, store->bytes, store->length);
    *length = store->length;
    return NEXUS_ACCOUNT_LOAD_FOUND;
}

static bool mock_store_write(void *context, const uint8_t *buffer,
                             size_t length) {
    struct mock_store *store = context;
    if (!store->writable || length > sizeof(store->bytes)) return false;
    memcpy(store->bytes, buffer, length);
    store->length = length;
    store->present = true;
    return true;
}

static bool mock_random(void *context, void *buffer, size_t length) {
    struct mock_store *store = context;
    uint8_t *bytes = buffer;
    for (size_t i = 0U; i < length; ++i) {
        ++store->random_counter;
        bytes[i] = (uint8_t)(store->random_counter * 73U + 19U);
    }
    return true;
}

static uint64_t mock_ticks(void *context) {
    return ((struct mock_store *)context)->ticks;
}

static struct nexus_account_backend make_backend(struct mock_store *store) {
    return (struct nexus_account_backend){
        .load = mock_load,
        .store = mock_store_write,
        .random = mock_random,
        .ticks = mock_ticks,
        .ticks_per_second = 100U,
        .persistent_writable = store->writable,
        .context = store
    };
}

static void test_pbkdf2_vector(void) {
    static const uint8_t salt[16] = {
        0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U,
        8U, 9U, 10U, 11U, 12U, 13U, 14U, 15U
    };
    static const uint8_t expected[32] = {
        0xA0U,0xFFU,0xD3U,0x42U,0x92U,0xE4U,0xE7U,0xD5U,
        0x19U,0xEEU,0x0FU,0x25U,0x12U,0x4EU,0x0CU,0x5BU,
        0x94U,0x81U,0xD8U,0x8B,0xE4U,0x0FU,0x46U,0x62U,
        0xEBU,0x6CU,0xB4U,0xB9U,0xE4U,0x3FU,0x9FU,0x41U
    };
    uint8_t actual[32];
    assert(nexus_accounts_pbkdf2_sha256(
        "password", 8U, salt, 2U, actual));
    assert(memcmp(actual, expected, sizeof(actual)) == 0);
}

int main(void) {
    test_pbkdf2_vector();
    nexus_security_init();

    struct mock_store store = {.writable = true};
    struct nexus_account_backend backend = make_backend(&store);
    assert(nexus_accounts_init_with_backend(&backend));
    assert(nexus_accounts_get_status() == NEXUS_ACCOUNTS_SETUP_REQUIRED);
    assert(nexus_accounts_setup_required());
    assert(nexus_accounts_count() == 0U);
    assert(!nexus_accounts_bootstrap_admin("admin", "short"));
    assert(!nexus_accounts_bootstrap_admin("bad/name",
                                            "StrongPassword2026!"));
    assert(nexus_accounts_bootstrap_admin(
        "admin", "StrongAdminPassword2026!"));
    assert(nexus_accounts_get_status() == NEXUS_ACCOUNTS_READY);
    assert(!nexus_accounts_setup_required());
    assert(store.present && store.length > 100U);
    assert(nexus_accounts_count() == 1U);
    assert(!nexus_accounts_create_user(
        "guest", "StrongGuestPassword2026!",
        NEXUS_ACCOUNT_ROLE_STANDARD)); /* not logged in yet */

    /* Unknown users and invalid passwords fail without creating a session. */
    assert(!nexus_accounts_login("missing", "StrongGuestPassword2026!"));
    assert(!nexus_accounts_login("admin", "incorrect-password"));
    assert(!nexus_accounts_login("admin", "incorrect-password"));
    assert(!nexus_accounts_login("admin", "incorrect-password"));
    assert(!nexus_accounts_login("admin", "incorrect-password"));
    assert(!nexus_accounts_login("admin", "incorrect-password"));
    assert(!nexus_accounts_login("admin", "StrongAdminPassword2026!"));
    store.ticks += 31U * 100U;
    assert(nexus_accounts_login("admin", "StrongAdminPassword2026!"));
    assert(nexus_accounts_is_logged_in());
    assert(nexus_accounts_is_admin());
    assert(nexus_accounts_authorize_admin());

    struct nexus_account_session session;
    assert(nexus_accounts_get_session(&session));
    assert(session.logged_in && session.uid == 1U);
    assert(session.role == NEXUS_ACCOUNT_ROLE_ADMIN);
    assert(session.session_id != 0U);
    assert(strcmp(session.username, "admin") == 0);

    assert(nexus_accounts_create_user(
        "guest", "StrongGuestPassword2026!",
        NEXUS_ACCOUNT_ROLE_STANDARD));
    assert(nexus_accounts_count() == 2U);
    struct nexus_account_info info;
    assert(nexus_accounts_get_info(1U, &info));
    assert(info.uid == 2U && info.active);
    assert(info.role == NEXUS_ACCOUNT_ROLE_STANDARD);
    assert(strcmp(info.username, "guest") == 0);

    struct nexus_account_profile profile;
    assert(nexus_accounts_get_profile(2U, &profile));
    assert(profile.ui_scale_percent == 100U);
    assert(profile.startup_count == 0U);
    assert(nexus_accounts_add_startup_app(2U, "NOTES.ELF"));
    assert(nexus_accounts_add_startup_app(2U, "BROWSER.ELF"));
    assert(nexus_accounts_add_startup_app(2U, "NOTES.ELF"));
    assert(!nexus_accounts_add_startup_app(2U, "../BAD"));
    assert(nexus_accounts_get_profile(2U, &profile));
    assert(profile.startup_count == 2U);
    assert(strcmp(profile.startup_apps[0], "NOTES.ELF") == 0);
    profile.theme = 1U;
    profile.accent = 3U;
    profile.wallpaper = 2U;
    profile.ui_scale_percent = 125U;
    assert(nexus_accounts_set_profile(2U, &profile));
    assert(nexus_accounts_remove_startup_app(2U, "NOTES.ELF"));
    assert(nexus_accounts_get_profile(2U, &profile));
    assert(profile.startup_count == 1U);
    assert(strcmp(profile.startup_apps[0], "BROWSER.ELF") == 0);

    /* Disabling an account survives reload, but it cannot authenticate. */
    assert(nexus_accounts_set_user_active(2U, false));
    assert(!nexus_accounts_login("guest", "StrongGuestPassword2026!"));
    assert(nexus_accounts_set_user_active(2U, true));

    /* An administrator cannot remove or disable their own final admin account. */
    assert(!nexus_accounts_delete_user(1U));
    assert(!nexus_accounts_set_user_active(1U, false));
    assert(nexus_accounts_admin_reset_password(2U,
                                                "ResetGuestPassword2026!"));

    nexus_accounts_logout();
    assert(!nexus_accounts_is_logged_in());
    assert(!nexus_accounts_is_admin());
    assert(!nexus_accounts_authorize_admin());
    assert(!nexus_accounts_create_user(
        "other", "StrongOtherPassword2026!",
        NEXUS_ACCOUNT_ROLE_STANDARD));
    assert(nexus_accounts_login("guest", "ResetGuestPassword2026!"));
    assert(!nexus_accounts_is_admin());
    assert(!nexus_accounts_create_user(
        "other", "StrongOtherPassword2026!",
        NEXUS_ACCOUNT_ROLE_STANDARD));
    assert(!nexus_accounts_admin_reset_password(1U,
                                                "ResetAdminPassword2026!"));
    assert(nexus_accounts_change_own_password(
        "ResetGuestPassword2026!", "NewGuestPassword2026!"));
    nexus_accounts_logout();
    assert(!nexus_accounts_login("guest", "ResetGuestPassword2026!"));
    store.ticks += 31U * 100U;
    assert(nexus_accounts_login("guest", "NewGuestPassword2026!"));

    /* Reload validates the persisted database and retains both profiles. */
    nexus_accounts_logout();
    backend = make_backend(&store);
    assert(nexus_accounts_init_with_backend(&backend));
    assert(nexus_accounts_get_status() == NEXUS_ACCOUNTS_READY);
    assert(nexus_accounts_count() == 2U);
    assert(nexus_accounts_get_info(1U, &info));
    assert(info.uid == 2U && strcmp(info.username, "guest") == 0);
    assert(nexus_accounts_login("guest", "NewGuestPassword2026!"));
    assert(nexus_accounts_get_profile(2U, &profile));
    assert(profile.theme == 1U && profile.accent == 3U &&
           profile.wallpaper == 2U && profile.ui_scale_percent == 125U);

    /* A standard user may remove their own startup entries and update profile. */
    assert(nexus_accounts_remove_startup_app(2U, "BROWSER.ELF"));
    assert(!nexus_accounts_get_profile(1U, &profile));

    /* Read-only storage permits authentication but refuses account writes. */
    nexus_accounts_logout();
    store.writable = false;
    backend = make_backend(&store);
    assert(nexus_accounts_init_with_backend(&backend));
    assert(nexus_accounts_get_status() == NEXUS_ACCOUNTS_READ_ONLY);
    assert(nexus_accounts_login("admin", "StrongAdminPassword2026!"));
    assert(!nexus_accounts_create_user(
        "read-only", "ReadOnlyAccountPassword2026!",
        NEXUS_ACCOUNT_ROLE_STANDARD));

    /* Corrupt persistent bytes are not mistaken for an empty database. */
    store.writable = true;
    store.bytes[store.length - 1U] ^= 0x80U;
    nexus_accounts_logout();
    backend = make_backend(&store);
    assert(!nexus_accounts_init_with_backend(&backend));
    assert(nexus_accounts_get_status() == NEXUS_ACCOUNTS_CORRUPT);
    assert(!nexus_accounts_setup_required());
    assert(nexus_accounts_count() == 0U);

    puts("NEXUS ACCOUNT, PASSWORD AND SESSION TESTS PASSED");
    return 0;
}

/* Production-only backend references, unused by these injected host tests. */
bool nxfs_is_mounted(void) { return false; }
bool nxfs_is_writable(void) { return false; }
size_t nxfs_entry_count(void) { return 0U; }
const struct nexus_nxfs_entry *nxfs_entry_at(size_t index) {
    (void)index; return NULL;
}
bool nxfs_read_file(const struct nexus_nxfs_entry *entry, uint32_t offset,
                    uint32_t length, void *buffer) {
    (void)entry; (void)offset; (void)length; (void)buffer; return false;
}
bool nxfs_write_file(const char *name, const void *data, size_t length) {
    (void)name; (void)data; (void)length; return false;
}
bool nexus_rng_fill(void *output, size_t length) {
    (void)output; (void)length; return false;
}
uint64_t timer_ticks(void) { return 0U; }
