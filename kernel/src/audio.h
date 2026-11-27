#ifndef NEXUS_AUDIO_H
#define NEXUS_AUDIO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NEXUS_AUDIO_MAX_DEVICES 8U
#define NEXUS_AUDIO_MAX_STREAMS 8U
#define NEXUS_AUDIO_DEVICE_LABEL_MAX 31U
#define NEXUS_AUDIO_STREAM_RING_FRAMES 2048U
#define NEXUS_AUDIO_MAX_SAMPLE_RATE 192000U
#define NEXUS_AUDIO_MIN_SAMPLE_RATE 8000U
#define NEXUS_AUDIO_MIX_RATE 48000U
#define NEXUS_AUDIO_MIX_MAX_FRAMES 1024U

enum nexus_audio_device_type {
    NEXUS_AUDIO_DEVICE_AC97 = 1,
    NEXUS_AUDIO_DEVICE_HDA,
    NEXUS_AUDIO_DEVICE_USB,
    NEXUS_AUDIO_DEVICE_HDMI_DP,
    NEXUS_AUDIO_DEVICE_BLUETOOTH
};

enum nexus_audio_direction {
    NEXUS_AUDIO_PLAYBACK = 1,
    NEXUS_AUDIO_CAPTURE = 2
};

#define NEXUS_AUDIO_DEVICE_VOLUME_CONTROL (1U << 0)
#define NEXUS_AUDIO_DEVICE_MUTE_CONTROL   (1U << 1)
#define NEXUS_AUDIO_DEVICE_LOW_LATENCY    (1U << 2)
#define NEXUS_AUDIO_DEVICE_HOTPLUG        (1U << 3)

struct nexus_audio_device_desc {
    enum nexus_audio_device_type type;
    enum nexus_audio_direction direction;
    uint32_t sample_rate;
    uint8_t channels;
    uint32_t capabilities;
    char label[NEXUS_AUDIO_DEVICE_LABEL_MAX + 1U];
};

struct nexus_audio_device {
    uint32_t id;
    struct nexus_audio_device_desc description;
    bool present;
    bool ready;
    uint64_t frames_submitted;
    uint64_t frames_captured;
    uint64_t errors;
};

struct nexus_audio_stream_info {
    uint32_t id;
    uint32_t application_id;
    uint32_t device_id; /* zero follows the current default endpoint */
    enum nexus_audio_direction direction;
    uint32_t sample_rate;
    uint8_t channels;
    uint8_t volume_percent;
    bool muted;
    uint32_t queued_frames;
    uint64_t underrun_frames;
    uint64_t overrun_frames;
};

struct nexus_audio_stats {
    uint64_t mix_calls;
    uint64_t mixed_frames;
    uint64_t playback_underrun_frames;
    uint64_t capture_overrun_frames;
    uint64_t backend_submit_errors;
};

bool nexus_audio_init(void);
size_t nexus_audio_device_count(void);
bool nexus_audio_device_get(size_t index, struct nexus_audio_device *device);
uint32_t nexus_audio_default_device(enum nexus_audio_direction direction);
bool nexus_audio_set_default_device(enum nexus_audio_direction direction,
                                    uint32_t device_id);
uint32_t nexus_audio_register_device(
    const struct nexus_audio_device_desc *description);
bool nexus_audio_set_device_ready(uint32_t device_id, bool ready);
bool nexus_audio_remove_device(uint32_t device_id);

void nexus_audio_set_master_volume(uint8_t volume_percent);
uint8_t nexus_audio_get_master_volume(void);
void nexus_audio_set_master_mute(bool muted);
bool nexus_audio_get_master_mute(void);

bool nexus_audio_stream_open(
    uint32_t application_id, enum nexus_audio_direction direction,
    uint32_t device_id, uint32_t sample_rate, uint8_t channels,
    uint8_t volume_percent, uint32_t *stream_id);
bool nexus_audio_stream_close(uint32_t stream_id);
bool nexus_audio_stream_get_info(
    size_t index, struct nexus_audio_stream_info *info);
bool nexus_audio_stream_set_volume(uint32_t stream_id,
                                   uint8_t volume_percent);
bool nexus_audio_stream_set_mute(uint32_t stream_id, bool muted);
bool nexus_audio_stream_set_device(uint32_t stream_id, uint32_t device_id);
size_t nexus_audio_set_application_volume(
    uint32_t application_id, uint8_t volume_percent);

/* Playback input is signed 16-bit interleaved PCM (mono or stereo). */
size_t nexus_audio_stream_write_s16(
    uint32_t stream_id, const int16_t *samples, size_t frames);

/* Hardware capture drivers publish PCM; capture streams read negotiated PCM. */
size_t nexus_audio_capture_publish(
    uint32_t device_id, const int16_t *samples, size_t frames);
size_t nexus_audio_stream_read_s16(
    uint32_t stream_id, int16_t *samples, size_t frame_capacity);

/* Mixes routed playback streams to signed 16-bit stereo at 48 kHz. */
bool nexus_audio_mix_s16_stereo_48k(
    int16_t *output, uint32_t frames, bool *had_audio);
void nexus_audio_poll(void);
void nexus_audio_get_stats(struct nexus_audio_stats *stats);

#endif
