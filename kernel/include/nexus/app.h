#ifndef NEXUS_APP_H
#define NEXUS_APP_H

/*
 * Nexus Native Application SDK — x86-64, static ET_EXEC ABI.
 *
 * Build applications as freestanding static x86-64 ELF executables. The
 * initial runtime is single-foreground-process and has no POSIX/libc or
 * dynamic-linker guarantee. All syscall pointers must reference app memory.
 */
#include <stddef.h>
#include <stdint.h>

#define NEXUS_APP_ABI_VERSION 1U
#define NEXUS_APP_MAX_SLEEP_TICKS 500U

#define NEXUS_APP_SYS_EXIT 1ULL
#define NEXUS_APP_SYS_WRITE 2ULL
#define NEXUS_APP_SYS_OPEN 3ULL
#define NEXUS_APP_SYS_READ 4ULL
#define NEXUS_APP_SYS_CLOSE 5ULL
#define NEXUS_APP_SYS_FILE_WRITE 6ULL
#define NEXUS_APP_SYS_ABI_VERSION 7ULL
#define NEXUS_APP_SYS_INPUT_POLL 8ULL
#define NEXUS_APP_SYS_DISPLAY_INFO 9ULL
#define NEXUS_APP_SYS_CLOCK_TICKS 10ULL
#define NEXUS_APP_SYS_FRAMEBUFFER_BLIT 11ULL
#define NEXUS_APP_SYS_SLEEP_TICKS 12ULL
#define NEXUS_APP_SYS_GAMEPAD_POLL 13ULL
#define NEXUS_APP_SYS_AUDIO_WRITE 14ULL
#define NEXUS_APP_SYS_CPU_INFO 15ULL
#define NEXUS_APP_SYS_PERMISSIONS 16ULL

/* Signed per-application capability requests; all unspecified capabilities
 * are denied. Permission bits are stable across ABI minor revisions.
 */
#define NEXUS_APP_PERMISSION_FILE_READ (1U << 0)
#define NEXUS_APP_PERMISSION_FILE_WRITE (1U << 1)
#define NEXUS_APP_PERMISSION_INPUT (1U << 2)
#define NEXUS_APP_PERMISSION_DISPLAY (1U << 3)
#define NEXUS_APP_PERMISSION_AUDIO (1U << 4)
#define NEXUS_APP_PERMISSION_GAMEPAD (1U << 5)
#define NEXUS_APP_PERMISSION_ALL (NEXUS_APP_PERMISSION_FILE_READ | \
    NEXUS_APP_PERMISSION_FILE_WRITE | NEXUS_APP_PERMISSION_INPUT | \
    NEXUS_APP_PERMISSION_DISPLAY | NEXUS_APP_PERMISSION_AUDIO | \
    NEXUS_APP_PERMISSION_GAMEPAD)

#define NEXUS_APP_INPUT_KEYBOARD 1U
#define NEXUS_APP_INPUT_MOUSE 2U
#define NEXUS_APP_PIXEL_XRGB8888 1U
#define NEXUS_APP_FRAME_MAX_PIXELS 16384U
#define NEXUS_APP_EIO 5LL
#define NEXUS_APP_EACCES 13LL
#define NEXUS_APP_EAGAIN 11LL
#define NEXUS_APP_EFAULT 14LL
#define NEXUS_APP_ENODEV 19LL
#define NEXUS_APP_EINVAL 22LL
#define NEXUS_APP_KEY_RELEASE 0x0080U
#define NEXUS_APP_KEY_EXTENDED 0x0100U

struct nexus_app_input_event {
    uint8_t type;
    uint8_t reserved0;
    uint16_t scancode;
    uint8_t mouse_buttons;
    uint8_t reserved1;
    int16_t mouse_dx;
    int16_t mouse_dy;
    int8_t mouse_wheel;
    uint8_t reserved2;
};


#define NEXUS_APP_GAMEPAD_MAX_AXES 8U
#define NEXUS_APP_GAMEPAD_HAT_NEUTRAL 0xFFU
#define NEXUS_APP_GAMEPAD_DISCONNECTED 0x0001U
#define NEXUS_APP_AUDIO_SAMPLE_RATE_HZ 48000U
#define NEXUS_APP_AUDIO_CHANNELS 2U
#define NEXUS_APP_AUDIO_MAX_FRAMES 1024U

struct nexus_app_gamepad_event {
    uint16_t device_id;
    uint16_t vendor_id;
    uint16_t product_id;
    uint16_t flags;
    uint32_t buttons;
    uint8_t axis_count;
    uint8_t hat;
    uint8_t reserved2[2];
    int16_t axes[NEXUS_APP_GAMEPAD_MAX_AXES];
};

struct nexus_app_display_info {
    uint32_t structure_size;
    uint32_t abi_version;
    uint64_t width;
    uint64_t height;
    uint64_t stride_bytes;
    uint32_t pixel_format;
    uint32_t reserved;
};

#define NEXUS_APP_CPU_SSE2   (1ULL << 0)
#define NEXUS_APP_CPU_SSE42  (1ULL << 1)
#define NEXUS_APP_CPU_AES    (1ULL << 2)
#define NEXUS_APP_CPU_AVX    (1ULL << 3)
#define NEXUS_APP_CPU_AVX2   (1ULL << 4)
#define NEXUS_APP_CPU_FMA    (1ULL << 5)
#define NEXUS_APP_CPU_BMI1   (1ULL << 6)
#define NEXUS_APP_CPU_BMI2   (1ULL << 7)
#define NEXUS_APP_CPU_SHA    (1ULL << 8)
#define NEXUS_APP_CPU_SMP    (1ULL << 9)

struct nexus_app_cpu_info {
    uint32_t structure_size;
    uint32_t abi_version;
    uint64_t feature_flags;
    uint32_t physical_cores;
    uint32_t logical_processors;
    uint32_t online_processors;
    uint32_t reserved;
    char vendor[13];
    char brand[49];
};

struct nexus_app_blit_request {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
    uint32_t stride_pixels;
    uint32_t reserved;
    uint64_t pixels;
};

static inline int64_t nexus_app_syscall3(uint64_t number,
                                         uint64_t arg0,
                                         uint64_t arg1,
                                         uint64_t arg2) {
    uint64_t result;
    __asm__ volatile ("int $0x80"
                      : "=a"(result)
                      : "0"(number), "D"(arg0), "S"(arg1), "d"(arg2)
                      : "rcx", "r11", "memory", "cc");
    return (int64_t)result;
}

static inline int64_t nexus_app_abi_version(void) {
    return nexus_app_syscall3(NEXUS_APP_SYS_ABI_VERSION, 0U, 0U, 0U);
}

/* Returns the immutable capability mask granted by the signed package. */
static inline uint32_t nexus_app_permissions(void) {
    return (uint32_t)nexus_app_syscall3(
        NEXUS_APP_SYS_PERMISSIONS, 0U, 0U, 0U);
}

static inline void nexus_app_exit(uint64_t status) {
    (void)nexus_app_syscall3(NEXUS_APP_SYS_EXIT, status, 0U, 0U);
    for (;;) __asm__ volatile ("pause");
}

static inline int64_t nexus_app_write(uint64_t descriptor,
                                      const void *data, size_t length) {
    return nexus_app_syscall3(NEXUS_APP_SYS_WRITE, descriptor,
                              (uint64_t)(uintptr_t)data, length);
}

static inline int64_t nexus_app_open(const char *path) {
    return nexus_app_syscall3(NEXUS_APP_SYS_OPEN,
                              (uint64_t)(uintptr_t)path, 0U, 0U);
}

static inline int64_t nexus_app_read(int64_t descriptor,
                                     void *buffer, size_t length) {
    return nexus_app_syscall3(NEXUS_APP_SYS_READ, (uint64_t)descriptor,
                              (uint64_t)(uintptr_t)buffer, length);
}

static inline int64_t nexus_app_close(int64_t descriptor) {
    return nexus_app_syscall3(NEXUS_APP_SYS_CLOSE, (uint64_t)descriptor,
                              0U, 0U);
}

static inline int64_t nexus_app_file_write(const char *path,
                                           const void *data, size_t length) {
    return nexus_app_syscall3(NEXUS_APP_SYS_FILE_WRITE,
                              (uint64_t)(uintptr_t)path,
                              (uint64_t)(uintptr_t)data, length);
}

/* Returns 1 for an event, 0 when the queue is empty, or a negative errno. */
static inline int64_t nexus_app_input_poll(struct nexus_app_input_event *event) {
    return nexus_app_syscall3(NEXUS_APP_SYS_INPUT_POLL,
                              (uint64_t)(uintptr_t)event, 0U, 0U);
}

/* Returns 1 for a controller event, 0 for no pending event, or -errno. */
static inline int64_t nexus_app_gamepad_poll(
    struct nexus_app_gamepad_event *event) {
    return nexus_app_syscall3(NEXUS_APP_SYS_GAMEPAD_POLL,
                              (uint64_t)(uintptr_t)event, 0U, 0U);
}

/* Queue up to 1024 interleaved signed 16-bit stereo frames at 48 kHz.
 * Returns 0 when accepted, -NEXUS_APP_EAGAIN when the single DMA buffer is
 * still busy, -NEXUS_APP_ENODEV when no audio device is present, -EFAULT for
 * an invalid pointer, or -NEXUS_APP_EINVAL for an invalid frame count.
 */
static inline int64_t nexus_app_audio_write_48k_s16_stereo(
    const int16_t *interleaved_samples, uint32_t sample_frames) {
    return nexus_app_syscall3(NEXUS_APP_SYS_AUDIO_WRITE,
        (uint64_t)(uintptr_t)interleaved_samples, sample_frames, 0U);
}

static inline int64_t nexus_app_get_display(
    struct nexus_app_display_info *info) {
    return nexus_app_syscall3(NEXUS_APP_SYS_DISPLAY_INFO,
                              (uint64_t)(uintptr_t)info, 0U, 0U);
}

/* Returns zero on success or a negative errno value. */
static inline int64_t nexus_app_get_cpu_info(
    struct nexus_app_cpu_info *info) {
    return nexus_app_syscall3(NEXUS_APP_SYS_CPU_INFO,
                              (uint64_t)(uintptr_t)info, 0U, 0U);
}

/* Monotonic timer ticks; applications should measure deltas, not assume Hz. */
static inline int64_t nexus_app_ticks(void) {
    return nexus_app_syscall3(NEXUS_APP_SYS_CLOCK_TICKS, 0U, 0U, 0U);
}

/*
 * Block this application context for at least the requested timer ticks.
 * Sleeping yields execution to other scheduler work instead of busy-waiting.
 * The current timer runs at 100 Hz and the maximum request is five seconds.
 * Returns zero on success or a negative errno value.
 */
static inline int64_t nexus_app_sleep_ticks(uint64_t ticks) {
    return nexus_app_syscall3(NEXUS_APP_SYS_SLEEP_TICKS, ticks, 0U, 0U);
}

/* pixels are packed 0x00RRGGBB; source stride is measured in uint32_t pixels. */
static inline int64_t nexus_app_blit(uint32_t x, uint32_t y,
                                     uint32_t width, uint32_t height,
                                     const uint32_t *pixels,
                                     uint32_t stride_pixels) {
    const struct nexus_app_blit_request request = {
        .x = x, .y = y, .width = width, .height = height,
        .stride_pixels = stride_pixels, .reserved = 0U,
        .pixels = (uint64_t)(uintptr_t)pixels
    };
    return nexus_app_syscall3(NEXUS_APP_SYS_FRAMEBUFFER_BLIT,
                              (uint64_t)(uintptr_t)&request, 0U, 0U);
}

#endif
