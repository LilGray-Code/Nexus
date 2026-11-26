#include "cpu.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    assert(nexus_cpu_init());
    const struct nexus_cpu_info *info = nexus_cpu_get_info();
    assert(info != NULL);
    assert(info->cpuid_available);
    assert(info->vendor[0] != '\0');
    assert(nexus_cpu_get_vendor() == info->vendor_kind);
    const enum nexus_cpu_core_type local_core_type =
        nexus_cpu_get_local_core_type();
    assert(local_core_type == NEXUS_CPU_CORE_TYPE_UNKNOWN ||
           local_core_type == NEXUS_CPU_CORE_TYPE_PERFORMANCE ||
           local_core_type == NEXUS_CPU_CORE_TYPE_EFFICIENCY);
    assert(nexus_cpu_vendor_name()[0] != '\0');
    if (info->vendor_kind == NEXUS_CPU_VENDOR_INTEL)
        assert(nexus_cpu_is_intel() && !nexus_cpu_is_amd());
    if (info->vendor_kind == NEXUS_CPU_VENDOR_AMD ||
        info->vendor_kind == NEXUS_CPU_VENDOR_HYGON)
        assert(nexus_cpu_is_amd() && !nexus_cpu_is_intel());
    assert(info->brand[0] != '\0');
    assert(info->logical_processors_per_package >= 1U);
    assert(info->physical_cores_per_package >= 1U);
    if (info->physical_address_bits != 0U)
        assert(info->physical_address_bits >= 32U &&
               info->physical_address_bits <= 64U);
    if (info->virtual_address_bits != 0U)
        assert(info->virtual_address_bits >= 32U &&
               info->virtual_address_bits <= 64U);
    assert(nexus_cpu_has_feature(NEXUS_CPU_FEATURE_LONG_MODE));
    assert(nexus_cpu_has_feature(NEXUS_CPU_FEATURE_SSE2));
    if (nexus_cpu_has_feature(NEXUS_CPU_FEATURE_AVX2))
        assert(nexus_cpu_has_feature(NEXUS_CPU_FEATURE_AVX));
    if (nexus_cpu_has_feature(NEXUS_CPU_FEATURE_AVX512F)) {
        assert(nexus_cpu_has_feature(NEXUS_CPU_FEATURE_AVX));
        assert(nexus_cpu_has_feature(NEXUS_CPU_FEATURE_XSAVE));
        assert(nexus_cpu_has_feature(NEXUS_CPU_FEATURE_OSXSAVE));
    }
    nexus_cpu_disable_avx();
    assert(!nexus_cpu_has_feature(NEXUS_CPU_FEATURE_AVX));
    assert(!nexus_cpu_has_feature(NEXUS_CPU_FEATURE_AVX2));
    assert(!nexus_cpu_has_feature(NEXUS_CPU_FEATURE_AVX512F));
    assert(nexus_cpu_feature_level_label()[0] != '\0');
    if (info->core_type != NEXUS_CPU_CORE_TYPE_UNKNOWN) {
        assert(info->vendor_kind == NEXUS_CPU_VENDOR_INTEL);
        assert(info->core_type == NEXUS_CPU_CORE_TYPE_PERFORMANCE ||
               info->core_type == NEXUS_CPU_CORE_TYPE_EFFICIENCY);
    }
    if (info->frequency_info_valid) {
        assert(info->vendor_kind == NEXUS_CPU_VENDOR_INTEL);
        assert(info->base_frequency_mhz != 0U);
        assert(info->maximum_frequency_mhz == 0U ||
               info->maximum_frequency_mhz >= info->base_frequency_mhz);
    }
    printf("NEXUS CPU CAPABILITY TESTS PASSED (%s / %s)\n",
           info->vendor, info->brand);
    return 0;
}
