#ifndef NEXUS_PROCESS_H
#define NEXUS_PROCESS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NEXUS_PROCESS_MAX 4U
#define NEXUS_PROCESS_NAME_MAX 16U

enum nexus_process_state {
    NEXUS_PROCESS_FREE = 0,
    NEXUS_PROCESS_READY,
    NEXUS_PROCESS_RUNNING,
    NEXUS_PROCESS_EXITED,
    NEXUS_PROCESS_FAILED
};

enum nexus_process_error {
    NEXUS_PROCESS_ERROR_NONE = 0,
    NEXUS_PROCESS_ERROR_NO_VOLUME,
    NEXUS_PROCESS_ERROR_NOT_FOUND,
    NEXUS_PROCESS_ERROR_IO,
    NEXUS_PROCESS_ERROR_TOO_LARGE,
    NEXUS_PROCESS_ERROR_TABLE_FULL,
    NEXUS_PROCESS_ERROR_BAD_ELF,
    NEXUS_PROCESS_ERROR_EXECUTION
};

struct nexus_process_info {
    uint32_t pid;
    uint32_t owner_uid;
    uint64_t session_id;
    enum nexus_process_state state;
    uint64_t exit_status;
    uint64_t start_tick;
    uint64_t elapsed_ticks;
    char name[NEXUS_PROCESS_NAME_MAX];
};

struct nexus_process_result {
    uint32_t pid;
    uint64_t exit_status;
};

void process_init(void);
bool process_spawn_elf(const char *name, const void *image, size_t image_size,
                       uint32_t *pid);
bool process_spawn_elf_with_capabilities(const char *name, const void *image,
                                         size_t image_size, uint64_t capabilities,
                                         uint32_t *pid);
bool process_run(uint32_t pid, uint64_t *exit_status);
bool process_execute_file(const char *name,
                          struct nexus_process_result *result,
                          enum nexus_process_error *error);
/* Runs an immutable/system file from the VFS root, independent of cwd. */
bool process_execute_root_file(const char *name,
                               struct nexus_process_result *result,
                               enum nexus_process_error *error);
bool process_execute_root_file_with_capabilities(
    const char *name, struct nexus_process_result *result,
    enum nexus_process_error *error, uint64_t capabilities);
size_t process_count(void);
bool process_get_info(size_t slot, struct nexus_process_info *info);
bool process_reap(uint32_t pid);
const char *process_state_name(enum nexus_process_state state);

#endif
