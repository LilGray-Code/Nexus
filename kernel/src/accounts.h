#ifndef NEXUS_ACCOUNTS_H
#define NEXUS_ACCOUNTS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NEXUS_ACCOUNT_MAX_USERS 8U
#define NEXUS_ACCOUNT_NAME_MAX 24U
#define NEXUS_ACCOUNT_PASSWORD_MAX 128U
#define NEXUS_ACCOUNT_SALT_BYTES 16U
#define NEXUS_ACCOUNT_VERIFIER_BYTES 32U
#define NEXUS_ACCOUNT_STARTUP_MAX 4U
#define NEXUS_ACCOUNT_STARTUP_NAME_MAX 24U
#ifndef NEXUS_ACCOUNT_PBKDF2_ITERATIONS
#define NEXUS_ACCOUNT_PBKDF2_ITERATIONS 600000U
#endif
#define NEXUS_ACCOUNT_MIN_PBKDF2_ITERATIONS 100000U
#define NEXUS_ACCOUNT_LOCKOUT_FAILURES 5U
#define NEXUS_ACCOUNT_LOCKOUT_SECONDS 30U

enum nexus_account_role {
    NEXUS_ACCOUNT_ROLE_STANDARD = 1,
    NEXUS_ACCOUNT_ROLE_ADMIN = 2
};

enum nexus_accounts_status {
    NEXUS_ACCOUNTS_UNINITIALIZED = 0,
    NEXUS_ACCOUNTS_SETUP_REQUIRED,
    NEXUS_ACCOUNTS_READY,
    NEXUS_ACCOUNTS_READ_ONLY,
    NEXUS_ACCOUNTS_STORAGE_UNAVAILABLE,
    NEXUS_ACCOUNTS_CORRUPT
};

enum nexus_account_load_result {
    NEXUS_ACCOUNT_LOAD_FOUND = 0,
    NEXUS_ACCOUNT_LOAD_MISSING,
    NEXUS_ACCOUNT_LOAD_ERROR
};

struct nexus_account_profile {
    uint8_t theme;
    uint8_t accent;
    uint8_t wallpaper;
    uint8_t ui_scale_percent;
    uint8_t startup_count;
    char startup_apps[NEXUS_ACCOUNT_STARTUP_MAX]
                     [NEXUS_ACCOUNT_STARTUP_NAME_MAX];
};

struct nexus_account_info {
    uint32_t uid;
    enum nexus_account_role role;
    bool active;
    char username[NEXUS_ACCOUNT_NAME_MAX];
};

struct nexus_account_session {
    bool logged_in;
    uint32_t uid;
    enum nexus_account_role role;
    uint64_t session_id;
    char username[NEXUS_ACCOUNT_NAME_MAX];
};

/*
 * Host-testable storage/entropy/clock boundary. Production initialization
 * binds these callbacks to NXFS, the hardware RNG and the kernel timer.
 */
struct nexus_account_backend {
    enum nexus_account_load_result (*load)(
        void *context, uint8_t *buffer, size_t capacity, size_t *length);
    bool (*store)(void *context, const uint8_t *buffer, size_t length);
    bool (*random)(void *context, void *buffer, size_t length);
    uint64_t (*ticks)(void *context);
    uint32_t ticks_per_second;
    bool persistent_writable;
    void *context;
};

void nexus_accounts_init(void);
bool nexus_accounts_init_with_backend(
    const struct nexus_account_backend *backend);
enum nexus_accounts_status nexus_accounts_get_status(void);
bool nexus_accounts_setup_required(void);
size_t nexus_accounts_count(void);
bool nexus_accounts_get_info(size_t index, struct nexus_account_info *info);

/* First administrator is provisioned only while the persistent database is empty. */
bool nexus_accounts_bootstrap_admin(const char *username,
                                    const char *password);
/* All subsequent account administration requires an authenticated administrator. */
bool nexus_accounts_create_user(const char *username, const char *password,
                                enum nexus_account_role role);
bool nexus_accounts_delete_user(uint32_t uid);
bool nexus_accounts_set_user_active(uint32_t uid, bool active);
bool nexus_accounts_change_own_password(const char *old_password,
                                        const char *new_password);
bool nexus_accounts_admin_reset_password(uint32_t uid,
                                         const char *new_password);

bool nexus_accounts_login(const char *username, const char *password);
void nexus_accounts_logout(void);
bool nexus_accounts_get_session(struct nexus_account_session *session);
bool nexus_accounts_is_logged_in(void);
bool nexus_accounts_is_admin(void);
uint32_t nexus_accounts_current_uid(void);
bool nexus_accounts_authorize_admin(void);

bool nexus_accounts_get_profile(uint32_t uid,
                                struct nexus_account_profile *profile);
bool nexus_accounts_set_profile(uint32_t uid,
                                const struct nexus_account_profile *profile);
bool nexus_accounts_add_startup_app(uint32_t uid, const char *application);
bool nexus_accounts_remove_startup_app(uint32_t uid,
                                       const char *application);

/* Exposed for RFC-vector verification and offline host tests. */
bool nexus_accounts_pbkdf2_sha256(
    const void *password, size_t password_length,
    const uint8_t salt[NEXUS_ACCOUNT_SALT_BYTES],
    uint32_t iterations, uint8_t output[NEXUS_ACCOUNT_VERIFIER_BYTES]);

#endif
