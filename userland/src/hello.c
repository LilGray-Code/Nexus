#include "nexus/syscall.h"

int main(void) {
    if (nexus_abi_version() != NEXUS_SYSCALL_ABI_VERSION) return 10;

    static const char filename[] = "README.TXT";
    static const char expected_prefix[] = "Nexus OS read-only FAT32";
    char contents[32];

    const int64_t descriptor = nexus_open(filename);
    if (descriptor < 0) return 11;

    const int64_t bytes_read =
        nexus_read((int)descriptor, contents, sizeof(contents));
    const int64_t close_result = nexus_close((int)descriptor);
    if (bytes_read != (int64_t)sizeof(contents) || close_result != 0)
        return 12;

    for (size_t i = 0U; i < sizeof(expected_prefix) - 1U; ++i) {
        if (contents[i] != expected_prefix[i]) return 13;
    }

    static const char message[] =
        "NEXUS FILE EXEC OK; SDK FILE IO OK";
    const int64_t bytes_written =
        nexus_write(1, message, sizeof(message) - 1U);
    if (bytes_written != (int64_t)(sizeof(message) - 1U))
        return 14;

    return 7;
}
