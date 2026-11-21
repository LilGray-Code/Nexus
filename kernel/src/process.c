#include "process.h"

#include "elf64.h"
#include "paging.h"
#include "usermode.h"
#include "vfs.h"

#include <stddef.h>
#include <stdint.h>

#define PROCESS_KERNEL_STACK_SIZE 16384U
#define PROCESS_MAX_IMAGE_SIZE (128U * 1024U)

struct nexus_process {
    uint32_t pid;
    enum nexus_process_state state;
    uint64_t address_space;
    uint64_t entry_point;
    uint64_t stack_pointer;
    uint64_t exit_status;
    char name[NEXUS_PROCESS_NAME_MAX];
    uint8_t kernel_stack[PROCESS_KERNEL_STACK_SIZE]
        __attribute__((aligned(16)));
};

static struct nexus_process processes[NEXUS_PROCESS_MAX];
static uint32_t next_pid = 1U;
static uint8_t process_image[PROCESS_MAX_IMAGE_SIZE]
    __attribute__((aligned(16)));

static void clear_slot(struct nexus_process *process) {
    if (process == NULL) return;
    process->pid = 0U;
    process->state = NEXUS_PROCESS_FREE;
    process->address_space = 0U;
    process->entry_point = 0U;
    process->stack_pointer = 0U;
    process->exit_status = UINT64_MAX;
    process->name[0] = '\0';
}

static struct nexus_process *find_process(uint32_t pid) {
    if (pid == 0U) return NULL;
    for (size_t i = 0; i < NEXUS_PROCESS_MAX; ++i) {
        if (processes[i].state != NEXUS_PROCESS_FREE &&
            processes[i].pid == pid) {
            return &processes[i];
        }
    }
    return NULL;
}

static struct nexus_process *find_available_slot(void) {
    for (size_t i = 0; i < NEXUS_PROCESS_MAX; ++i) {
        if (processes[i].state == NEXUS_PROCESS_FREE) return &processes[i];
    }
    /* Completed records may be recycled; active/ready slots are never stolen. */
    for (size_t i = 0; i < NEXUS_PROCESS_MAX; ++i) {
        if (processes[i].state == NEXUS_PROCESS_EXITED ||
            processes[i].state == NEXUS_PROCESS_FAILED) {
            clear_slot(&processes[i]);
            return &processes[i];
        }
    }
    return NULL;
}

static bool has_available_slot(void) {
    for (size_t i = 0; i < NEXUS_PROCESS_MAX; ++i) {
        if (processes[i].state == NEXUS_PROCESS_FREE ||
            processes[i].state == NEXUS_PROCESS_EXITED ||
            processes[i].state == NEXUS_PROCESS_FAILED) {
            return true;
        }
    }
    return false;
}

static bool copy_process_name(char destination[NEXUS_PROCESS_NAME_MAX],
                              const char *source) {
    if (destination == NULL || source == NULL) return false;
    for (size_t i = 0; i < NEXUS_PROCESS_NAME_MAX; ++i) destination[i] = '\0';
    size_t length = 0;
    while (length < NEXUS_PROCESS_NAME_MAX - 1U && source[length] != '\0') {
        if (source[length] == '/' || source[length] == '\\') return false;
        destination[length] = source[length];
        ++length;
    }
    if (length == 0U || source[length] != '\0') return false;
    destination[length] = '\0';
    return true;
}

void process_init(void) {
    for (size_t i = 0; i < NEXUS_PROCESS_MAX; ++i) clear_slot(&processes[i]);
    next_pid = 1U;
}

bool process_spawn_elf(const char *name, const void *image, size_t image_size,
                       uint32_t *pid) {
    if (image == NULL || image_size == 0U ||
        image_size > PROCESS_MAX_IMAGE_SIZE || pid == NULL) return false;

    char process_name[NEXUS_PROCESS_NAME_MAX];
    if (!copy_process_name(process_name, name)) return false;

    struct nexus_elf64_image parsed;
    if (!nexus_elf64_parse(image, image_size, &parsed)) return false;

    struct nexus_process *process = find_available_slot();
    if (process == NULL) return false;

    uint64_t address_space;
    uint64_t entry_point;
    uint64_t stack_pointer;
    if (!nexus_elf64_load(image, image_size, &address_space,
                          &entry_point, &stack_pointer)) {
        clear_slot(process);
        return false;
    }

    process->pid = next_pid++;
    if (next_pid == 0U) next_pid = 1U;
    if (process->pid == 0U) {
        process->pid = next_pid++;
        if (next_pid == 0U) next_pid = 1U;
    }

    process->state = NEXUS_PROCESS_READY;
    process->address_space = address_space;
    process->entry_point = entry_point;
    process->stack_pointer = stack_pointer;
    process->exit_status = UINT64_MAX;
    for (size_t i = 0; i < NEXUS_PROCESS_NAME_MAX; ++i) {
        process->name[i] = process_name[i];
    }
    *pid = process->pid;
    return true;
}

bool process_run(uint32_t pid, uint64_t *exit_status) {
    if (exit_status == NULL) return false;
    struct nexus_process *process = find_process(pid);
    if (process == NULL || process->state != NEXUS_PROCESS_READY) return false;

    process->state = NEXUS_PROCESS_RUNNING;
    const uint64_t kernel_stack_top = (uint64_t)(uintptr_t)
        (process->kernel_stack + sizeof(process->kernel_stack));
    uint64_t status = UINT64_MAX;
    const bool executed =
        usermode_execute(process->address_space, process->entry_point,
                         process->stack_pointer, kernel_stack_top, &status);
    const bool reclaimed =
        paging_destroy_user_address_space(process->address_space);
    process->address_space = 0U;
    process->exit_status = status;
    *exit_status = status;

    if (!executed || !reclaimed) {
        process->state = NEXUS_PROCESS_FAILED;
        return false;
    }
    process->state = NEXUS_PROCESS_EXITED;
    return true;
}

bool process_execute_file(const char *name,
                          struct nexus_process_result *result,
                          enum nexus_process_error *error) {
    if (error != NULL) *error = NEXUS_PROCESS_ERROR_NONE;
    if (result == NULL || name == NULL) {
        if (error != NULL) *error = NEXUS_PROCESS_ERROR_NOT_FOUND;
        return false;
    }
    if (!vfs_root_mounted()) {
        if (error != NULL) *error = NEXUS_PROCESS_ERROR_NO_VOLUME;
        return false;
    }

    struct nexus_vfs_file file;
    if (!vfs_open_readonly(name, &file)) {
        if (error != NULL) *error = NEXUS_PROCESS_ERROR_NOT_FOUND;
        return false;
    }

    uint32_t file_size = 0U;
    if (!vfs_file_size(&file, &file_size)) {
        vfs_close(&file);
        if (error != NULL) *error = NEXUS_PROCESS_ERROR_IO;
        return false;
    }
    if (file_size == 0U || file_size > PROCESS_MAX_IMAGE_SIZE) {
        vfs_close(&file);
        if (error != NULL) *error = NEXUS_PROCESS_ERROR_TOO_LARGE;
        return false;
    }

    size_t total_read = 0U;
    while (total_read < file_size) {
        const size_t remaining = (size_t)file_size - total_read;
        const size_t amount = remaining > NEXUS_VFS_READ_CHUNK
            ? NEXUS_VFS_READ_CHUNK : remaining;
        size_t bytes_read = 0U;
        if (!vfs_read(&file, process_image + total_read, amount,
                      &bytes_read) || bytes_read == 0U) {
            vfs_close(&file);
            if (error != NULL) *error = NEXUS_PROCESS_ERROR_IO;
            return false;
        }
        total_read += bytes_read;
    }
    vfs_close(&file);

    if (!has_available_slot()) {
        if (error != NULL) *error = NEXUS_PROCESS_ERROR_TABLE_FULL;
        return false;
    }

    uint32_t pid = 0U;
    if (!process_spawn_elf(name, process_image, total_read, &pid)) {
        if (error != NULL) *error = NEXUS_PROCESS_ERROR_BAD_ELF;
        return false;
    }

    uint64_t exit_status = UINT64_MAX;
    if (!process_run(pid, &exit_status)) {
        if (error != NULL) *error = NEXUS_PROCESS_ERROR_EXECUTION;
        return false;
    }
    result->pid = pid;
    result->exit_status = exit_status;
    return true;
}

size_t process_count(void) {
    size_t count = 0U;
    for (size_t i = 0; i < NEXUS_PROCESS_MAX; ++i) {
        if (processes[i].state != NEXUS_PROCESS_FREE) ++count;
    }
    return count;
}

bool process_get_info(size_t slot, struct nexus_process_info *info) {
    if (slot >= NEXUS_PROCESS_MAX || info == NULL ||
        processes[slot].state == NEXUS_PROCESS_FREE) {
        return false;
    }
    info->pid = processes[slot].pid;
    info->state = processes[slot].state;
    info->exit_status = processes[slot].exit_status;
    for (size_t i = 0; i < NEXUS_PROCESS_NAME_MAX; ++i) {
        info->name[i] = processes[slot].name[i];
    }
    return true;
}

bool process_reap(uint32_t pid) {
    struct nexus_process *process = find_process(pid);
    if (process == NULL ||
        (process->state != NEXUS_PROCESS_EXITED &&
         process->state != NEXUS_PROCESS_FAILED)) {
        return false;
    }
    clear_slot(process);
    return true;
}

const char *process_state_name(enum nexus_process_state state) {
    switch (state) {
        case NEXUS_PROCESS_READY: return "READY";
        case NEXUS_PROCESS_RUNNING: return "RUNNING";
        case NEXUS_PROCESS_EXITED: return "EXITED";
        case NEXUS_PROCESS_FAILED: return "FAILED";
        default: return "FREE";
    }
}
