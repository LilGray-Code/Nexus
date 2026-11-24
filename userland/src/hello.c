
#include "nexus/runtime.h"

int main(void) {
    if (nexus_abi_version() != NEXUS_SYSCALL_ABI_VERSION) return 10;

    static const char filename[] = "README.TXT";
    static const char expected_prefix[] = "Nexus OS read-only FAT32";
    char contents[600];
    size_t contents_length = 0U;

    const int64_t bytes_read = nexus_read_file(
        filename, contents, sizeof(contents), &contents_length);
    if (bytes_read < 0 || contents_length < sizeof(expected_prefix) - 1U)
        return 11;
    if (nexus_memcmp(contents, expected_prefix,
                     sizeof(expected_prefix) - 1U) != 0) return 12;

    static const char output_name[] = "HELLO.OUT";
    static const char output_payload[] = "NEXUS SDK FILE WRITE OK";
    const int64_t saved = nexus_write_file(
        output_name, output_payload, sizeof(output_payload) - 1U);
    if (saved != (int64_t)(sizeof(output_payload) - 1U)) return 13;

    char output_readback[sizeof(output_payload) - 1U];
    size_t output_length = 0U;
    const int64_t output_bytes = nexus_read_file(
        output_name, output_readback, sizeof(output_readback), &output_length);
    if (output_bytes != (int64_t)sizeof(output_readback) ||
        output_length != sizeof(output_readback)) return 14;
    if (nexus_memcmp(output_readback, output_payload,
                     sizeof(output_readback)) != 0) return 15;

    static const char message[] =
        "NEXUS FILE EXEC OK; SDK FILE IO/WRITE OK; FORMAT=0023";
    if (nexus_printf(
            "NEXUS FILE EXEC OK; SDK FILE IO/WRITE OK; FORMAT=%04u", 23U) !=
        (int64_t)(sizeof(message) - 1U))
        return 16;

    return 7;
}
