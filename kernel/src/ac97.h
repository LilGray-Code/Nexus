#ifndef NEXUS_AC97_H
#define NEXUS_AC97_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NEXUS_AC97_SAMPLE_RATE_HZ 48000U
#define NEXUS_AC97_CHANNELS 2U
#define NEXUS_AC97_MAX_FRAMES 1024U
#define NEXUS_AC97_MAX_BYTES (NEXUS_AC97_MAX_FRAMES * 4U)

struct nexus_ac97_bdl_entry {
    uint32_t buffer_physical;
    uint16_t sample_count;
    uint16_t control;
} __attribute__((packed));

#define NEXUS_AC97_BDL_IOC 0x8000U
#define NEXUS_AC97_BDL_BUP 0x4000U

enum nexus_ac97_submit_result {
    NEXUS_AC97_SUBMIT_OK = 0,
    NEXUS_AC97_SUBMIT_BUSY,
    NEXUS_AC97_SUBMIT_NOT_READY,
    NEXUS_AC97_SUBMIT_INVALID,
    NEXUS_AC97_SUBMIT_IO_ERROR
};

bool nexus_ac97_build_bdl_entry(uint64_t buffer_physical,
                                uint32_t sample_frames,
                                struct nexus_ac97_bdl_entry *entry);

bool ac97_init(void);
bool ac97_is_ready(void);
/* Set codec master output volume from 0 (mute) to 100 (maximum). */
bool ac97_set_master_volume(uint8_t percent);
void ac97_poll(void);
enum nexus_ac97_submit_result ac97_write_pcm_s16_stereo_48k(
    const int16_t *interleaved_samples, uint32_t sample_frames);

#endif
