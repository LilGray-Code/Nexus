#include "usermode.h"

#include "elf64.h"
#include "gdt.h"
#include "paging.h"
#include "serial.h"
#include "syscall.h"
#include "vfs.h"

#include <stddef.h>
#include <stdint.h>

#define USER_CODE_VA 0x00400000ULL
#define USER_EXIT_STATUS 42U
#define TEST_ELF_FILE_CAPACITY 8192U
#define USERMODE_FILE_SLOTS 8U
#define USERMODE_FD_BASE 3U

struct test_elf_header {
    uint8_t ident[16];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint64_t entry;
    uint64_t program_header_offset;
    uint64_t section_header_offset;
    uint32_t flags;
    uint16_t header_size;
    uint16_t program_header_entry_size;
    uint16_t program_header_count;
    uint16_t section_header_entry_size;
    uint16_t section_header_count;
    uint16_t section_name_index;
} __attribute__((packed));

struct test_elf_program_header {
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t virtual_address;
    uint64_t physical_address;
    uint64_t file_size;
    uint64_t memory_size;
    uint64_t alignment;
} __attribute__((packed));

uint64_t usermode_saved_cr3;
uintptr_t usermode_saved_rsp;
volatile uint64_t usermode_exit_status = UINT64_MAX;
volatile bool usermode_write_rejected;

extern void nexus_enter_user(uint64_t root_physical,
                             uint64_t entry_point,
                             uint64_t stack_pointer,
                             uint64_t kernel_stack_top);

static uint8_t usermode_kernel_stack[16384U] __attribute__((aligned(16)));
static struct nexus_vfs_file usermode_files[USERMODE_FILE_SLOTS];
static bool usermode_file_used[USERMODE_FILE_SLOTS];
static uint8_t usermode_test_elf[TEST_ELF_FILE_CAPACITY]
    __attribute__((aligned(16)));

static const uint8_t test_program[] = {
    /* Unsupported syscall must return -ENOSYS, not hang the system. */
    0x48, 0xC7, 0xC0, 0x63, 0x00, 0x00, 0x00,
    0xCD, 0x80,
    0x48, 0x83, 0xF8, 0xDA, 0x74, 0x02, 0xEB, 0xFE,

    /* write(1, kernel_pointer, 1) must return -EFAULT. */
    0x48, 0xC7, 0xC0, 0x02, 0x00, 0x00, 0x00,
    0x48, 0xC7, 0xC7, 0x01, 0x00, 0x00, 0x00,
    0x48, 0xBE, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xFF, 0xFF,
    0x48, 0xC7, 0xC2, 0x01, 0x00, 0x00, 0x00,
    0xCD, 0x80,
    0x48, 0x83, 0xF8, 0xF2, 0x74, 0x02, 0xEB, 0xFE,

    /* write(1, message, 15) must succeed. */
    0x48, 0xC7, 0xC0, 0x02, 0x00, 0x00, 0x00,
    0x48, 0xC7, 0xC7, 0x01, 0x00, 0x00, 0x00,
    0x48, 0xC7, 0xC6, 0x00, 0x00, 0x00, 0x00,
    0x48, 0xC7, 0xC2, 0x0F, 0x00, 0x00, 0x00,
    0xCD, 0x80,

    /* open("NOTHERE.NXS") must return -ENOENT or -ENODEV. */
    0x48, 0xC7, 0xC0, 0x03, 0x00, 0x00, 0x00,
    0x48, 0xC7, 0xC7, 0x00, 0x00, 0x00, 0x00,
    0xCD, 0x80,
    0x48, 0x83, 0xF8, 0xFE, 0x74, 0x08,
    0x48, 0x83, 0xF8, 0xED, 0x74, 0x02,
    0xEB, 0xFE,

    /* exit(42) */
    0x48, 0xC7, 0xC0, 0x01, 0x00, 0x00, 0x00,
    0x48, 0xC7, 0xC7, 0x2A, 0x00, 0x00, 0x00,
    0xCD, 0x80,
    0xEB, 0xFE
};

static const char test_message[] = "NEXUS USER MODE";
static const char test_missing_path[] = "NOTHERE.NXS";
static const size_t test_message_immediate_offset = 75U;
static const size_t test_path_immediate_offset = 98U;

static bool build_test_elf(size_t *file_size) {
    if (file_size == NULL) return false;

    for (size_t i = 0; i < sizeof(usermode_test_elf); ++i) {
        usermode_test_elf[i] = 0;
    }

    const size_t message_offset = sizeof(test_program);
    const size_t message_file_size = sizeof(test_message) - 1U;
    const size_t path_file_size = sizeof(test_missing_path);
    const size_t path_offset = sizeof(test_program) + message_file_size;
    const size_t segment_file_size =
        sizeof(test_program) + message_file_size + path_file_size;
    const size_t segment_file_offset = 0x1000U;
    const size_t total_size = segment_file_offset + segment_file_size;
    if (total_size > sizeof(usermode_test_elf) ||
        test_message_immediate_offset + sizeof(uint32_t) >
            sizeof(test_program) ||
        test_path_immediate_offset + sizeof(uint32_t) >
            sizeof(test_program)) {
        return false;
    }

    struct test_elf_header *header =
        (struct test_elf_header *)(void *)usermode_test_elf;
    header->ident[0] = 0x7FU;
    header->ident[1] = 'E';
    header->ident[2] = 'L';
    header->ident[3] = 'F';
    header->ident[4] = 2U;
    header->ident[5] = 1U;
    header->ident[6] = 1U;
    header->type = 2U;       /* ET_EXEC */
    header->machine = 62U;    /* EM_X86_64 */
    header->version = 1U;
    header->entry = USER_CODE_VA;
    header->program_header_offset = sizeof(*header);
    header->header_size = sizeof(*header);
    header->program_header_entry_size =
        sizeof(struct test_elf_program_header);
    header->program_header_count = 1U;

    struct test_elf_program_header *program =
        (struct test_elf_program_header *)(void *)(
            usermode_test_elf + sizeof(*header));
    program->type = 1U;              /* PT_LOAD */
    program->flags = 0x5U;           /* PF_R | PF_X */
    program->offset = segment_file_offset;
    program->virtual_address = USER_CODE_VA;
    program->file_size = segment_file_size;
    program->memory_size = 4096U;    /* Include zero-fill coverage. */
    program->alignment = 4096U;

    for (size_t i = 0; i < sizeof(test_program); ++i) {
        usermode_test_elf[segment_file_offset + i] = test_program[i];
    }
    const uint32_t message_address =
        (uint32_t)(USER_CODE_VA + message_offset);
    for (size_t i = 0; i < sizeof(message_address); ++i) {
        usermode_test_elf[segment_file_offset +
                          test_message_immediate_offset + i] =
            (uint8_t)(message_address >> (i * 8U));
    }
    const uint32_t path_address = (uint32_t)(USER_CODE_VA + path_offset);
    for (size_t i = 0; i < sizeof(path_address); ++i) {
        usermode_test_elf[segment_file_offset +
                          test_path_immediate_offset + i] =
            (uint8_t)(path_address >> (i * 8U));
    }
    for (size_t i = 0; i < sizeof(test_message) - 1U; ++i) {
        usermode_test_elf[segment_file_offset + message_offset + i] =
            (uint8_t)test_message[i];
    }
    for (size_t i = 0; i < sizeof(test_missing_path); ++i) {
        usermode_test_elf[segment_file_offset + path_offset + i] =
            (uint8_t)test_missing_path[i];
    }

    *file_size = total_size;
    return true;
}

bool usermode_run_selftest(void) {
    size_t elf_file_size;
    if (!build_test_elf(&elf_file_size)) return false;

    uint64_t user_root_physical;
    uint64_t entry_point;
    uint64_t stack_pointer;
    if (!nexus_elf64_load(usermode_test_elf, elf_file_size,
                          &user_root_physical, &entry_point,
                          &stack_pointer)) {
        return false;
    }

    const uint64_t kernel_stack_top = (uint64_t)(uintptr_t)
        (usermode_kernel_stack + sizeof(usermode_kernel_stack));
    if (!gdt_set_kernel_stack(kernel_stack_top)) return false;

    for (size_t i = 0; i < USERMODE_FILE_SLOTS; ++i) {
        if (usermode_file_used[i]) vfs_close(&usermode_files[i]);
        usermode_file_used[i] = false;
    }
    usermode_exit_status = UINT64_MAX;
    usermode_write_rejected = false;
    nexus_enter_user(user_root_physical, entry_point, stack_pointer,
                     kernel_stack_top);
    const bool selftest_ok =
        usermode_exit_status == USER_EXIT_STATUS && usermode_write_rejected;
    if (!paging_destroy_user_address_space(user_root_physical)) return false;
    return selftest_ok;
}

bool usermode_sys_write(uint64_t user_address, uint64_t length) {
    static char kernel_buffer[128U];
    if (length == 0U) return true;
    if (length > sizeof(kernel_buffer) ||
        !paging_copy_from_user(kernel_buffer, user_address, (size_t)length)) {
        return false;
    }

    /* Never dereference a userspace pointer directly in ring 0. */
    serial_write_n(kernel_buffer, (size_t)length);
    serial_write("\r\n");
    return true;
}


/* Read-only descriptor table for the current bootstrap user process. */
int64_t usermode_sys_open(uint64_t user_path) {
    char path[NEXUS_VFS_NAME_MAX];
    if (!paging_copy_from_user(path, user_path, sizeof(path))) {
        return -NEXUS_EFAULT;
    }

    size_t length = 0;
    while (length < sizeof(path) && path[length] != '\0') {
        if (path[length] == '/' || path[length] == '\\') return -NEXUS_EINVAL;
        ++length;
    }
    if (length == 0U) return -NEXUS_EINVAL;
    if (length == sizeof(path)) return -NEXUS_ENAMETOOLONG;
    if (!vfs_root_mounted()) return -NEXUS_ENODEV;

    size_t slot = USERMODE_FILE_SLOTS;
    for (size_t i = 0; i < USERMODE_FILE_SLOTS; ++i) {
        if (!usermode_file_used[i]) {
            slot = i;
            break;
        }
    }
    if (slot == USERMODE_FILE_SLOTS) return -NEXUS_EMFILE;
    if (!vfs_open_readonly(path, &usermode_files[slot])) return -NEXUS_ENOENT;

    usermode_file_used[slot] = true;
    return (int64_t)(USERMODE_FD_BASE + slot);
}

int64_t usermode_sys_read(uint64_t descriptor, uint64_t user_buffer,
                          uint64_t length) {
    if (descriptor < USERMODE_FD_BASE ||
        descriptor >= USERMODE_FD_BASE + USERMODE_FILE_SLOTS) {
        return -NEXUS_EBADF;
    }
    const size_t slot = (size_t)(descriptor - USERMODE_FD_BASE);
    if (!usermode_file_used[slot]) return -NEXUS_EBADF;
    if (length == 0U) return 0;
    if (length > NEXUS_VFS_READ_CHUNK) return -NEXUS_EINVAL;
    if (!paging_user_range_writable(user_buffer, (size_t)length)) {
        return -NEXUS_EFAULT;
    }

    static uint8_t read_buffer[NEXUS_VFS_READ_CHUNK];
    size_t bytes_read = 0;
    if (!vfs_read(&usermode_files[slot], read_buffer, (size_t)length,
                  &bytes_read)) {
        return -NEXUS_EIO;
    }
    if (!paging_copy_to_user(user_buffer, read_buffer, bytes_read)) {
        return -NEXUS_EFAULT;
    }
    return (int64_t)bytes_read;
}

int64_t usermode_sys_close(uint64_t descriptor) {
    if (descriptor < USERMODE_FD_BASE ||
        descriptor >= USERMODE_FD_BASE + USERMODE_FILE_SLOTS) {
        return -NEXUS_EBADF;
    }
    const size_t slot = (size_t)(descriptor - USERMODE_FD_BASE);
    if (!usermode_file_used[slot]) return -NEXUS_EBADF;
    vfs_close(&usermode_files[slot]);
    usermode_file_used[slot] = false;
    return 0;
}
