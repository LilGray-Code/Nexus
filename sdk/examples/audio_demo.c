#include "nexus/app.h"

#include <stdbool.h>
#include <stdint.h>

#define AUDIO_CHUNK_FRAMES 1024U
#define AUDIO_AMPLITUDE 5000

static int16_t audio_chunk[AUDIO_CHUNK_FRAMES * NEXUS_APP_AUDIO_CHANNELS];

static void make_tone(uint32_t *phase) {
    for (uint32_t frame = 0U; frame < AUDIO_CHUNK_FRAMES; ++frame) {
        /* A 440 Hz square wave: no floating point or math library required. */
        const int16_t sample = *phase < 24000U
            ? AUDIO_AMPLITUDE : -AUDIO_AMPLITUDE;
        audio_chunk[frame * 2U] = sample;
        audio_chunk[frame * 2U + 1U] = sample;
        *phase += 440U;
        if (*phase >= NEXUS_APP_AUDIO_SAMPLE_RATE_HZ)
            *phase -= NEXUS_APP_AUDIO_SAMPLE_RATE_HZ;
    }
}

static bool escape_pressed(void) {
    struct nexus_app_input_event event;
    while (nexus_app_input_poll(&event) > 0) {
        if (event.type == NEXUS_APP_INPUT_KEYBOARD &&
            (event.scancode & 0x7FU) == 0x01U &&
            (event.scancode & NEXUS_APP_KEY_RELEASE) == 0U)
            return true;
    }
    return false;
}

void _start(void) {
    if (nexus_app_abi_version() != NEXUS_APP_ABI_VERSION)
        nexus_app_exit(2U);

    static const char welcome[] =
        "Nexus AC97 audio demo: 440 Hz stereo tone. Press Escape to exit.\n";
    (void)nexus_app_write(1U, welcome, sizeof(welcome) - 1U);

    uint32_t phase = 0U;
    for (;;) {
        if (escape_pressed()) nexus_app_exit(0U);
        make_tone(&phase);
        const int64_t result = nexus_app_audio_write_48k_s16_stereo(
            audio_chunk, AUDIO_CHUNK_FRAMES);
        if (result == 0 || result == -11) {
            /* The single DMA buffer is either queued or still playing. */
            (void)nexus_app_sleep_ticks(1U);
        } else if (result == -19) {
            static const char missing[] =
                "No supported AC97 audio device is ready.\n";
            (void)nexus_app_write(1U, missing, sizeof(missing) - 1U);
            nexus_app_exit(3U);
        } else {
            static const char failed[] =
                "Audio DMA submission failed.\n";
            (void)nexus_app_write(1U, failed, sizeof(failed) - 1U);
            nexus_app_exit(4U);
        }
    }
}
