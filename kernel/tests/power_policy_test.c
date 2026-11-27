#include "power.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

int main(void) {
    uint8_t temperature = 0U;
    assert(nexus_power_profile_epp(NEXUS_POWER_PROFILE_POWER_SAVER) == 0xFFU);
    assert(nexus_power_profile_epp(NEXUS_POWER_PROFILE_BALANCED) == 0x80U);
    assert(nexus_power_profile_epp(NEXUS_POWER_PROFILE_PERFORMANCE) == 0x00U);

    assert(nexus_power_decode_intel_temperature(
        (1ULL << 31) | (20ULL << 16), 100ULL << 16, &temperature));
    assert(temperature == 80U);
    assert(!nexus_power_decode_intel_temperature(
        20ULL << 16, 100ULL << 16, &temperature));
    assert(!nexus_power_decode_intel_temperature(
        (1ULL << 31) | (90ULL << 16), 80ULL << 16, &temperature));
    assert(!nexus_power_decode_intel_temperature(
        (1ULL << 31) | (20ULL << 16), 20ULL << 16, &temperature));
    puts("NEXUS POWER POLICY HELPERS PASSED");
    return 0;
}
