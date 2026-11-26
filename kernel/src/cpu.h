#ifndef NEXUS_CPU_H
#define NEXUS_CPU_H

#include <stdbool.h>
#include <stdint.h>

/* Feature masks are macros rather than large enumerators so they remain
 * strictly conforming uint64_t constants in ISO C and C23 toolchains. */
#define NEXUS_CPU_FEATURE_FPU (UINT64_C(1) << 0)
#define NEXUS_CPU_FEATURE_TSC (UINT64_C(1) << 1)
#define NEXUS_CPU_FEATURE_APIC (UINT64_C(1) << 2)
#define NEXUS_CPU_FEATURE_SSE (UINT64_C(1) << 3)
#define NEXUS_CPU_FEATURE_SSE2 (UINT64_C(1) << 4)
#define NEXUS_CPU_FEATURE_SSE3 (UINT64_C(1) << 5)
#define NEXUS_CPU_FEATURE_SSSE3 (UINT64_C(1) << 6)
#define NEXUS_CPU_FEATURE_SSE41 (UINT64_C(1) << 7)
#define NEXUS_CPU_FEATURE_SSE42 (UINT64_C(1) << 8)
#define NEXUS_CPU_FEATURE_AES (UINT64_C(1) << 9)
#define NEXUS_CPU_FEATURE_PCLMUL (UINT64_C(1) << 10)
#define NEXUS_CPU_FEATURE_XSAVE (UINT64_C(1) << 11)
#define NEXUS_CPU_FEATURE_OSXSAVE (UINT64_C(1) << 12)
#define NEXUS_CPU_FEATURE_AVX (UINT64_C(1) << 13)
#define NEXUS_CPU_FEATURE_AVX2 (UINT64_C(1) << 14)
#define NEXUS_CPU_FEATURE_FMA (UINT64_C(1) << 15)
#define NEXUS_CPU_FEATURE_BMI1 (UINT64_C(1) << 16)
#define NEXUS_CPU_FEATURE_BMI2 (UINT64_C(1) << 17)
#define NEXUS_CPU_FEATURE_RDRAND (UINT64_C(1) << 18)
#define NEXUS_CPU_FEATURE_RDSEED (UINT64_C(1) << 19)
#define NEXUS_CPU_FEATURE_SHA (UINT64_C(1) << 20)
#define NEXUS_CPU_FEATURE_NX (UINT64_C(1) << 21)
#define NEXUS_CPU_FEATURE_LONG_MODE (UINT64_C(1) << 22)
#define NEXUS_CPU_FEATURE_INVAR_TSC (UINT64_C(1) << 23)
#define NEXUS_CPU_FEATURE_HYPERVISOR (UINT64_C(1) << 24)
#define NEXUS_CPU_FEATURE_PCID (UINT64_C(1) << 25)
#define NEXUS_CPU_FEATURE_FSGSBASE (UINT64_C(1) << 26)
#define NEXUS_CPU_FEATURE_UMIP (UINT64_C(1) << 27)
#define NEXUS_CPU_FEATURE_SMEP (UINT64_C(1) << 28)
#define NEXUS_CPU_FEATURE_INVPCID (UINT64_C(1) << 29)
#define NEXUS_CPU_FEATURE_SMAP (UINT64_C(1) << 30)
#define NEXUS_CPU_FEATURE_CLFLUSHOPT (UINT64_C(1) << 31)
#define NEXUS_CPU_FEATURE_CLWB (UINT64_C(1) << 32)
#define NEXUS_CPU_FEATURE_LA57 (UINT64_C(1) << 33)
#define NEXUS_CPU_FEATURE_RDTSCP (UINT64_C(1) << 34)
#define NEXUS_CPU_FEATURE_PAGE_1GB (UINT64_C(1) << 35)
#define NEXUS_CPU_FEATURE_FXSR (UINT64_C(1) << 36)
#define NEXUS_CPU_FEATURE_AVX512F (UINT64_C(1) << 37)
#define NEXUS_CPU_FEATURE_AVX512DQ (UINT64_C(1) << 38)
#define NEXUS_CPU_FEATURE_AVX512CD (UINT64_C(1) << 39)
#define NEXUS_CPU_FEATURE_AVX512BW (UINT64_C(1) << 40)
#define NEXUS_CPU_FEATURE_AVX512VL (UINT64_C(1) << 41)
#define NEXUS_CPU_FEATURE_AVX512VNNI (UINT64_C(1) << 42)
#define NEXUS_CPU_FEATURE_CMPXCHG16B (UINT64_C(1) << 43)

struct nexus_cpu_info {
    bool cpuid_available;
    char vendor[13];
    char brand[49];
    uint32_t max_basic_leaf;
    uint32_t max_extended_leaf;
    uint32_t family;
    uint32_t model;
    uint32_t stepping;
    uint16_t logical_processors_per_package;
    uint16_t physical_cores_per_package;
    uint8_t physical_address_bits;
    uint8_t virtual_address_bits;
    uint64_t features;
};

/* Runtime detection only: optional instructions are never enabled implicitly. */
bool nexus_cpu_init(void);
const struct nexus_cpu_info *nexus_cpu_get_info(void);
bool nexus_cpu_has_feature(uint64_t feature);
/* Publish AVX/AVX2 only after the kernel enables and verifies XCR0 state. */
void nexus_cpu_enable_avx_features(void);
/* Mark AVX unavailable when the OS deliberately disables XSAVE state. */
void nexus_cpu_disable_avx(void);
const char *nexus_cpu_feature_level_label(void);

#endif
