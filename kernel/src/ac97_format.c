#include "ac97.h"

#include <limits.h>

bool nexus_ac97_build_bdl_entry(uint64_t buffer_physical,
                                uint32_t sample_frames,
                                struct nexus_ac97_bdl_entry *entry) {
    if (entry == NULL || buffer_physical > UINT32_MAX ||
        (buffer_physical & 0x03U) != 0U ||
        sample_frames == 0U ||
        sample_frames > NEXUS_AC97_MAX_FRAMES)
        return false;

    const uint32_t samples = sample_frames * NEXUS_AC97_CHANNELS;
    if (samples == 0U || samples > UINT16_MAX || (samples & 1U) != 0U)
        return false;

    entry->buffer_physical = (uint32_t)buffer_physical;
    entry->sample_count = (uint16_t)samples;
    entry->control = NEXUS_AC97_BDL_IOC | NEXUS_AC97_BDL_BUP;
    return true;
}
