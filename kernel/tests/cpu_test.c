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
    printf("NEXUS CPU CAPABILITY TESTS PASSED (%s / %s)\n",
           info->vendor, info->brand);
    return 0;
}
