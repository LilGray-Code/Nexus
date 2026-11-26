#ifndef NEXUS_GAME_LIBRARY_H
#define NEXUS_GAME_LIBRARY_H

#include "gaming.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NEXUS_GAME_LIBRARY_MAX_ENTRIES 24U
#define NEXUS_GAME_ID_MAX 31U
#define NEXUS_GAME_TITLE_MAX 63U
#define NEXUS_GAME_PATH_MAX 31U
#define NEXUS_GAME_VERSION_MAX 23U
#define NEXUS_GAME_TEST_NOTE_MAX 63U

struct nexus_game_library_entry {
    char game_id[NEXUS_GAME_ID_MAX + 1U];
    char title[NEXUS_GAME_TITLE_MAX + 1U];
    char executable_path[NEXUS_GAME_PATH_MAX + 1U];
    char game_version[NEXUS_GAME_VERSION_MAX + 1U];
    char tested_build_id[NEXUS_GAME_VERSION_MAX + 1U];
    char test_note[NEXUS_GAME_TEST_NOTE_MAX + 1U];
    struct nexus_game_descriptor compatibility;
    uint32_t launch_count;
    uint64_t last_played_ticks;
    uint64_t tested_at_ticks;
};

enum nexus_game_library_error {
    NEXUS_GAME_LIBRARY_ERROR_NONE = 0,
    NEXUS_GAME_LIBRARY_ERROR_ARGUMENT,
    NEXUS_GAME_LIBRARY_ERROR_FULL,
    NEXUS_GAME_LIBRARY_ERROR_NOT_FOUND,
    NEXUS_GAME_LIBRARY_ERROR_FORMAT,
    NEXUS_GAME_LIBRARY_ERROR_CHECKSUM,
    NEXUS_GAME_LIBRARY_ERROR_STORAGE,
    NEXUS_GAME_LIBRARY_ERROR_BACKEND,
    NEXUS_GAME_LIBRARY_ERROR_PREREQUISITES,
    NEXUS_GAME_LIBRARY_ERROR_UNTESTED,
    NEXUS_GAME_LIBRARY_ERROR_ANTICHEAT,
    NEXUS_GAME_LIBRARY_ERROR_EXECUTION
};

void nexus_game_library_init(void);
size_t nexus_game_library_count(void);
const struct nexus_game_library_entry *nexus_game_library_at(size_t index);
bool nexus_game_library_upsert(const struct nexus_game_library_entry *entry);
bool nexus_game_library_remove(const char *game_id);
bool nexus_game_library_load(void);
bool nexus_game_library_save(void);
enum nexus_game_library_error nexus_game_library_last_error(void);

/* A test result is evidence for one exact build/GPU/driver configuration. */
bool nexus_game_library_record_test(
    const char *game_id, enum nexus_game_compatibility compatibility,
    uint64_t tested_capabilities, uint16_t gpu_vendor_id,
    uint16_t gpu_device_id, uint32_t gpu_driver_version,
    const char *tested_build_id, const char *test_note);

/*
 * The first launch backend deliberately supports only bounded static Nexus
 * ELF files available from the root NXFS/system pack. Steam/Proton/Wine
 * profiles are reported as backend-not-implemented, never launched falsely.
 */
bool nexus_game_library_launch(
    const char *game_id,
    const struct nexus_gaming_system_capabilities *system,
    bool allow_untested,
    uint64_t *exit_status);

#endif
