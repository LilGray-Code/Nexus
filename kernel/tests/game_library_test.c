#include "game_library.h"
#include "process.h"
#include "timer.h"
#include "vfs.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MOCK_LIBRARY_BYTES 8192U

static uint8_t library_data[MOCK_LIBRARY_BYTES];
static size_t library_size;
static bool library_present;
static bool nxfs_ready = true;
static uint32_t process_launches;
static char last_executable[NEXUS_GAME_PATH_MAX + 1U];
static uint64_t mock_ticks = 1234U;

bool vfs_nxfs_writable(void) { return nxfs_ready; }
bool vfs_write_root_path(const char *path, const void *data, size_t length) {
    if (!nxfs_ready || path == NULL || strcmp(path, "GAMES.NXL") != 0 ||
        data == NULL || length > sizeof(library_data)) return false;
    memcpy(library_data, data, length);
    library_size = length;
    library_present = true;
    return true;
}
bool vfs_open_root_readonly_path(const char *path,
                                 struct nexus_vfs_file *file) {
    if (path == NULL || strcmp(path, "GAMES.NXL") != 0 ||
        file == NULL || !library_present) return false;
    *file = (struct nexus_vfs_file){0};
    file->filesystem_entry = library_data;
    file->open = true;
    file->nxfs_file = true;
    return true;
}
bool vfs_file_size(const struct nexus_vfs_file *file, uint32_t *size) {
    if (file == NULL || !file->open ||
        file->filesystem_entry != library_data || size == NULL) return false;
    *size = (uint32_t)library_size;
    return true;
}
bool vfs_read(struct nexus_vfs_file *file, void *buffer,
              size_t requested, size_t *bytes_read) {
    if (file == NULL || !file->open ||
        file->filesystem_entry != library_data || bytes_read == NULL ||
        (requested != 0U && buffer == NULL) ||
        file->offset > library_size ||
        requested > library_size - file->offset) return false;
    memcpy(buffer, library_data + file->offset, requested);
    file->offset += (uint32_t)requested;
    *bytes_read = requested;
    return true;
}
void vfs_close(struct nexus_vfs_file *file) {
    if (file != NULL) *file = (struct nexus_vfs_file){0};
}
bool process_execute_root_file(const char *name,
                               struct nexus_process_result *result,
                               enum nexus_process_error *error) {
    if (name == NULL || result == NULL ||
        strcmp(name, "ARCADE.ELF") != 0) {
        if (error != NULL) *error = NEXUS_PROCESS_ERROR_NOT_FOUND;
        return false;
    }
    ++process_launches;
    strcpy(last_executable, name);
    result->pid = 9U;
    result->exit_status = 77U;
    if (error != NULL) *error = NEXUS_PROCESS_ERROR_NONE;
    return true;
}
uint64_t timer_ticks(void) { return mock_ticks; }

static bool equal_text(const char *a, const char *b) {
    return a != NULL && b != NULL && strcmp(a, b) == 0;
}

static bool test_assessment_override_ready(
    const struct nexus_game_descriptor *game,
    const struct nexus_gaming_system_capabilities *system,
    struct nexus_game_assessment *assessment) {
    (void)system;
    if (game == NULL || assessment == NULL) return false;
    *assessment = (struct nexus_game_assessment){0};
    if (game->anticheat == NEXUS_GAME_ANTICHEAT_KERNEL ||
        game->anticheat == NEXUS_GAME_ANTICHEAT_UNKNOWN) {
        assessment->status = NEXUS_GAME_ASSESS_BLOCKED_ANTICHEAT;
        return true;
    }
    if (game->compatibility == NEXUS_GAME_COMPAT_UNSUPPORTED) {
        assessment->status = NEXUS_GAME_ASSESS_TESTED_UNSUPPORTED;
        return true;
    }
    assessment->required_capabilities = NEXUS_GAME_CAP_NATIVE_STATIC_ELF;
    assessment->missing_capabilities =
        assessment->required_capabilities & ~system->available;
    if (assessment->missing_capabilities != 0U) {
        assessment->status = NEXUS_GAME_ASSESS_MISSING_PREREQUISITES;
    } else if (game->compatibility == NEXUS_GAME_COMPAT_VERIFIED) {
        assessment->status = NEXUS_GAME_ASSESS_VERIFIED;
    } else if (game->compatibility ==
               NEXUS_GAME_COMPAT_PLAYABLE_LIMITATIONS) {
        assessment->status = NEXUS_GAME_ASSESS_PLAYABLE_LIMITATIONS;
    } else {
        assessment->status = NEXUS_GAME_ASSESS_READY_TO_TEST;
    }
    return true;
}

/* The library test focuses on persistence and dispatch; evaluator logic is
 * exercised with the production evaluator in gaming_test.c.
 */
bool nexus_gaming_assess(
    const struct nexus_game_descriptor *game,
    const struct nexus_gaming_system_capabilities *system,
    struct nexus_game_assessment *assessment) {
    return test_assessment_override_ready(game, system, assessment);
}

static struct nexus_game_library_entry make_native_entry(void) {
    struct nexus_game_library_entry entry = {
        .game_id = "NEXUS_BREAKOUT",
        .title = "Nexus Breakout",
        .executable_path = "ARCADE.ELF",
        .game_version = "1.0",
        .compatibility = {
            .platform = NEXUS_GAME_PLATFORM_NATIVE_NEXUS,
            .graphics_api = NEXUS_GAME_GRAPHICS_NONE,
            .anticheat = NEXUS_GAME_ANTICHEAT_NONE,
            .compatibility = NEXUS_GAME_COMPAT_UNTESTED
        }
    };
    return entry;
}

static void reset_mocks(void) {
    memset(library_data, 0, sizeof(library_data));
    library_size = 0U;
    library_present = false;
    nxfs_ready = true;
    process_launches = 0U;
    last_executable[0] = '\0';
    mock_ticks = 1234U;
    nexus_game_library_init();
}

static void test_upsert_save_and_reload(void) {
    reset_mocks();
    struct nexus_game_library_entry entry = make_native_entry();
    assert(nexus_game_library_upsert(&entry));
    assert(nexus_game_library_count() == 1U);
    assert(nexus_game_library_save());
    assert(library_present && library_size > 60U);

    nexus_game_library_init();
    assert(nexus_game_library_count() == 0U);
    assert(nexus_game_library_load());
    assert(nexus_game_library_count() == 1U);
    const struct nexus_game_library_entry *loaded =
        nexus_game_library_at(0U);
    assert(loaded != NULL);
    assert(equal_text(loaded->game_id, "NEXUS_BREAKOUT"));
    assert(equal_text(loaded->executable_path, "ARCADE.ELF"));
    assert(loaded->compatibility.compatibility == NEXUS_GAME_COMPAT_UNTESTED);
}

static void test_bad_records_and_checksums_are_rejected(void) {
    reset_mocks();
    struct nexus_game_library_entry bad = make_native_entry();
    strcpy(bad.executable_path, "../ARCADE.ELF");
    assert(!nexus_game_library_upsert(&bad));
    bad = make_native_entry();
    assert(nexus_game_library_upsert(&bad));
    assert(nexus_game_library_save());
    library_data[library_size - 1U] ^= 1U;
    assert(!nexus_game_library_load());
    assert(nexus_game_library_last_error() ==
           NEXUS_GAME_LIBRARY_ERROR_CHECKSUM);
}

static void test_test_evidence_is_bound_to_build_and_driver(void) {
    reset_mocks();
    struct nexus_game_library_entry entry = make_native_entry();
    assert(nexus_game_library_upsert(&entry));
    assert(nexus_game_library_record_test(
        "NEXUS_BREAKOUT", NEXUS_GAME_COMPAT_VERIFIED,
        NEXUS_GAME_CAP_NATIVE_STATIC_ELF, 0x1234U, 0x5678U, 9U,
        "BUILD_A", "QEMU smoke test"));
    const struct nexus_game_library_entry *tested =
        nexus_game_library_at(0U);
    assert(tested->compatibility.compatibility == NEXUS_GAME_COMPAT_VERIFIED);
    assert(tested->compatibility.tested_gpu_vendor_id == 0x1234U);
    assert(tested->compatibility.tested_gpu_device_id == 0x5678U);
    assert(tested->compatibility.tested_gpu_driver_version == 9U);
    assert(equal_text(tested->tested_build_id, "BUILD_A"));
    assert(equal_text(tested->test_note, "QEMU smoke test"));
    assert(tested->tested_at_ticks == 1234U);
    assert(nexus_game_library_save());
    nexus_game_library_init();
    assert(nexus_game_library_load());
    assert(nexus_game_library_at(0U)->tested_at_ticks == 1234U);
}

static void test_native_launch_requires_explicit_untested_opt_in(void) {
    reset_mocks();
    struct nexus_game_library_entry entry = make_native_entry();
    assert(nexus_game_library_upsert(&entry));
    struct nexus_gaming_system_capabilities system = {
        .available = NEXUS_GAME_CAP_NATIVE_STATIC_ELF
    };
    uint64_t exit_status = 0U;
    assert(!nexus_game_library_launch(
        "NEXUS_BREAKOUT", &system, false, &exit_status));
    assert(nexus_game_library_last_error() ==
           NEXUS_GAME_LIBRARY_ERROR_UNTESTED);
    assert(process_launches == 0U);

    assert(nexus_game_library_launch(
        "NEXUS_BREAKOUT", &system, true, &exit_status));
    assert(process_launches == 1U);
    assert(equal_text(last_executable, "ARCADE.ELF"));
    assert(exit_status == 77U);
    assert(nexus_game_library_at(0U)->launch_count == 1U);
    assert(nexus_game_library_at(0U)->last_played_ticks == mock_ticks);
}

static void test_steam_and_kernel_anticheat_never_fall_through_to_native(void) {
    reset_mocks();
    struct nexus_game_library_entry steam = make_native_entry();
    strcpy(steam.game_id, "STEAM_GAME");
    steam.executable_path[0] = '\0';
    steam.compatibility.platform = NEXUS_GAME_PLATFORM_PROTON;
    steam.compatibility.graphics_api =
        NEXUS_GAME_GRAPHICS_DIRECT3D_12;
    assert(nexus_game_library_upsert(&steam));

    struct nexus_gaming_system_capabilities system = {
        .available = NEXUS_GAME_CAP_NATIVE_STATIC_ELF
    };
    uint64_t exit_status = 0U;
    assert(!nexus_game_library_launch(
        "STEAM_GAME", &system, true, &exit_status));
    assert(nexus_game_library_last_error() ==
           NEXUS_GAME_LIBRARY_ERROR_BACKEND);
    assert(process_launches == 0U);

    struct nexus_game_library_entry anticheat = make_native_entry();
    strcpy(anticheat.game_id, "KERNEL_AC");
    anticheat.compatibility.anticheat = NEXUS_GAME_ANTICHEAT_KERNEL;
    assert(nexus_game_library_upsert(&anticheat));
    assert(!nexus_game_library_launch(
        "KERNEL_AC", &system, true, &exit_status));
    assert(nexus_game_library_last_error() ==
           NEXUS_GAME_LIBRARY_ERROR_ANTICHEAT);
    assert(process_launches == 0U);
}

static void test_remove_and_persistent_only_save(void) {
    reset_mocks();
    struct nexus_game_library_entry entry = make_native_entry();
    assert(nexus_game_library_upsert(&entry));
    nxfs_ready = false;
    assert(!nexus_game_library_save());
    assert(nexus_game_library_last_error() ==
           NEXUS_GAME_LIBRARY_ERROR_STORAGE);
    nxfs_ready = true;
    assert(nexus_game_library_remove("NEXUS_BREAKOUT"));
    assert(nexus_game_library_count() == 0U);
    assert(!nexus_game_library_remove("NEXUS_BREAKOUT"));
}

int main(void) {
    test_upsert_save_and_reload();
    test_bad_records_and_checksums_are_rejected();
    test_test_evidence_is_bound_to_build_and_driver();
    test_native_launch_requires_explicit_untested_opt_in();
    test_steam_and_kernel_anticheat_never_fall_through_to_native();
    test_remove_and_persistent_only_save();
    puts("NEXUS GAME LIBRARY TESTS PASSED");
    return 0;
}
