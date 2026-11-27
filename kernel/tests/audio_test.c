#include "audio.h"
#include "ac97.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static bool fake_ac97_ready;
static bool fake_ac97_idle;
static enum nexus_ac97_submit_result fake_ac97_submit_result;
static uint32_t fake_ac97_submissions;
static uint32_t fake_ac97_last_frames;
static int16_t fake_ac97_last_samples[NEXUS_AC97_MAX_FRAMES * 2U];
static uint8_t fake_ac97_hardware_volume;

bool ac97_is_ready(void) { return fake_ac97_ready; }
bool ac97_playback_idle(void) { return fake_ac97_idle; }
void ac97_poll(void) { }
bool ac97_set_master_volume(uint8_t percent) {
    fake_ac97_hardware_volume = percent;
    return fake_ac97_ready && percent <= 100U;
}
enum nexus_ac97_submit_result ac97_write_pcm_s16_stereo_48k(
    const int16_t *samples, uint32_t frames) {
    if (!fake_ac97_ready) return NEXUS_AC97_SUBMIT_NOT_READY;
    if (!fake_ac97_idle) return NEXUS_AC97_SUBMIT_BUSY;
    if (fake_ac97_submit_result != NEXUS_AC97_SUBMIT_OK)
        return fake_ac97_submit_result;
    assert(samples != NULL && frames <= NEXUS_AC97_MAX_FRAMES);
    memcpy(fake_ac97_last_samples, samples,
           (size_t)frames * 2U * sizeof(int16_t));
    fake_ac97_last_frames = frames;
    ++fake_ac97_submissions;
    fake_ac97_idle = false;
    return NEXUS_AC97_SUBMIT_OK;
}

static struct nexus_audio_device_desc make_device(
    enum nexus_audio_device_type type,
    enum nexus_audio_direction direction,
    uint32_t rate, uint8_t channels, const char *label) {
    struct nexus_audio_device_desc device = {
        .type = type,
        .direction = direction,
        .sample_rate = rate,
        .channels = channels,
        .capabilities = NEXUS_AUDIO_DEVICE_VOLUME_CONTROL |
                        NEXUS_AUDIO_DEVICE_MUTE_CONTROL |
                        NEXUS_AUDIO_DEVICE_LOW_LATENCY
    };
    size_t i = 0U;
    while (label[i] != '\0' && i < NEXUS_AUDIO_DEVICE_LABEL_MAX) {
        device.label[i] = label[i];
        ++i;
    }
    device.label[i] = '\0';
    return device;
}

static uint32_t add_ac97_output(void) {
    const struct nexus_audio_device_desc device = make_device(
        NEXUS_AUDIO_DEVICE_AC97, NEXUS_AUDIO_PLAYBACK,
        NEXUS_AUDIO_MIX_RATE, 2U, "Test AC97 Output");
    return nexus_audio_register_device(&device);
}

static uint32_t open_output(uint32_t app, uint32_t rate, uint8_t channels,
                            uint8_t volume) {
    uint32_t stream = 0U;
    assert(nexus_audio_stream_open(app, NEXUS_AUDIO_PLAYBACK, 0U,
                                   rate, channels, volume, &stream));
    assert(stream != 0U);
    return stream;
}

static void test_device_registry_and_defaults(void) {
    fake_ac97_ready = false;
    assert(nexus_audio_init());
    assert(nexus_audio_device_count() == 0U);
    assert(nexus_audio_default_device(NEXUS_AUDIO_PLAYBACK) == 0U);

    struct nexus_audio_device_desc bad = make_device(
        NEXUS_AUDIO_DEVICE_USB, NEXUS_AUDIO_PLAYBACK, 48000U, 2U, "USB DAC");
    bad.channels = 3U;
    assert(nexus_audio_register_device(&bad) == 0U);
    bad.channels = 2U;
    bad.sample_rate = 1000U;
    assert(nexus_audio_register_device(&bad) == 0U);

    const uint32_t ac97 = add_ac97_output();
    assert(ac97 != 0U);
    assert(nexus_audio_device_count() == 1U);
    assert(nexus_audio_default_device(NEXUS_AUDIO_PLAYBACK) == ac97);
    struct nexus_audio_device info;
    assert(nexus_audio_device_get(0U, &info));
    assert(info.id == ac97 && info.ready && info.present);
    assert(info.description.type == NEXUS_AUDIO_DEVICE_AC97);
    assert(!nexus_audio_device_get(1U, &info));

    const struct nexus_audio_device_desc usb = make_device(
        NEXUS_AUDIO_DEVICE_USB, NEXUS_AUDIO_PLAYBACK,
        44100U, 2U, "USB Headphones");
    const uint32_t usb_id = nexus_audio_register_device(&usb);
    assert(usb_id != 0U && usb_id != ac97);
    assert(nexus_audio_device_count() == 2U);
    assert(nexus_audio_set_default_device(NEXUS_AUDIO_PLAYBACK, usb_id));
    assert(nexus_audio_default_device(NEXUS_AUDIO_PLAYBACK) == usb_id);
    assert(!nexus_audio_set_default_device(NEXUS_AUDIO_CAPTURE, usb_id));
    assert(nexus_audio_set_device_ready(usb_id, false));
    assert(nexus_audio_default_device(NEXUS_AUDIO_PLAYBACK) == ac97);
    assert(nexus_audio_remove_device(ac97));
    assert(nexus_audio_default_device(NEXUS_AUDIO_PLAYBACK) == 0U);
}

static void test_mono_stereo_mixing_volume_and_mute(void) {
    assert(nexus_audio_init());
    assert(add_ac97_output() != 0U);
    nexus_audio_set_master_volume(100U);
    nexus_audio_set_master_mute(false);

    const int16_t mono[] = {1000, -2000, 3000, -4000};
    const uint32_t first = open_output(10U, 48000U, 1U, 100U);
    assert(nexus_audio_stream_write_s16(first, mono, 4U) == 4U);
    int16_t output[8] = {0};
    bool had_audio = false;
    assert(nexus_audio_mix_s16_stereo_48k(output, 4U, &had_audio));
    assert(had_audio);
    for (size_t i = 0U; i < 4U; ++i) {
        assert(output[i * 2U] == mono[i]);
        assert(output[i * 2U + 1U] == mono[i]);
    }

    const int16_t stereo[] = {10000, -10000, 10000, -10000};
    const uint32_t second = open_output(20U, 48000U, 2U, 50U);
    assert(nexus_audio_stream_write_s16(second, stereo, 2U) == 2U);
    nexus_audio_set_application_volume(20U, 25U);
    assert(nexus_audio_mix_s16_stereo_48k(output, 2U, &had_audio));
    assert(had_audio);
    assert(output[0] == 2500 && output[1] == -2500);
    assert(output[2] == 2500 && output[3] == -2500);

    assert(nexus_audio_stream_set_mute(first, true));
    assert(nexus_audio_stream_set_mute(second, true));
    assert(nexus_audio_mix_s16_stereo_48k(output, 2U, &had_audio));
    assert(!had_audio);
    assert(output[0] == 0 && output[1] == 0);

    assert(nexus_audio_stream_set_mute(second, false));
    assert(nexus_audio_set_application_volume(20U, 100U) == 1U);
    assert(nexus_audio_mix_s16_stereo_48k(output, 2U, &had_audio));
    assert(had_audio);
    /* Master mute is applied after all streams, not by changing their gain. */
    nexus_audio_set_master_mute(true);
    assert(nexus_audio_mix_s16_stereo_48k(output, 2U, &had_audio));
    assert(output[0] == 0 && output[1] == 0);
    assert(nexus_audio_stream_close(first));
    assert(nexus_audio_stream_close(second));
    assert(!nexus_audio_stream_close(second));
}

static void test_resampling_and_clipping(void) {
    assert(nexus_audio_init());
    assert(add_ac97_output() != 0U);
    nexus_audio_set_master_volume(100U);
    const uint32_t resampled = open_output(1U, 24000U, 1U, 100U);
    const int16_t low_rate[] = {0, 10000, 20000};
    assert(nexus_audio_stream_write_s16(resampled, low_rate, 3U) == 3U);
    int16_t output[8] = {0};
    bool had_audio = false;
    assert(nexus_audio_mix_s16_stereo_48k(output, 4U, &had_audio));
    assert(had_audio);
    const int16_t expected[] = {0, 5000, 10000, 15000};
    for (size_t i = 0U; i < 4U; ++i) {
        assert(output[i * 2U] == expected[i]);
        assert(output[i * 2U + 1U] == expected[i]);
    }

    const uint32_t louder_a = open_output(2U, 48000U, 2U, 100U);
    const uint32_t louder_b = open_output(3U, 48000U, 2U, 100U);
    const int16_t loud[] = {30000, -30000, 30000, -30000};
    assert(nexus_audio_stream_write_s16(louder_a, loud, 2U) == 2U);
    assert(nexus_audio_stream_write_s16(louder_b, loud, 2U) == 2U);
    assert(nexus_audio_mix_s16_stereo_48k(output, 2U, &had_audio));
    assert(output[0] == INT16_MAX && output[1] == INT16_MIN);
}

static void test_ring_capacity_and_underrun_accounting(void) {
    assert(nexus_audio_init());
    assert(add_ac97_output() != 0U);
    const uint32_t stream = open_output(1U, 48000U, 2U, 100U);
    static int16_t input[NEXUS_AUDIO_STREAM_RING_FRAMES * 2U];
    const size_t accepted = nexus_audio_stream_write_s16(
        stream, input, NEXUS_AUDIO_STREAM_RING_FRAMES + 25U);
    assert(accepted == NEXUS_AUDIO_STREAM_RING_FRAMES);
    struct nexus_audio_stream_info info;
    assert(nexus_audio_stream_get_info(0U, &info));
    assert(info.queued_frames == NEXUS_AUDIO_STREAM_RING_FRAMES);
    assert(info.overrun_frames == 25U);

    int16_t output[NEXUS_AUDIO_MIX_MAX_FRAMES * 2U];
    bool had_audio = false;
    assert(nexus_audio_mix_s16_stereo_48k(
        output, NEXUS_AUDIO_MIX_MAX_FRAMES, &had_audio));
    assert(had_audio);
    assert(nexus_audio_stream_get_info(0U, &info));
    assert(info.queued_frames == NEXUS_AUDIO_STREAM_RING_FRAMES -
                                 NEXUS_AUDIO_MIX_MAX_FRAMES);
    assert(nexus_audio_mix_s16_stereo_48k(
        output, NEXUS_AUDIO_MIX_MAX_FRAMES, &had_audio));
    assert(!had_audio);
    assert(nexus_audio_stream_get_info(0U, &info));
    assert(info.underrun_frames > 0U);
}

static void test_capture_stream_path_and_device_hotplug(void) {
    assert(nexus_audio_init());
    const struct nexus_audio_device_desc mic = make_device(
        NEXUS_AUDIO_DEVICE_USB, NEXUS_AUDIO_CAPTURE,
        48000U, 2U, "USB Microphone");
    const uint32_t mic_id = nexus_audio_register_device(&mic);
    assert(mic_id != 0U);
    assert(nexus_audio_default_device(NEXUS_AUDIO_CAPTURE) == mic_id);
    uint32_t stream = 0U;
    assert(nexus_audio_stream_open(55U, NEXUS_AUDIO_CAPTURE, 0U,
                                   48000U, 1U, 100U, &stream));
    assert(stream != 0U);
    const int16_t microphone[] = {
        1000, 3000, -2000, 2000, 4000, 6000
    };
    assert(nexus_audio_capture_publish(mic_id, microphone, 3U) == 3U);
    int16_t recording[3] = {0};
    assert(nexus_audio_stream_read_s16(stream, recording, 3U) == 3U);
    assert(recording[0] == 2000);
    assert(recording[1] == 0);
    assert(recording[2] == 5000);
    assert(nexus_audio_set_device_ready(mic_id, false));
    assert(nexus_audio_default_device(NEXUS_AUDIO_CAPTURE) == 0U);
    assert(!nexus_audio_stream_open(56U, NEXUS_AUDIO_CAPTURE, 0U,
                                    48000U, 1U, 100U, &stream));
}

static void test_ac97_backend_adapter(void) {
    fake_ac97_ready = true;
    fake_ac97_idle = true;
    fake_ac97_submit_result = NEXUS_AC97_SUBMIT_OK;
    fake_ac97_submissions = 0U;
    fake_ac97_last_frames = 0U;
    assert(nexus_audio_init());
    assert(nexus_audio_device_count() == 1U);
    assert(fake_ac97_hardware_volume == 100U);

    const uint32_t stream = open_output(123U, 48000U, 2U, 100U);
    int16_t tone[960];
    for (size_t i = 0U; i < 480U; ++i) {
        tone[i * 2U] = 1234;
        tone[i * 2U + 1U] = -1234;
    }
    assert(nexus_audio_stream_write_s16(stream, tone, 480U) == 480U);
    nexus_audio_poll();
    assert(fake_ac97_submissions == 1U);
    assert(fake_ac97_last_frames == 480U);
    assert(fake_ac97_last_samples[0] == (int16_t)(1234 * 75 / 100));
    assert(fake_ac97_last_samples[1] == (int16_t)(-1234 * 75 / 100));

    fake_ac97_idle = true;
    fake_ac97_submit_result = NEXUS_AC97_SUBMIT_IO_ERROR;
    assert(nexus_audio_stream_write_s16(stream, tone, 480U) == 480U);
    nexus_audio_poll();
    struct nexus_audio_stats stats;
    nexus_audio_get_stats(&stats);
    assert(stats.backend_submit_errors == 1U);
    fake_ac97_ready = false;
    fake_ac97_idle = true;
}

int main(void) {
    test_device_registry_and_defaults();
    test_mono_stereo_mixing_volume_and_mute();
    test_resampling_and_clipping();
    test_ring_capacity_and_underrun_accounting();
    test_capture_stream_path_and_device_hotplug();
    test_ac97_backend_adapter();
    puts("NEXUS AUDIO CORE, MIXER AND CAPTURE TESTS PASSED");
    return 0;
}
