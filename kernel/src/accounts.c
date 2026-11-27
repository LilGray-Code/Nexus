#include "accounts.h"

#include "nxfs.h"
#include "random.h"
#include "security.h"
#include "sha256.h"
#include "timer.h"

#include <stddef.h>
#include <stdint.h>

#define ACCOUNT_DB_MAGIC "NXACCT01"
#define ACCOUNT_DB_VERSION 1U
#define ACCOUNT_DB_HEADER_BYTES 28U
#define ACCOUNT_DB_RECORD_BYTES 183U
#define ACCOUNT_DB_TRAILER_BYTES 4U
#define ACCOUNT_DB_MAX_BYTES (ACCOUNT_DB_HEADER_BYTES + \
    NEXUS_ACCOUNT_MAX_USERS * ACCOUNT_DB_RECORD_BYTES + \
    ACCOUNT_DB_TRAILER_BYTES)
#define ACCOUNT_DB_MAX_ITERATIONS 2000000U
#define ACCOUNT_SESSION_ID_BYTES 8U

struct account_record {
    uint32_t uid;
    enum nexus_account_role role;
    bool active;
    char username[NEXUS_ACCOUNT_NAME_MAX];
    uint8_t salt[NEXUS_ACCOUNT_SALT_BYTES];
    uint32_t iterations;
    uint8_t verifier[NEXUS_ACCOUNT_VERIFIER_BYTES];
    struct nexus_account_profile profile;
};

struct account_database {
    struct account_record users[NEXUS_ACCOUNT_MAX_USERS];
    size_t user_count;
    uint32_t next_uid;
    uint64_t generation;
};

static struct account_database database;
static struct nexus_account_backend active_backend;
static enum nexus_accounts_status accounts_status =
    NEXUS_ACCOUNTS_UNINITIALIZED;
static struct nexus_account_session active_session;
static uint8_t login_failures[NEXUS_ACCOUNT_MAX_USERS];
static uint64_t lockout_until[NEXUS_ACCOUNT_MAX_USERS];
static bool backend_ready;

static void secure_zero(void *memory, size_t length) {
    volatile uint8_t *bytes = (volatile uint8_t *)memory;
    for (size_t i = 0U; i < length; ++i) bytes[i] = 0U;
}

static bool bytes_equal_constant_time(const uint8_t *left,
                                      const uint8_t *right,
                                      size_t length) {
    if (left == NULL || right == NULL) return false;
    uint8_t difference = 0U;
    for (size_t i = 0U; i < length; ++i)
        difference |= (uint8_t)(left[i] ^ right[i]);
    return difference == 0U;
}

static uint32_t read_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static uint64_t read_le64(const uint8_t *bytes) {
    return (uint64_t)read_le32(bytes) |
           ((uint64_t)read_le32(bytes + 4U) << 32);
}

static void write_le32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static void write_le64(uint8_t *bytes, uint64_t value) {
    write_le32(bytes, (uint32_t)value);
    write_le32(bytes + 4U, (uint32_t)(value >> 32));
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *bytes,
                             size_t length) {
    for (size_t i = 0U; i < length; ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0U; bit < 8U; ++bit) {
            const uint32_t mask = (uint32_t)-(int32_t)(crc & 1U);
            crc = (crc >> 1) ^ (0xEDB88320U & mask);
        }
    }
    return crc;
}

static uint32_t crc32_bytes(const uint8_t *bytes, size_t length) {
    return ~crc32_update(0xFFFFFFFFU, bytes, length);
}

static bool valid_username(const char *username) {
    if (username == NULL || username[0] == '\0') return false;
    size_t length = 0U;
    while (username[length] != '\0') {
        const char c = username[length];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-'))
            return false;
        if (++length >= NEXUS_ACCOUNT_NAME_MAX) return false;
    }
    return length >= 1U;
}

static bool valid_startup_name(const char *name) {
    if (name == NULL || name[0] == '\0') return false;
    size_t length = 0U;
    while (name[length] != '\0') {
        const char c = name[length];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.'))
            return false;
        if (++length >= NEXUS_ACCOUNT_STARTUP_NAME_MAX) return false;
    }
    return true;
}

static bool valid_password(const char *password, size_t *length_out) {
    if (password == NULL || length_out == NULL) return false;
    size_t length = 0U;
    while (password[length] != '\0') {
        const unsigned char c = (unsigned char)password[length];
        if (c < 0x20U || c > 0x7EU ||
            length >= NEXUS_ACCOUNT_PASSWORD_MAX) return false;
        ++length;
    }
    if (length < 12U || length > NEXUS_ACCOUNT_PASSWORD_MAX) return false;
    *length_out = length;
    return true;
}

static void copy_text(char *destination, const char *source, size_t capacity) {
    size_t i = 0U;
    if (capacity == 0U) return;
    if (source != NULL) {
        while (i + 1U < capacity && source[i] != '\0') {
            destination[i] = source[i];
            ++i;
        }
    }
    destination[i] = '\0';
    while (++i < capacity) destination[i] = '\0';
}

static void copy_bytes(uint8_t *destination, const uint8_t *source,
                       size_t length) {
    for (size_t i = 0U; i < length; ++i) destination[i] = source[i];
}

static bool profile_valid(const struct nexus_account_profile *profile) {
    if (profile == NULL || profile->theme > 1U || profile->accent >= 8U ||
        profile->wallpaper >= 4U ||
        (profile->ui_scale_percent != 75U &&
         profile->ui_scale_percent != 90U &&
         profile->ui_scale_percent != 100U &&
         profile->ui_scale_percent != 110U &&
         profile->ui_scale_percent != 120U &&
         profile->ui_scale_percent != 125U &&
         profile->ui_scale_percent != 150U) ||
        profile->startup_count > NEXUS_ACCOUNT_STARTUP_MAX) return false;

    for (size_t i = 0U; i < profile->startup_count; ++i) {
        if (!valid_startup_name(profile->startup_apps[i])) return false;
        for (size_t j = 0U; j < i; ++j) {
            size_t k = 0U;
            while (profile->startup_apps[i][k] != '\0' &&
                   profile->startup_apps[j][k] != '\0' &&
                   profile->startup_apps[i][k] == profile->startup_apps[j][k])
                ++k;
            if (profile->startup_apps[i][k] == '\0' &&
                profile->startup_apps[j][k] == '\0') return false;
        }
    }
    return true;
}

bool nexus_accounts_pbkdf2_sha256(
    const void *password, size_t password_length,
    const uint8_t salt[NEXUS_ACCOUNT_SALT_BYTES],
    uint32_t iterations, uint8_t output[NEXUS_ACCOUNT_VERIFIER_BYTES]) {
    if (password == NULL || password_length == 0U || salt == NULL ||
        output == NULL || iterations == 0U ||
        iterations > ACCOUNT_DB_MAX_ITERATIONS) return false;

    uint8_t block_input[NEXUS_ACCOUNT_SALT_BYTES + 4U];
    uint8_t u[NEXUS_ACCOUNT_VERIFIER_BYTES];
    uint8_t accumulator[NEXUS_ACCOUNT_VERIFIER_BYTES];
    copy_bytes(block_input, salt, NEXUS_ACCOUNT_SALT_BYTES);
    block_input[16] = 0U;
    block_input[17] = 0U;
    block_input[18] = 0U;
    block_input[19] = 1U;

    nexus_hmac_sha256(password, password_length, block_input,
                      sizeof(block_input), u);
    copy_bytes(accumulator, u, sizeof(accumulator));
    for (uint32_t round = 1U; round < iterations; ++round) {
        nexus_hmac_sha256(password, password_length, u, sizeof(u), u);
        for (size_t i = 0U; i < sizeof(accumulator); ++i)
            accumulator[i] ^= u[i];
    }
    copy_bytes(output, accumulator, sizeof(accumulator));
    secure_zero(block_input, sizeof(block_input));
    secure_zero(u, sizeof(u));
    secure_zero(accumulator, sizeof(accumulator));
    return true;
}

static bool derive_password_verifier(const char *password,
                                     const uint8_t salt[NEXUS_ACCOUNT_SALT_BYTES],
                                     uint32_t iterations,
                                     uint8_t verifier[NEXUS_ACCOUNT_VERIFIER_BYTES]) {
    size_t password_length = 0U;
    if (!valid_password(password, &password_length)) return false;
    return nexus_accounts_pbkdf2_sha256(password, password_length, salt,
                                        iterations, verifier);
}

static void record_default_profile(struct nexus_account_profile *profile) {
    *profile = (struct nexus_account_profile){
        .theme = 0U,
        .accent = 0U,
        .wallpaper = 0U,
        .ui_scale_percent = 100U,
        .startup_count = 0U
    };
}

static size_t find_user_name(const char *username) {
    for (size_t i = 0U; i < database.user_count; ++i) {
        size_t j = 0U;
        while (database.users[i].username[j] != '\0' &&
               username[j] != '\0' &&
               database.users[i].username[j] == username[j]) ++j;
        if (database.users[i].username[j] == '\0' && username[j] == '\0')
            return i;
    }
    return database.user_count;
}

static size_t find_user_uid(uint32_t uid) {
    for (size_t i = 0U; i < database.user_count; ++i)
        if (database.users[i].uid == uid) return i;
    return database.user_count;
}

static size_t count_active_admins(void) {
    size_t count = 0U;
    for (size_t i = 0U; i < database.user_count; ++i) {
        if (database.users[i].active &&
            database.users[i].role == NEXUS_ACCOUNT_ROLE_ADMIN) ++count;
    }
    return count;
}

static bool encode_database(uint8_t output[ACCOUNT_DB_MAX_BYTES],
                            size_t *output_length, uint64_t generation) {
    if (output == NULL || output_length == NULL ||
        database.user_count > NEXUS_ACCOUNT_MAX_USERS ||
        database.next_uid == 0U) return false;

    for (size_t i = 0U; i < ACCOUNT_DB_MAX_BYTES; ++i) output[i] = 0U;
    copy_bytes(output, (const uint8_t *)ACCOUNT_DB_MAGIC, 8U);
    write_le32(output + 8U, ACCOUNT_DB_VERSION);
    write_le32(output + 12U, (uint32_t)database.user_count);
    write_le32(output + 16U, database.next_uid);
    write_le64(output + 20U, generation);

    size_t offset = ACCOUNT_DB_HEADER_BYTES;
    for (size_t i = 0U; i < database.user_count; ++i) {
        const struct account_record *user = &database.users[i];
        if (user->uid == 0U ||
            (user->role != NEXUS_ACCOUNT_ROLE_STANDARD &&
             user->role != NEXUS_ACCOUNT_ROLE_ADMIN) ||
            !valid_username(user->username) ||
            user->iterations < NEXUS_ACCOUNT_MIN_PBKDF2_ITERATIONS ||
            user->iterations > ACCOUNT_DB_MAX_ITERATIONS ||
            !profile_valid(&user->profile)) return false;

        write_le32(output + offset, user->uid);
        offset += 4U;
        output[offset++] = (uint8_t)user->role;
        output[offset++] = user->active ? 1U : 0U;
        copy_bytes(output + offset, (const uint8_t *)user->username,
                   NEXUS_ACCOUNT_NAME_MAX);
        offset += NEXUS_ACCOUNT_NAME_MAX;
        copy_bytes(output + offset, user->salt, sizeof(user->salt));
        offset += sizeof(user->salt);
        write_le32(output + offset, user->iterations);
        offset += 4U;
        copy_bytes(output + offset, user->verifier, sizeof(user->verifier));
        offset += sizeof(user->verifier);
        output[offset++] = user->profile.theme;
        output[offset++] = user->profile.accent;
        output[offset++] = user->profile.wallpaper;
        output[offset++] = user->profile.ui_scale_percent;
        output[offset++] = user->profile.startup_count;
        for (size_t app = 0U; app < NEXUS_ACCOUNT_STARTUP_MAX; ++app) {
            copy_bytes(output + offset,
                (const uint8_t *)user->profile.startup_apps[app],
                NEXUS_ACCOUNT_STARTUP_NAME_MAX);
            offset += NEXUS_ACCOUNT_STARTUP_NAME_MAX;
        }
    }

    const uint32_t checksum = crc32_bytes(output, offset);
    write_le32(output + offset, checksum);
    offset += ACCOUNT_DB_TRAILER_BYTES;
    *output_length = offset;
    return true;
}

static bool decode_database(const uint8_t *input, size_t length) {
    if (input == NULL || length < ACCOUNT_DB_HEADER_BYTES +
                                  ACCOUNT_DB_TRAILER_BYTES ||
        length > ACCOUNT_DB_MAX_BYTES) return false;
    static const uint8_t magic[8] = ACCOUNT_DB_MAGIC;
    for (size_t i = 0U; i < sizeof(magic); ++i)
        if (input[i] != magic[i]) return false;
    if (read_le32(input + 8U) != ACCOUNT_DB_VERSION) return false;

    const uint32_t count = read_le32(input + 12U);
    const uint32_t next_uid = read_le32(input + 16U);
    const uint64_t generation = read_le64(input + 20U);
    if (count > NEXUS_ACCOUNT_MAX_USERS || next_uid == 0U ||
        length != ACCOUNT_DB_HEADER_BYTES +
                  (size_t)count * ACCOUNT_DB_RECORD_BYTES +
                  ACCOUNT_DB_TRAILER_BYTES) return false;

    const size_t checksum_offset = length - ACCOUNT_DB_TRAILER_BYTES;
    if (read_le32(input + checksum_offset) !=
        crc32_bytes(input, checksum_offset)) return false;

    struct account_database candidate = {0};
    candidate.user_count = count;
    candidate.next_uid = next_uid;
    candidate.generation = generation;

    size_t offset = ACCOUNT_DB_HEADER_BYTES;
    for (size_t i = 0U; i < count; ++i) {
        struct account_record *user = &candidate.users[i];
        user->uid = read_le32(input + offset);
        offset += 4U;
        user->role = (enum nexus_account_role)input[offset++];
        if (input[offset] > 1U) return false;
        user->active = input[offset++] != 0U;
        copy_bytes((uint8_t *)user->username, input + offset,
                   NEXUS_ACCOUNT_NAME_MAX);
        offset += NEXUS_ACCOUNT_NAME_MAX;
        user->username[NEXUS_ACCOUNT_NAME_MAX - 1U] = '\0';
        copy_bytes(user->salt, input + offset, sizeof(user->salt));
        offset += sizeof(user->salt);
        user->iterations = read_le32(input + offset);
        offset += 4U;
        copy_bytes(user->verifier, input + offset, sizeof(user->verifier));
        offset += sizeof(user->verifier);
        user->profile.theme = input[offset++];
        user->profile.accent = input[offset++];
        user->profile.wallpaper = input[offset++];
        user->profile.ui_scale_percent = input[offset++];
        user->profile.startup_count = input[offset++];
        for (size_t app = 0U; app < NEXUS_ACCOUNT_STARTUP_MAX; ++app) {
            copy_bytes((uint8_t *)user->profile.startup_apps[app],
                input + offset, NEXUS_ACCOUNT_STARTUP_NAME_MAX);
            user->profile.startup_apps[app]
                [NEXUS_ACCOUNT_STARTUP_NAME_MAX - 1U] = '\0';
            offset += NEXUS_ACCOUNT_STARTUP_NAME_MAX;
        }

        if (user->uid == 0U ||
            (user->role != NEXUS_ACCOUNT_ROLE_STANDARD &&
             user->role != NEXUS_ACCOUNT_ROLE_ADMIN) ||
            !user->active || !valid_username(user->username) ||
            user->iterations < NEXUS_ACCOUNT_MIN_PBKDF2_ITERATIONS ||
            user->iterations > ACCOUNT_DB_MAX_ITERATIONS ||
            !profile_valid(&user->profile) || user->uid >= next_uid)
            return false;
        for (size_t prior = 0U; prior < i; ++prior) {
            if (candidate.users[prior].uid == user->uid) return false;
            size_t k = 0U;
            while (candidate.users[prior].username[k] != '\0' &&
                   user->username[k] != '\0' &&
                   candidate.users[prior].username[k] == user->username[k])
                ++k;
            if (candidate.users[prior].username[k] == '\0' &&
                user->username[k] == '\0') return false;
        }
    }

    bool has_admin = false;
    for (size_t i = 0U; i < count; ++i)
        if (candidate.users[i].role == NEXUS_ACCOUNT_ROLE_ADMIN)
            has_admin = true;
    if (count != 0U && !has_admin) return false;

    database = candidate;
    return true;
}

static bool persist_database(void) {
    if (!backend_ready || active_backend.store == NULL ||
        database.generation == UINT64_MAX) return false;
    uint8_t encoded[ACCOUNT_DB_MAX_BYTES];
    size_t length = 0U;
    const uint64_t next_generation = database.generation + 1U;
    const bool prepared = encode_database(encoded, &length, next_generation);
    const bool stored = prepared &&
        active_backend.store(active_backend.context, encoded, length);
    secure_zero(encoded, sizeof(encoded));
    if (!stored) return false;
    database.generation = next_generation;
    accounts_status = NEXUS_ACCOUNTS_READY;
    return true;
}

static uint64_t current_ticks(void) {
    return active_backend.ticks != NULL
        ? active_backend.ticks(active_backend.context) : 0U;
}

static bool create_record(const char *username, const char *password,
                          enum nexus_account_role role,
                          struct account_record *record) {
    if (record == NULL || !valid_username(username) ||
        (role != NEXUS_ACCOUNT_ROLE_STANDARD &&
         role != NEXUS_ACCOUNT_ROLE_ADMIN) ||
        !active_backend.random ||
        !active_backend.random(active_backend.context,
                               record->salt, sizeof(record->salt)))
        return false;

    size_t password_length = 0U;
    if (!valid_password(password, &password_length)) {
        secure_zero(record->salt, sizeof(record->salt));
        return false;
    }
    (void)password_length;
    record->uid = database.next_uid;
    record->role = role;
    record->active = true;
    copy_text(record->username, username, sizeof(record->username));
    record->iterations = NEXUS_ACCOUNT_PBKDF2_ITERATIONS;
    record_default_profile(&record->profile);
    if (!derive_password_verifier(password, record->salt,
                                  record->iterations, record->verifier)) {
        secure_zero(record, sizeof(*record));
        return false;
    }

    uint8_t salt_nonzero = 0U;
    for (size_t i = 0U; i < sizeof(record->salt); ++i)
        salt_nonzero |= record->salt[i];
    if (salt_nonzero == 0U) {
        secure_zero(record, sizeof(*record));
        return false;
    }
    return true;
}

static bool verify_record_password(const struct account_record *record,
                                   const char *password) {
    if (record == NULL) return false;
    size_t password_length = 0U;
    uint8_t candidate[NEXUS_ACCOUNT_VERIFIER_BYTES];
    const bool valid = valid_password(password, &password_length);
    const char *material = valid ? password : "invalid-password-material";
    const size_t material_length = valid ? password_length : 25U;
    const bool derived = nexus_accounts_pbkdf2_sha256(
        material, material_length, record->salt, record->iterations,
        candidate);
    const bool matches = derived && valid &&
        bytes_equal_constant_time(candidate, record->verifier,
                                  sizeof(candidate));
    secure_zero(candidate, sizeof(candidate));
    return matches;
}

static bool create_session(const struct account_record *record) {
    if (record == NULL || active_backend.random == NULL) return false;
    uint64_t session_id = 0U;
    if (!active_backend.random(active_backend.context,
                              &session_id, sizeof(session_id)) ||
        session_id == 0U) {
        secure_zero(&session_id, sizeof(session_id));
        return false;
    }
    active_session = (struct nexus_account_session){
        .logged_in = true,
        .uid = record->uid,
        .role = record->role,
        .session_id = session_id
    };
    copy_text(active_session.username, record->username,
              sizeof(active_session.username));
    secure_zero(&session_id, sizeof(session_id));
    return true;
}

static bool can_modify_user(uint32_t uid) {
    return nexus_accounts_is_admin() ||
           (nexus_accounts_is_logged_in() &&
            nexus_accounts_current_uid() == uid);
}

static bool save_with_rollback(const struct account_database *original) {
    if (persist_database()) return true;
    database = *original;
    return false;
}

static enum nexus_account_load_result system_load(
    void *context, uint8_t *buffer, size_t capacity, size_t *length) {
    (void)context;
    if (!nxfs_is_mounted() || buffer == NULL || length == NULL)
        return NEXUS_ACCOUNT_LOAD_ERROR;
    for (size_t i = 0U; i < nxfs_entry_count(); ++i) {
        const struct nexus_nxfs_entry *entry = nxfs_entry_at(i);
        if (entry == NULL || entry->directory) continue;
        static const char database_name[] = "ACCOUNTS.DAT";
        size_t j = 0U;
        while (entry->name[j] != '\0' && database_name[j] != '\0' &&
               entry->name[j] == database_name[j]) ++j;
        if (entry->name[j] != '\0' || database_name[j] != '\0') continue;
        if (entry->size > capacity) return NEXUS_ACCOUNT_LOAD_ERROR;
        if (!nxfs_read_file(entry, 0U, entry->size, buffer))
            return NEXUS_ACCOUNT_LOAD_ERROR;
        *length = entry->size;
        return NEXUS_ACCOUNT_LOAD_FOUND;
    }
    return NEXUS_ACCOUNT_LOAD_MISSING;
}

static bool system_store(void *context, const uint8_t *buffer, size_t length) {
    (void)context;
    if (!nxfs_is_writable() || buffer == NULL ||
        length > ACCOUNT_DB_MAX_BYTES) return false;
    return nxfs_write_file("ACCOUNTS.DAT", buffer, length);
}

static bool system_random(void *context, void *buffer, size_t length) {
    (void)context;
    return nexus_rng_fill(buffer, length);
}

static uint64_t system_ticks(void *context) {
    (void)context;
    return timer_ticks();
}

void nexus_accounts_init(void) {
    const struct nexus_account_backend backend = {
        .load = system_load,
        .store = system_store,
        .random = system_random,
        .ticks = system_ticks,
        .ticks_per_second = 100U,
        .context = NULL
    };
    (void)nexus_accounts_init_with_backend(&backend);
}

bool nexus_accounts_init_with_backend(
    const struct nexus_account_backend *backend) {
    secure_zero(&database, sizeof(database));
    secure_zero(&active_session, sizeof(active_session));
    secure_zero(login_failures, sizeof(login_failures));
    secure_zero(lockout_until, sizeof(lockout_until));
    active_backend = (struct nexus_account_backend){0};
    backend_ready = false;
    accounts_status = NEXUS_ACCOUNTS_STORAGE_UNAVAILABLE;

    if (backend == NULL || backend->load == NULL ||
        backend->store == NULL || backend->random == NULL ||
        backend->ticks_per_second == 0U) return false;
    active_backend = *backend;
    backend_ready = true;
    database.next_uid = 1U;

    uint8_t data[ACCOUNT_DB_MAX_BYTES];
    size_t length = 0U;
    const enum nexus_account_load_result loaded =
        active_backend.load(active_backend.context, data, sizeof(data), &length);
    if (loaded == NEXUS_ACCOUNT_LOAD_ERROR) {
        secure_zero(data, sizeof(data));
        accounts_status = NEXUS_ACCOUNTS_STORAGE_UNAVAILABLE;
        return false;
    }
    if (loaded == NEXUS_ACCOUNT_LOAD_MISSING) {
        accounts_status = NEXUS_ACCOUNTS_SETUP_REQUIRED;
        secure_zero(data, sizeof(data));
        return true;
    }

    const bool decoded = decode_database(data, length);
    secure_zero(data, sizeof(data));
    if (!decoded) {
        secure_zero(&database, sizeof(database));
        accounts_status = NEXUS_ACCOUNTS_CORRUPT;
        return false;
    }
    accounts_status = active_backend.store == NULL
        ? NEXUS_ACCOUNTS_READ_ONLY : NEXUS_ACCOUNTS_READY;
    return true;
}

enum nexus_accounts_status nexus_accounts_get_status(void) {
    return accounts_status;
}

bool nexus_accounts_setup_required(void) {
    return accounts_status == NEXUS_ACCOUNTS_SETUP_REQUIRED &&
           database.user_count == 0U;
}

size_t nexus_accounts_count(void) {
    return database.user_count;
}

bool nexus_accounts_get_info(size_t index, struct nexus_account_info *info) {
    if (info == NULL || index >= database.user_count) return false;
    const struct account_record *user = &database.users[index];
    info->uid = user->uid;
    info->role = user->role;
    info->active = user->active;
    copy_text(info->username, user->username, sizeof(info->username));
    return true;
}

bool nexus_accounts_bootstrap_admin(const char *username,
                                    const char *password) {
    if (accounts_status != NEXUS_ACCOUNTS_SETUP_REQUIRED ||
        database.user_count != 0U || !backend_ready ||
        active_backend.store == NULL) return false;

    struct account_record admin = {0};
    if (!create_record(username, password, NEXUS_ACCOUNT_ROLE_ADMIN, &admin))
        return false;

    const struct account_database original = database;
    database.users[0] = admin;
    database.user_count = 1U;
    if (database.next_uid == UINT32_MAX) {
        database = original;
        secure_zero(&admin, sizeof(admin));
        return false;
    }
    ++database.next_uid;
    const bool saved = save_with_rollback(&original);
    secure_zero(&admin, sizeof(admin));
    if (!saved) return false;

    /* Setup completes; the UI returns to the normal login screen. */
    return true;
}

bool nexus_accounts_create_user(const char *username, const char *password,
                                enum nexus_account_role role) {
    if (!nexus_accounts_authorize_admin() ||
        (accounts_status != NEXUS_ACCOUNTS_READY &&
         accounts_status != NEXUS_ACCOUNTS_READ_ONLY) ||
        active_backend.store == NULL ||
        database.user_count >= NEXUS_ACCOUNT_MAX_USERS ||
        database.next_uid == 0U || database.next_uid == UINT32_MAX ||
        !valid_username(username) ||
        find_user_name(username) != database.user_count) return false;

    struct account_record record = {0};
    if (!create_record(username, password, role, &record)) return false;
    const struct account_database original = database;
    database.users[database.user_count++] = record;
    ++database.next_uid;
    const bool saved = save_with_rollback(&original);
    secure_zero(&record, sizeof(record));
    return saved;
}

bool nexus_accounts_delete_user(uint32_t uid) {
    if (!nexus_accounts_authorize_admin() ||
        active_backend.store == NULL || uid == active_session.uid)
        return false;
    const size_t index = find_user_uid(uid);
    if (index == database.user_count) return false;
    if (database.users[index].role == NEXUS_ACCOUNT_ROLE_ADMIN &&
        database.users[index].active && count_active_admins() <= 1U)
        return false;

    const struct account_database original = database;
    secure_zero(&database.users[index], sizeof(database.users[index]));
    for (size_t i = index; i + 1U < database.user_count; ++i)
        database.users[i] = database.users[i + 1U];
    --database.user_count;
    secure_zero(&database.users[database.user_count],
                sizeof(database.users[database.user_count]));
    return save_with_rollback(&original);
}

bool nexus_accounts_set_user_active(uint32_t uid, bool active) {
    if (!nexus_accounts_authorize_admin() || active_backend.store == NULL ||
        uid == active_session.uid) return false;
    const size_t index = find_user_uid(uid);
    if (index == database.user_count) return false;
    struct account_record *user = &database.users[index];
    if (user->active == active) return true;
    if (!active && user->role == NEXUS_ACCOUNT_ROLE_ADMIN &&
        count_active_admins() <= 1U) return false;

    const struct account_database original = database;
    user->active = active;
    return save_with_rollback(&original);
}

bool nexus_accounts_change_own_password(const char *old_password,
                                        const char *new_password) {
    if (!nexus_accounts_is_logged_in() || active_backend.store == NULL)
        return false;
    const size_t index = find_user_uid(active_session.uid);
    if (index == database.user_count ||
        !verify_record_password(&database.users[index], old_password))
        return false;

    struct account_record replacement = database.users[index];
    secure_zero(replacement.salt, sizeof(replacement.salt));
    secure_zero(replacement.verifier, sizeof(replacement.verifier));
    if (!active_backend.random(active_backend.context,
            replacement.salt, sizeof(replacement.salt)) ||
        !derive_password_verifier(new_password, replacement.salt,
            NEXUS_ACCOUNT_PBKDF2_ITERATIONS, replacement.verifier)) {
        secure_zero(&replacement, sizeof(replacement));
        return false;
    }
    replacement.iterations = NEXUS_ACCOUNT_PBKDF2_ITERATIONS;

    const struct account_database original = database;
    database.users[index] = replacement;
    const bool saved = save_with_rollback(&original);
    secure_zero(&replacement, sizeof(replacement));
    return saved;
}

bool nexus_accounts_admin_reset_password(uint32_t uid,
                                         const char *new_password) {
    if (!nexus_accounts_authorize_admin() || active_backend.store == NULL)
        return false;
    const size_t index = find_user_uid(uid);
    if (index == database.user_count) return false;

    struct account_record replacement = database.users[index];
    if (!active_backend.random(active_backend.context,
            replacement.salt, sizeof(replacement.salt)) ||
        !derive_password_verifier(new_password, replacement.salt,
            NEXUS_ACCOUNT_PBKDF2_ITERATIONS, replacement.verifier)) {
        secure_zero(&replacement, sizeof(replacement));
        return false;
    }
    replacement.iterations = NEXUS_ACCOUNT_PBKDF2_ITERATIONS;

    const struct account_database original = database;
    database.users[index] = replacement;
    const bool saved = save_with_rollback(&original);
    secure_zero(&replacement, sizeof(replacement));
    return saved;
}

bool nexus_accounts_login(const char *username, const char *password) {
    if ((accounts_status != NEXUS_ACCOUNTS_READY &&
         accounts_status != NEXUS_ACCOUNTS_READ_ONLY) ||
        nexus_accounts_is_logged_in() || username == NULL ||
        password == NULL) return false;

    const size_t index = valid_username(username)
        ? find_user_name(username) : database.user_count;
    if (index == database.user_count) {
        static const uint8_t dummy_salt[NEXUS_ACCOUNT_SALT_BYTES] = {
            0x4EU,0x58U,0x55U,0x53U,0x2DU,0x41U,0x43U,0x43U,
            0x54U,0x2DU,0x44U,0x55U,0x4DU,0x4DU,0x59U,0x01U
        };
        uint8_t ignored[NEXUS_ACCOUNT_VERIFIER_BYTES];
        size_t password_length = 0U;
        const char *material = valid_password(password, &password_length)
            ? password : "invalid-password-material";
        const size_t amount = material == password ? password_length : 25U;
        (void)nexus_accounts_pbkdf2_sha256(
            material, amount, dummy_salt,
            NEXUS_ACCOUNT_PBKDF2_ITERATIONS, ignored);
        secure_zero(ignored, sizeof(ignored));
        nexus_security_audit(NEXUS_SECURITY_EVENT_AUTH_FAILURE, 0U,
            NEXUS_SECURITY_REASON_AUTHENTICATION, 0U, 0U);
        return false;
    }

    struct account_record *user = &database.users[index];
    const uint64_t now = current_ticks();
    if (lockout_until[index] != 0U && now < lockout_until[index]) {
        (void)verify_record_password(user, password);
        nexus_security_audit(NEXUS_SECURITY_EVENT_AUTH_FAILURE, 0U,
            NEXUS_SECURITY_REASON_AUTHENTICATION, user->uid,
            lockout_until[index] - now);
        return false;
    }
    if (!user->active || !verify_record_password(user, password)) {
        if (login_failures[index] < UINT8_MAX) ++login_failures[index];
        if (login_failures[index] >= NEXUS_ACCOUNT_LOCKOUT_FAILURES) {
            const uint64_t duration =
                (uint64_t)active_backend.ticks_per_second *
                NEXUS_ACCOUNT_LOCKOUT_SECONDS;
            lockout_until[index] = UINT64_MAX - now < duration
                ? UINT64_MAX : now + duration;
        }
        nexus_security_audit(NEXUS_SECURITY_EVENT_AUTH_FAILURE, 0U,
            NEXUS_SECURITY_REASON_AUTHENTICATION, user->uid,
            login_failures[index]);
        return false;
    }

    login_failures[index] = 0U;
    lockout_until[index] = 0U;
    if (!create_session(user)) {
        nexus_security_audit(NEXUS_SECURITY_EVENT_AUTH_FAILURE, 0U,
            NEXUS_SECURITY_REASON_AUTHENTICATION, user->uid, 0U);
        return false;
    }
    return true;
}

void nexus_accounts_logout(void) {
    secure_zero(&active_session, sizeof(active_session));
}

bool nexus_accounts_get_session(struct nexus_account_session *session) {
    if (session == NULL) return false;
    *session = active_session;
    return true;
}

bool nexus_accounts_is_logged_in(void) {
    return active_session.logged_in && active_session.uid != 0U &&
           active_session.session_id != 0U;
}

bool nexus_accounts_is_admin(void) {
    return nexus_accounts_is_logged_in() &&
           active_session.role == NEXUS_ACCOUNT_ROLE_ADMIN;
}

uint32_t nexus_accounts_current_uid(void) {
    return nexus_accounts_is_logged_in() ? active_session.uid : 0U;
}

bool nexus_accounts_authorize_admin(void) {
    return nexus_accounts_is_admin();
}

bool nexus_accounts_get_profile(uint32_t uid,
                                struct nexus_account_profile *profile) {
    if (profile == NULL || !can_modify_user(uid)) return false;
    const size_t index = find_user_uid(uid);
    if (index == database.user_count) return false;
    *profile = database.users[index].profile;
    return true;
}

bool nexus_accounts_set_profile(uint32_t uid,
                                const struct nexus_account_profile *profile) {
    if (active_backend.store == NULL || !can_modify_user(uid) ||
        !profile_valid(profile)) return false;
    const size_t index = find_user_uid(uid);
    if (index == database.user_count) return false;
    const struct account_database original = database;
    database.users[index].profile = *profile;
    return save_with_rollback(&original);
}

bool nexus_accounts_add_startup_app(uint32_t uid, const char *application) {
    if (!valid_startup_name(application) || active_backend.store == NULL ||
        !can_modify_user(uid)) return false;
    const size_t index = find_user_uid(uid);
    if (index == database.user_count) return false;
    struct nexus_account_profile *profile = &database.users[index].profile;
    for (size_t i = 0U; i < profile->startup_count; ++i) {
        size_t j = 0U;
        while (profile->startup_apps[i][j] != '\0' &&
               application[j] != '\0' &&
               profile->startup_apps[i][j] == application[j]) ++j;
        if (profile->startup_apps[i][j] == '\0' && application[j] == '\0')
            return true;
    }
    if (profile->startup_count >= NEXUS_ACCOUNT_STARTUP_MAX) return false;
    const struct account_database original = database;
    copy_text(profile->startup_apps[profile->startup_count],
              application, NEXUS_ACCOUNT_STARTUP_NAME_MAX);
    ++profile->startup_count;
    return save_with_rollback(&original);
}

bool nexus_accounts_remove_startup_app(uint32_t uid,
                                       const char *application) {
    if (!valid_startup_name(application) || active_backend.store == NULL ||
        !can_modify_user(uid)) return false;
    const size_t index = find_user_uid(uid);
    if (index == database.user_count) return false;
    struct nexus_account_profile *profile = &database.users[index].profile;
    size_t found = profile->startup_count;
    for (size_t i = 0U; i < profile->startup_count; ++i) {
        size_t j = 0U;
        while (profile->startup_apps[i][j] != '\0' &&
               application[j] != '\0' &&
               profile->startup_apps[i][j] == application[j]) ++j;
        if (profile->startup_apps[i][j] == '\0' && application[j] == '\0') {
            found = i;
            break;
        }
    }
    if (found == profile->startup_count) return false;
    const struct account_database original = database;
    for (size_t i = found; i + 1U < profile->startup_count; ++i)
        copy_text(profile->startup_apps[i], profile->startup_apps[i + 1U],
                  NEXUS_ACCOUNT_STARTUP_NAME_MAX);
    --profile->startup_count;
    profile->startup_apps[profile->startup_count][0] = '\0';
    return save_with_rollback(&original);
}
