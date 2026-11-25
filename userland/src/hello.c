
#include "nexus/runtime.h"

int main(void) {
    if (nexus_abi_version() != NEXUS_SYSCALL_ABI_VERSION) return 10;

    struct nexus_cpu_capabilities cpu = {0};
    if (nexus_query_cpu_capabilities(&cpu) != 0) return 17;
    if (cpu.structure_size != sizeof(cpu) ||
        cpu.abi_version != NEXUS_SYSCALL_ABI_VERSION ||
        cpu.vendor[0] == '\0' || cpu.brand[0] == '\0' ||
        cpu.physical_cores == 0U ||
        cpu.logical_processors == 0U ||
        (cpu.feature_flags & NEXUS_CPU_FEATURE_SSE2) == 0U)
        return 18;
    if ((cpu.feature_flags & NEXUS_CPU_FEATURE_AVX2) != 0U &&
        (cpu.feature_flags & NEXUS_CPU_FEATURE_AVX) == 0U)
        return 19;

    /* Exercise both AVX2 runtime dispatch and the scalar fallback. */
    uint8_t bulk_source[1024];
    uint8_t bulk_copy[1024];
    for (size_t i = 0U; i < sizeof(bulk_source); ++i)
        bulk_source[i] = (uint8_t)((i * 29U + 7U) & 0xFFU);
    if (nexus_memset(bulk_copy, 0xA5, sizeof(bulk_copy)) != bulk_copy)
        return 21;
    if (nexus_memcpy(bulk_copy, bulk_source, sizeof(bulk_source)) != bulk_copy ||
        nexus_memcmp(bulk_copy, bulk_source, sizeof(bulk_source)) != 0)
        return 22;
    if (nexus_memset(bulk_copy, 0x5A, sizeof(bulk_copy)) != bulk_copy)
        return 23;
    for (size_t i = 0U; i < sizeof(bulk_copy); ++i)
        if (bulk_copy[i] != 0x5AU) return 24;

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

    static const char cpu_ready[] = "NEXUS CPU CAPABILITY QUERY READY";
    if (nexus_puts(cpu_ready) != (int64_t)(sizeof(cpu_ready) - 1U))
        return 25;
    if ((cpu.feature_flags & NEXUS_CPU_FEATURE_AVX2) != 0U) {
        if (nexus_puts("NEXUS AVX2 USER SIMD READY") != 25) return 26;
    } else if (nexus_puts("NEXUS SCALAR USER SIMD FALLBACK READY") != 37) {
        return 27;
    }

    return 7;
}
