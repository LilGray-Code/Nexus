#ifndef NEXUS_USERLAND_SYSCALL_H
#define NEXUS_USERLAND_SYSCALL_H

#include <stddef.h>
#include <stdint.h>

/*
 * Nexus x86-64 user ABI. Invoke int 0x80 with:
 *   RAX = syscall number
 *   RDI, RSI, RDX = arguments
 *   RAX = result (negative values are -errno)
 *
 * This is a small freestanding ABI header, not a POSIX libc replacement.
 */
#define NEXUS_SYS_EXIT       1ULL
#define NEXUS_SYS_WRITE      2ULL
#define NEXUS_SYS_OPEN       3ULL
#define NEXUS_SYS_READ       4ULL
#define NEXUS_SYS_CLOSE      5ULL
#define NEXUS_SYS_FILE_WRITE 6ULL
#define NEXUS_SYS_ABI_VERSION 7ULL
#define NEXUS_SYS_INPUT_POLL 8ULL
#define NEXUS_SYS_DISPLAY_INFO 9ULL
#define NEXUS_SYS_CLOCK_TICKS 10ULL
#define NEXUS_SYS_FRAMEBUFFER_BLIT 11ULL
#define NEXUS_SYS_SLEEP_TICKS 12ULL
#define NEXUS_SYS_GAMEPAD_POLL 13ULL
#define NEXUS_SYS_AUDIO_WRITE 14ULL
#define NEXUS_SYS_CPU_INFO 15ULL
#define NEXUS_SYS_APP_PERMISSIONS 16ULL
#define NEXUS_SYSCALL_ABI_VERSION 1ULL

/* CPU capabilities exposed to apps; AVX flags mean OS XSAVE/YMM state is live. */
#define NEXUS_CPU_FEATURE_SSE2   (1ULL << 0)
#define NEXUS_CPU_FEATURE_SSE42  (1ULL << 1)
#define NEXUS_CPU_FEATURE_AES    (1ULL << 2)
#define NEXUS_CPU_FEATURE_AVX    (1ULL << 3)
#define NEXUS_CPU_FEATURE_AVX2   (1ULL << 4)
#define NEXUS_CPU_FEATURE_FMA    (1ULL << 5)
#define NEXUS_CPU_FEATURE_BMI1   (1ULL << 6)
#define NEXUS_CPU_FEATURE_BMI2   (1ULL << 7)
#define NEXUS_CPU_FEATURE_SHA    (1ULL << 8)
#define NEXUS_CPU_FEATURE_SMP    (1ULL << 9)

struct nexus_cpu_capabilities {
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

#define NEXUS_ENOENT         2LL
#define NEXUS_EIO            5LL
#define NEXUS_EBADF          9LL
#define NEXUS_EFAULT        14LL
#define NEXUS_ENODEV        19LL
#define NEXUS_EMFILE         24LL
#define NEXUS_EFBIG          27LL
#define NEXUS_ENAMETOOLONG   36LL
#define NEXUS_ENOSYS         38LL
#define NEXUS_EINVAL         22LL

#ifdef NEXUS_USERLAND_HOST_TEST
/* Host tests inject a deterministic syscall backend. Production builds use
 * the real Nexus ring-3 interrupt ABI below.
 */
int64_t nexus_test_syscall3(uint64_t number, uint64_t arg1,
                            uint64_t arg2, uint64_t arg3);
static inline int64_t nexus_syscall3(uint64_t number,
                                     uint64_t arg1,
                                     uint64_t arg2,
                                     uint64_t arg3) {
    return nexus_test_syscall3(number, arg1, arg2, arg3);
}
#else
static inline int64_t nexus_syscall3(uint64_t number,
                                     uint64_t arg1,
                                     uint64_t arg2,
                                     uint64_t arg3) {
    uint64_t result = number;
    __asm__ volatile ("int $0x80"
                      : "+a"(result)
                      : "D"(arg1), "S"(arg2), "d"(arg3)
                      : "rcx", "r11", "memory", "cc");
    return (int64_t)result;
}
#endif

static inline uint64_t nexus_abi_version(void) {
    return (uint64_t)nexus_syscall3(NEXUS_SYS_ABI_VERSION, 0U, 0U, 0U);
}

static inline int64_t nexus_write(int descriptor,
                                  const void *buffer,
                                  size_t length) {
    return nexus_syscall3(NEXUS_SYS_WRITE, (uint64_t)descriptor,
                          (uint64_t)(uintptr_t)buffer, (uint64_t)length);
}

static inline int64_t nexus_open(const char *path) {
    return nexus_syscall3(NEXUS_SYS_OPEN, (uint64_t)(uintptr_t)path, 0U, 0U);
}

static inline int64_t nexus_read(int descriptor, void *buffer, size_t length) {
    return nexus_syscall3(NEXUS_SYS_READ, (uint64_t)descriptor,
                          (uint64_t)(uintptr_t)buffer, (uint64_t)length);
}

static inline int64_t nexus_close(int descriptor) {
    return nexus_syscall3(NEXUS_SYS_CLOSE, (uint64_t)descriptor, 0U, 0U);
}

static inline int64_t nexus_query_cpu_capabilities(
    struct nexus_cpu_capabilities *capabilities) {
    return nexus_syscall3(NEXUS_SYS_CPU_INFO,
        (uint64_t)(uintptr_t)capabilities, 0U, 0U);
}

static inline int64_t nexus_file_write(const char *path,
                                       const void *buffer,
                                       size_t length) {
    return nexus_syscall3(NEXUS_SYS_FILE_WRITE,
                          (uint64_t)(uintptr_t)path,
                          (uint64_t)(uintptr_t)buffer, (uint64_t)length);
}

__attribute__((noreturn))
static inline void nexus_exit(int status) {
    (void)nexus_syscall3(NEXUS_SYS_EXIT, (uint64_t)(int64_t)status, 0U, 0U);
    for (;;) __asm__ volatile ("pause");
}

#endif
