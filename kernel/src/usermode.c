#include "usermode.h"

#include "elf64.h"
#include "ac97.h"
#include "cpu.h"
#include "smp.h"
#include "fpu.h"
#include "gdt.h"
#include "graphics.h"
#include "input.h"
#include "nexus/app.h"
#include "paging.h"
#include "process.h"
#include "serial.h"
#include "syscall.h"
#include "timer.h"
#include "vfs.h"
#include "virtio_gpu.h"
#include "xhci.h"

#include <stddef.h>
#include <stdint.h>

#define USER_CODE_VA 0x00400000ULL
#define USER_EXIT_STATUS 42U
#define TEST_ELF_FILE_CAPACITY 8192U
#define USERMODE_FILE_SLOTS 8U
#define USERMODE_FD_BASE 3U

static uint8_t audio_staging[NEXUS_AC97_MAX_BYTES];

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
volatile bool usermode_exit_seen;
volatile bool usermode_write_rejected;

extern void nexus_enter_user(uint64_t root_physical,
                             uint64_t entry_point,
                             uint64_t stack_pointer,
                             uint64_t kernel_stack_top);

static struct nexus_vfs_file usermode_files[USERMODE_FILE_SLOTS];
static bool usermode_file_used[USERMODE_FILE_SLOTS];
static char usermode_output_buffer[NEXUS_USERMODE_OUTPUT_MAX];
static size_t usermode_output_length;
static bool usermode_output_truncated;

static void usermode_output_reset(void) {
    usermode_output_length = 0U;
    usermode_output_truncated = false;
    usermode_output_buffer[0] = '\0';
}

static void usermode_output_append(const char *data, size_t length) {
    if (data == NULL || length == 0U) return;
    const size_t available =
        sizeof(usermode_output_buffer) - 1U - usermode_output_length;
    const size_t amount = length < available ? length : available;
    for (size_t i = 0U; i < amount; ++i)
        usermode_output_buffer[usermode_output_length + i] = data[i];
    usermode_output_length += amount;
    usermode_output_buffer[usermode_output_length] = '\0';
    if (amount != length) usermode_output_truncated = true;
}

size_t usermode_console_output_copy(char *destination, size_t capacity,
                                    bool *truncated) {
    if (truncated != NULL) *truncated = usermode_output_truncated;
    if (destination == NULL || capacity == 0U) return 0U;
    size_t amount = usermode_output_length;
    if (amount >= capacity) amount = capacity - 1U;
    for (size_t i = 0U; i < amount; ++i)
        destination[i] = usermode_output_buffer[i];
    destination[amount] = '\0';
    if (truncated != NULL && amount < usermode_output_length)
        *truncated = true;
    return amount;
}
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

    /* open("../NEXUS") must reject path traversal with -EINVAL. */
    0x48, 0xC7, 0xC0, 0x03, 0x00, 0x00, 0x00,
    0x48, 0xC7, 0xC7, 0x00, 0x00, 0x00, 0x00,
    0xCD, 0x80,
    0x48, 0x83, 0xF8, 0xEA, 0x74, 0x02, 0xEB, 0xFE,

    /* read(3, ..., 1) on an unused descriptor must return -EBADF. */
    0x48, 0xC7, 0xC0, 0x04, 0x00, 0x00, 0x00,
    0x48, 0xC7, 0xC7, 0x03, 0x00, 0x00, 0x00,
    0xCD, 0x80,
    0x48, 0x83, 0xF8, 0xF7, 0x74, 0x02, 0xEB, 0xFE,

    /* close(3) on an unused descriptor must return -EBADF. */
    0x48, 0xC7, 0xC0, 0x05, 0x00, 0x00, 0x00,
    0x48, 0xC7, 0xC7, 0x03, 0x00, 0x00, 0x00,
    0xCD, 0x80,
    0x48, 0x83, 0xF8, 0xF7, 0x74, 0x02, 0xEB, 0xFE,

    /* Execute a small SSE2 round-trip before returning to the kernel. */
    0xB8, 0x2A, 0x00, 0x00, 0x00,             /* mov eax, 42 */
    0x66, 0x0F, 0x6E, 0xC0,                   /* movd eax, xmm0 */
    0x66, 0x0F, 0xEF, 0xC9,                   /* pxor xmm1, xmm1 */
    0x66, 0x0F, 0x7E, 0xC0,                   /* movd xmm0, eax */
    0x83, 0xF8, 0x2A,                         /* cmp eax, 42 */
    0x74, 0x02, 0xEB, 0xFE,                   /* jne spin */
    
    /* exit(42) */
    0x48, 0xC7, 0xC0, 0x01, 0x00, 0x00, 0x00,
    0x48, 0xC7, 0xC7, 0x2A, 0x00, 0x00, 0x00,
    0xCD, 0x80,
    0xEB, 0xFE
};

static const char test_message[] = "NEXUS USER MODE";
static const char test_invalid_path[] = "../NEXUS";
static const size_t test_message_immediate_offset = 75U;
static const size_t test_path_immediate_offset = 98U;

static bool build_test_elf(size_t *file_size) {
    if (file_size == NULL) return false;

    for (size_t i = 0; i < sizeof(usermode_test_elf); ++i) {
        usermode_test_elf[i] = 0;
    }

    const size_t message_offset = sizeof(test_program);
    const size_t message_file_size = sizeof(test_message) - 1U;
    const size_t path_file_size = sizeof(test_invalid_path);
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
    for (size_t i = 0; i < sizeof(test_invalid_path); ++i) {
        usermode_test_elf[segment_file_offset + path_offset + i] =
            (uint8_t)test_invalid_path[i];
    }

    *file_size = total_size;
    return true;
}

static void usermode_close_all_files(void) {
    for (size_t i = 0; i < USERMODE_FILE_SLOTS; ++i) {
        if (usermode_file_used[i]) vfs_close(&usermode_files[i]);
        usermode_file_used[i] = false;
    }
}

bool usermode_execute(uint64_t root_physical, uint64_t entry_point,
                      uint64_t stack_pointer, uint64_t kernel_stack_top,
                      uint64_t *exit_status) {
    if (root_physical == 0U || entry_point == 0U ||
        stack_pointer == 0U || kernel_stack_top == 0U ||
        (kernel_stack_top & 0x0FU) != 0U || exit_status == NULL ||
        !nexus_fpu_available()) {
        return false;
    }

    const uint64_t previous_kernel_stack = gdt_get_kernel_stack();
    if (previous_kernel_stack == 0U ||
        !gdt_set_kernel_stack(kernel_stack_top)) {
        return false;
    }

    if (!nexus_fpu_user_begin()) {
        (void)gdt_set_kernel_stack(previous_kernel_stack);
        return false;
    }

    usermode_close_all_files();
    usermode_output_reset();
    usermode_exit_status = UINT64_MAX;
    usermode_exit_seen = false;
    usermode_write_rejected = false;

    nexus_enter_user(root_physical, entry_point, stack_pointer,
                     kernel_stack_top);

    const bool exited = usermode_exit_seen;
    const uint64_t status = usermode_exit_status;
    const bool fpu_restored = nexus_fpu_user_end();
    usermode_close_all_files();
    const bool stack_restored = gdt_set_kernel_stack(previous_kernel_stack);
    *exit_status = status;
    return exited && stack_restored && fpu_restored;
}

bool usermode_run_selftest(void) {
    size_t elf_file_size;
    if (!build_test_elf(&elf_file_size)) return false;

    uint32_t pid = 0U;
    if (!process_spawn_elf("BOOTTEST", usermode_test_elf, elf_file_size, &pid)) {
        return false;
    }

    uint64_t exit_status = UINT64_MAX;
    const bool executed = process_run(pid, &exit_status);
    char captured_output[sizeof(test_message)] = {0};
    bool output_truncated = false;
    const size_t output_length = usermode_console_output_copy(
        captured_output, sizeof(captured_output), &output_truncated);
    bool output_matches = output_length == sizeof(test_message) - 1U &&
                          !output_truncated;
    for (size_t i = 0U; i < output_length && output_matches; ++i) {
        if (captured_output[i] != test_message[i]) output_matches = false;
    }
    const bool result_ok = executed && exit_status == USER_EXIT_STATUS &&
                           usermode_write_rejected && output_matches;
    return process_reap(pid) && result_ok;
}

bool usermode_sys_write(uint64_t user_address, uint64_t length) {
    static char kernel_buffer[128U];
    if (length == 0U) return true;
    if (length > sizeof(kernel_buffer) ||
        !paging_copy_from_user(kernel_buffer, user_address, (size_t)length)) {
        return false;
    }

    /* Never dereference a userspace pointer directly in ring 0. */
    usermode_output_append(kernel_buffer, (size_t)length);
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
    if (!vfs_has_filesystem()) return -NEXUS_ENODEV;

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

int64_t usermode_sys_file_write(uint64_t user_path,
                                uint64_t user_buffer, uint64_t length) {
    char path[NEXUS_VFS_NAME_MAX];
    static uint8_t write_buffer[NEXUS_VFS_RAM_MAX_FILE_SIZE];

    if (!paging_copy_from_user(path, user_path, sizeof(path)))
        return -NEXUS_EFAULT;
    size_t path_length = 0U;
    while (path_length < sizeof(path) && path[path_length] != '\0') {
        if (path[path_length] == '/' || path[path_length] == '\\')
            return -NEXUS_EINVAL;
        ++path_length;
    }
    if (path_length == 0U) return -NEXUS_EINVAL;
    if (path_length == sizeof(path)) return -NEXUS_ENAMETOOLONG;
    if (length > sizeof(write_buffer)) return -NEXUS_EFBIG;
    if (length != 0U &&
        !paging_copy_from_user(write_buffer, user_buffer, (size_t)length))
        return -NEXUS_EFAULT;
    if (!vfs_write_file(path, write_buffer, (size_t)length))
        return -NEXUS_EIO;
    return (int64_t)length;
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

int64_t usermode_sys_input_poll(uint64_t user_event) {
    if (!paging_user_range_writable(user_event,
                                    sizeof(struct nexus_app_input_event))) {
        return -NEXUS_EFAULT;
    }

    /* xHCI HID reports are drained by polling rather than an IRQ handler. */
    xhci_poll();
    struct nexus_input_event event;
    if (!input_pop_event(&event)) return 0;

    const struct nexus_app_input_event result = {
        .type = event.type,
        .reserved0 = 0U,
        .scancode = event.scancode,
        .mouse_buttons = event.mouse_buttons,
        .reserved1 = 0U,
        .mouse_dx = event.mouse_dx,
        .mouse_dy = event.mouse_dy,
        .mouse_wheel = event.mouse_wheel,
        .reserved2 = 0U
    };
    return paging_copy_to_user(user_event, &result, sizeof(result))
        ? 1 : -NEXUS_EFAULT;
}

int64_t usermode_sys_audio_write(uint64_t user_samples,
                                 uint64_t sample_frames) {
    if (sample_frames == 0U ||
        sample_frames > NEXUS_AC97_MAX_FRAMES)
        return -NEXUS_EINVAL;
    const size_t bytes = (size_t)sample_frames * 4U;
    if (!paging_copy_from_user(audio_staging, user_samples, bytes))
        return -NEXUS_EFAULT;
    const enum nexus_ac97_submit_result result =
        ac97_write_pcm_s16_stereo_48k(
            (const int16_t *)audio_staging, (uint32_t)sample_frames);
    switch (result) {
        case NEXUS_AC97_SUBMIT_OK: return 0;
        case NEXUS_AC97_SUBMIT_BUSY: return -NEXUS_EAGAIN;
        case NEXUS_AC97_SUBMIT_NOT_READY: return -NEXUS_ENODEV;
        case NEXUS_AC97_SUBMIT_INVALID: return -NEXUS_EINVAL;
        default: return -NEXUS_EIO;
    }
}

int64_t usermode_sys_gamepad_poll(uint64_t user_event) {
    if (!paging_user_range_writable(
            user_event, sizeof(struct nexus_app_gamepad_event)))
        return -NEXUS_EFAULT;
    xhci_poll();
    struct nexus_usb_gamepad_event event;
    if (!input_pop_gamepad_event(&event)) return 0;
    _Static_assert(sizeof(struct nexus_usb_gamepad_event) ==
                   sizeof(struct nexus_app_gamepad_event),
                   "kernel and SDK gamepad event layouts must match");
    return paging_copy_to_user(user_event, &event, sizeof(event))
        ? 1 : -NEXUS_EFAULT;
}

int64_t usermode_sys_display_info(uint64_t user_info) {
    const uint64_t width = graphics_width();
    const uint64_t height = graphics_height();
    const uint64_t pitch = graphics_pitch();
    if (width == 0U || height == 0U || pitch == 0U)
        return -NEXUS_ENODEV;
    const struct nexus_app_display_info info = {
        .structure_size = sizeof(struct nexus_app_display_info),
        .abi_version = NEXUS_APP_ABI_VERSION,
        .width = width,
        .height = height,
        .stride_bytes = pitch,
        .pixel_format = NEXUS_APP_PIXEL_XRGB8888,
        .reserved = 0U
    };
    if (!paging_user_range_writable(user_info, sizeof(info)))
        return -NEXUS_EFAULT;
    return paging_copy_to_user(user_info, &info, sizeof(info))
        ? 0 : -NEXUS_EFAULT;
}

int64_t usermode_sys_cpu_info(uint64_t user_info) {
    const struct nexus_cpu_info *cpu = nexus_cpu_get_info();
    if (cpu == NULL || !cpu->cpuid_available) return -NEXUS_ENODEV;

    uint64_t features = 0U;
    if (nexus_cpu_has_feature(NEXUS_CPU_FEATURE_SSE2))
        features |= NEXUS_APP_CPU_SSE2;
    if (nexus_cpu_has_feature(NEXUS_CPU_FEATURE_SSE42))
        features |= NEXUS_APP_CPU_SSE42;
    if (nexus_cpu_has_feature(NEXUS_CPU_FEATURE_AES))
        features |= NEXUS_APP_CPU_AES;
    if (nexus_cpu_has_feature(NEXUS_CPU_FEATURE_AVX) &&
        nexus_fpu_avx_available())
        features |= NEXUS_APP_CPU_AVX;
    if (nexus_cpu_has_feature(NEXUS_CPU_FEATURE_AVX2) &&
        nexus_fpu_avx_available())
        features |= NEXUS_APP_CPU_AVX2;
    if (nexus_cpu_has_feature(NEXUS_CPU_FEATURE_FMA) &&
        nexus_fpu_avx_available())
        features |= NEXUS_APP_CPU_FMA;
    if (nexus_cpu_has_feature(NEXUS_CPU_FEATURE_BMI1))
        features |= NEXUS_APP_CPU_BMI1;
    if (nexus_cpu_has_feature(NEXUS_CPU_FEATURE_BMI2))
        features |= NEXUS_APP_CPU_BMI2;
    if (nexus_cpu_has_feature(NEXUS_CPU_FEATURE_SHA))
        features |= NEXUS_APP_CPU_SHA;
    if (nexus_smp_online_cpu_count() > 1U)
        features |= NEXUS_APP_CPU_SMP;

    struct nexus_app_cpu_info info = {
        .structure_size = sizeof(struct nexus_app_cpu_info),
        .abi_version = NEXUS_APP_ABI_VERSION,
        .feature_flags = features,
        .physical_cores = cpu->physical_cores_per_package,
        .logical_processors = cpu->logical_processors_per_package,
        .online_processors = nexus_smp_online_cpu_count()
    };
    for (size_t i = 0U; i < sizeof(info.vendor); ++i)
        info.vendor[i] = cpu->vendor[i];
    for (size_t i = 0U; i < sizeof(info.brand); ++i)
        info.brand[i] = cpu->brand[i];

    if (!paging_user_range_writable(user_info, sizeof(info)))
        return -NEXUS_EFAULT;
    return paging_copy_to_user(user_info, &info, sizeof(info))
        ? 0 : -NEXUS_EFAULT;
}

int64_t usermode_sys_clock_ticks(void) {
    return (int64_t)timer_ticks();
}

int64_t usermode_sys_framebuffer_blit(uint64_t user_request) {
    struct nexus_app_blit_request request;
    static uint32_t staging[NEXUS_APP_FRAME_MAX_PIXELS];

    if (!paging_copy_from_user(&request, user_request, sizeof(request)))
        return -NEXUS_EFAULT;
    if (request.reserved != 0U || request.width == 0U ||
        request.height == 0U || request.stride_pixels < request.width ||
        request.width > NEXUS_APP_FRAME_MAX_PIXELS ||
        request.height > NEXUS_APP_FRAME_MAX_PIXELS / request.width ||
        request.stride_pixels > NEXUS_APP_FRAME_MAX_PIXELS) {
        return -NEXUS_EINVAL;
    }

    const uint64_t display_width = graphics_width();
    const uint64_t display_height = graphics_height();
    if (display_width == 0U || display_height == 0U ||
        request.x >= display_width || request.y >= display_height) {
        return -NEXUS_ENODEV;
    }

    uint64_t copy_width = request.width;
    uint64_t copy_height = request.height;
    if (copy_width > display_width - request.x)
        copy_width = display_width - request.x;
    if (copy_height > display_height - request.y)
        copy_height = display_height - request.y;
    if (copy_width == 0U || copy_height == 0U ||
        copy_width > NEXUS_APP_FRAME_MAX_PIXELS / copy_height) {
        return -NEXUS_EINVAL;
    }

    const uint64_t source_stride = request.stride_pixels;
    for (uint64_t row = 0U; row < copy_height; ++row) {
        if (row != 0U && source_stride >
            (UINT64_MAX / sizeof(uint32_t)) / row) {
            return -NEXUS_EFAULT;
        }
        const uint64_t source_offset =
            row * source_stride * sizeof(uint32_t);
        if (request.pixels > UINT64_MAX - source_offset)
            return -NEXUS_EFAULT;
        if (!paging_copy_from_user(staging + row * copy_width,
                request.pixels + source_offset,
                (size_t)copy_width * sizeof(uint32_t))) {
            return -NEXUS_EFAULT;
        }
    }

    if (!graphics_blit_xrgb8888(request.x, request.y, copy_width,
            copy_height, staging, (size_t)copy_width)) {
        return -NEXUS_EINVAL;
    }
    if (virtio_gpu_active() &&
        !virtio_gpu_present_region(request.x, request.y,
                                   copy_width, copy_height)) {
        return -NEXUS_EIO;
    }
    return 0;
}

int64_t usermode_sys_sleep_ticks(uint64_t ticks) {
    /* At 100 Hz, 500 ticks caps one sleep request at five seconds. */
    if (ticks > 500U) return -NEXUS_EINVAL;
    if (ticks == 0U) return 0;

    const uint64_t start = timer_ticks();
    while (timer_ticks() - start < ticks) {
        /*
         * The interrupt gate enters with IF clear. STI;HLT allows timer,
         * keyboard, and mouse IRQs to run; CLI restores the syscall handler's
         * expected interrupt state immediately after wakeup.
         */
        __asm__ volatile ("sti; hlt; cli" : : : "memory");
    }
    return 0;
}
