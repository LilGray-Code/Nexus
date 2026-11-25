#include "ac97.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>

static void test_descriptor_encoding(void) {
    struct nexus_ac97_bdl_entry entry = {0};
    assert(nexus_ac97_build_bdl_entry(0x00123000ULL, 1U, &entry));
    assert(entry.buffer_physical == 0x00123000U);
    assert(entry.sample_count == 2U);
    assert(entry.control == (NEXUS_AC97_BDL_IOC | NEXUS_AC97_BDL_BUP));

    assert(nexus_ac97_build_bdl_entry(0xFFF00000ULL, 1024U, &entry));
    assert(entry.sample_count == 2048U);
}

static void test_invalid_descriptors_are_rejected(void) {
    struct nexus_ac97_bdl_entry entry = {0};
    assert(!nexus_ac97_build_bdl_entry(0x100000000ULL, 1U, &entry));
    assert(!nexus_ac97_build_bdl_entry(0x00123001ULL, 1U, &entry));
    assert(!nexus_ac97_build_bdl_entry(0x00123000ULL, 0U, &entry));
    assert(!nexus_ac97_build_bdl_entry(0x00123000ULL,
        NEXUS_AC97_MAX_FRAMES + 1U, &entry));
    assert(!nexus_ac97_build_bdl_entry(0x00123000ULL, 1U, NULL));
}

int main(void) {
    test_descriptor_encoding();
    test_invalid_descriptors_are_rejected();
    puts("NEXUS AC97 PCM DMA DESCRIPTOR TESTS PASSED");
    return 0;
}
