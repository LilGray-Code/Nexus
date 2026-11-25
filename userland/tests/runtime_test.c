#include "nexus/runtime.h"
#include "nexus/syscall.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TEST_DESCRIPTOR 16
#define TEST_FILE_CAPACITY 1024U
#define TEST_CONSOLE_CAPACITY 512U

static uint8_t test_file[TEST_FILE_CAPACITY];
static size_t test_file_size;
static size_t test_file_cursor;
static bool test_file_exists;
static bool test_descriptor_open;
static bool test_missing_file;
static bool test_close_failure;
static size_t test_read_chunk = 17U;
static size_t test_write_chunk = 3U;
static bool test_zero_write;
static uint8_t test_console[TEST_CONSOLE_CAPACITY];
static size_t test_console_length;

static int64_t test_syscall(uint64_t number, uint64_t arg1,
                            uint64_t arg2, uint64_t arg3) {
    switch (number) {
        case NEXUS_SYS_ABI_VERSION:
            return (int64_t)NEXUS_SYSCALL_ABI_VERSION;
        case NEXUS_SYS_CPU_INFO: {
            struct nexus_cpu_capabilities *info =
                (struct nexus_cpu_capabilities *)(uintptr_t)arg1;
            if (info == NULL) return -NEXUS_EFAULT;
            nexus_memset(info, 0, sizeof(*info));
            info->structure_size = sizeof(*info);
            info->abi_version = NEXUS_SYSCALL_ABI_VERSION;
            info->feature_flags = NEXUS_CPU_FEATURE_SSE2;
#if defined(__GNUC__)
            __builtin_cpu_init();
            if (__builtin_cpu_supports("avx2"))
                info->feature_flags |= NEXUS_CPU_FEATURE_AVX |
                                       NEXUS_CPU_FEATURE_AVX2;
#endif
            info->physical_cores = 4U;
            info->logical_processors = 8U;
            info->vendor[0] = 'T';
            info->brand[0] = 'N';
            return 0;
        }
        case NEXUS_SYS_OPEN: {
            const char *path = (const char *)(uintptr_t)arg1;
            if (path == NULL || test_missing_file) return -NEXUS_ENOENT;
            if (nexus_strcmp(path, "README.TXT") != 0 &&
                nexus_strcmp(path, "HELLO.OUT") != 0)
                return -NEXUS_ENOENT;
            if (nexus_strcmp(path, "HELLO.OUT") == 0 && !test_file_exists)
                return -NEXUS_ENOENT;
            test_file_cursor = 0U;
            test_descriptor_open = true;
            return TEST_DESCRIPTOR;
        }
        case NEXUS_SYS_READ: {
            if (arg1 != TEST_DESCRIPTOR || !test_descriptor_open)
                return -NEXUS_EBADF;
            if (arg3 == 0U) return 0;
            if (arg2 == 0U) return -NEXUS_EFAULT;
            size_t amount = test_file_size - test_file_cursor;
            if (amount > (size_t)arg3) amount = (size_t)arg3;
            if (amount > test_read_chunk) amount = test_read_chunk;
            uint8_t *destination = (uint8_t *)(uintptr_t)arg2;
            for (size_t i = 0U; i < amount; ++i)
                destination[i] = test_file[test_file_cursor + i];
            test_file_cursor += amount;
            return (int64_t)amount;
        }
        case NEXUS_SYS_CLOSE:
            if (arg1 != TEST_DESCRIPTOR || !test_descriptor_open)
                return -NEXUS_EBADF;
            test_descriptor_open = false;
            return test_close_failure ? -NEXUS_EIO : 0;
        case NEXUS_SYS_FILE_WRITE: {
            const char *path = (const char *)(uintptr_t)arg1;
            const uint8_t *source = (const uint8_t *)(uintptr_t)arg2;
            const size_t length = (size_t)arg3;
            if (path == NULL || nexus_strcmp(path, "HELLO.OUT") != 0)
                return -NEXUS_EINVAL;
            if (length > sizeof(test_file)) return -NEXUS_EFBIG;
            if (length != 0U && source == NULL) return -NEXUS_EFAULT;
            for (size_t i = 0U; i < length; ++i) test_file[i] = source[i];
            test_file_size = length;
            test_file_exists = true;
            return (int64_t)length;
        }
        case NEXUS_SYS_WRITE: {
            if (arg1 != 1U) return -NEXUS_EBADF;
            if (test_zero_write) return 0;
            if (arg3 != 0U && arg2 == 0U) return -NEXUS_EFAULT;
            size_t amount = (size_t)arg3;
            if (amount > test_write_chunk) amount = test_write_chunk;
            if (amount > sizeof(test_console) - test_console_length)
                amount = sizeof(test_console) - test_console_length;
            const uint8_t *source = (const uint8_t *)(uintptr_t)arg2;
            for (size_t i = 0U; i < amount; ++i)
                test_console[test_console_length + i] = source[i];
            test_console_length += amount;
            return (int64_t)amount;
        }
        default:
            return -NEXUS_ENOSYS;
    }
}

int64_t nexus_test_syscall3(uint64_t number, uint64_t arg1,
                            uint64_t arg2, uint64_t arg3) {
    return test_syscall(number, arg1, arg2, arg3);
}

static void test_runtime_cpu_query_and_bulk_memory(void) {
    struct nexus_cpu_capabilities cpu = {0};
    assert(nexus_query_cpu_capabilities(&cpu) == 0);
    assert(cpu.structure_size == sizeof(cpu));
    assert(cpu.abi_version == NEXUS_SYSCALL_ABI_VERSION);
    assert((cpu.feature_flags & NEXUS_CPU_FEATURE_SSE2) != 0U);
    assert(cpu.physical_cores == 4U && cpu.logical_processors == 8U);
    assert(cpu.vendor[0] == 'T' && cpu.brand[0] == 'N');

    uint8_t source[777], destination[777];
    for (size_t i = 0U; i < sizeof(source); ++i)
        source[i] = (uint8_t)((i * 37U) & 0xFFU);
    assert(nexus_memset(destination, 0xA5, sizeof(destination)) == destination);
    for (size_t i = 0U; i < sizeof(destination); ++i)
        assert(destination[i] == 0xA5U);
    assert(nexus_memcpy(destination, source, sizeof(source)) == destination);
    assert(nexus_memcmp(destination, source, sizeof(source)) == 0);
}

static void test_string_and_memory_helpers(void) {
    assert(nexus_strlen("nexus") == 5U);
    assert(nexus_strnlen("nexus", 3U) == 3U);
    assert(nexus_strcmp("abc", "abc") == 0);
    assert(nexus_strcmp("abc", "abd") < 0);
    assert(nexus_strncmp("abcdef", "abcxyz", 3U) == 0);

    uint8_t first[8] = {0};
    uint8_t second[8] = {0};
    assert(nexus_memset(first, 0x5A, sizeof(first)) == first);
    assert(nexus_memcpy(second, first, sizeof(first)) == second);
    assert(nexus_memcmp(first, second, sizeof(first)) == 0);
    second[7] = 0U;
    assert(nexus_memcmp(first, second, sizeof(first)) > 0);
}

static void test_bounded_file_read_and_overflow(void) {
    static const char text[] =
        "Nexus runtime read test: short reads must be joined safely.";
    for (size_t i = 0U; i < sizeof(text) - 1U; ++i)
        test_file[i] = (uint8_t)text[i];
    test_file_size = sizeof(text) - 1U;
    test_file_exists = true;
    test_missing_file = false;
    test_descriptor_open = false;
    test_read_chunk = 7U;

    uint8_t output[128] = {0};
    size_t bytes_read = 999U;
    const int64_t read_result =
        nexus_read_file("README.TXT", output, sizeof(output), &bytes_read);
    assert(read_result == (int64_t)(sizeof(text) - 1U));
    assert(bytes_read == sizeof(text) - 1U);
    assert(nexus_memcmp(output, text, bytes_read) == 0);
    assert(!test_descriptor_open);

    bytes_read = 999U;
    assert(nexus_read_file("README.TXT", output, 4U, &bytes_read) ==
           -NEXUS_EFBIG);
    assert(bytes_read == 0U && !test_descriptor_open);

    assert(nexus_read_file("MISSING.TXT", output, sizeof(output),
                           &bytes_read) == -NEXUS_ENOENT);
    assert(bytes_read == 0U);

    test_close_failure = true;
    assert(nexus_read_file("README.TXT", output, sizeof(output),
                           &bytes_read) == -NEXUS_EIO);
    test_close_failure = false;
    assert(!test_descriptor_open);

    assert(nexus_read_file("README.TXT", NULL, 1U,
                           &bytes_read) == -NEXUS_EINVAL);
    assert(nexus_read_file("README.TXT", output, sizeof(output),
                           NULL) == -NEXUS_EINVAL);
}

static void test_file_write_and_readback(void) {
    static const char payload[] = "NEXUS SDK WRITE ROUND TRIP";
    assert(nexus_write_file("HELLO.OUT", payload, sizeof(payload) - 1U) ==
           (int64_t)(sizeof(payload) - 1U));
    uint8_t readback[64] = {0};
    size_t bytes_read = 0U;
    assert(nexus_read_file("HELLO.OUT", readback, sizeof(readback),
                           &bytes_read) == (int64_t)(sizeof(payload) - 1U));
    assert(bytes_read == sizeof(payload) - 1U);
    assert(nexus_memcmp(readback, payload, bytes_read) == 0);
}

static void test_formatted_console_output(void) {
    test_console_length = 0U;
    test_write_chunk = 5U;
    assert(nexus_printf("PID=%04u %s %d 0x%X %c %%", 23U,
                        "ready", -7, 0x2AU, 'Z') > 0);
    static const char expected[] = "PID=0023 ready -7 0x2A Z %";
    assert(test_console_length == sizeof(expected) - 1U);
    assert(nexus_memcmp(test_console, expected, sizeof(expected) - 1U) == 0);

    assert(nexus_printf("%q", 1) == -NEXUS_EINVAL);
    assert(nexus_printf("%100u", 1U) == -NEXUS_EINVAL);
    assert(nexus_printf(NULL) == -NEXUS_EINVAL);
}
static void test_partial_console_writes_and_errors(void) {
    static const char message[] = "short writes must be retried";
    test_console_length = 0U;
    test_write_chunk = 3U;
    test_zero_write = false;
    assert(nexus_puts(message) == (int64_t)(sizeof(message) - 1U));
    assert(test_console_length == sizeof(message) - 1U);
    assert(nexus_memcmp(test_console, message, test_console_length) == 0);

    test_zero_write = true;
    assert(nexus_write_all(1, message, sizeof(message) - 1U) ==
           -NEXUS_EIO);
    test_zero_write = false;
    assert(nexus_write_all(1, NULL, 1U) == -NEXUS_EINVAL);
}

int main(void) {
    assert(nexus_abi_version() == NEXUS_SYSCALL_ABI_VERSION);
    test_runtime_cpu_query_and_bulk_memory();
    test_string_and_memory_helpers();
    test_bounded_file_read_and_overflow();
    test_file_write_and_readback();
    test_formatted_console_output();
    test_partial_console_writes_and_errors();
    return 0;
}
