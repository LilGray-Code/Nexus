#include "audio.h"

#include "ac97.h"

#include <stddef.h>
#include <stdint.h>

#define AUDIO_RING_MASK (NEXUS_AUDIO_STREAM_RING_FRAMES - 1U)
#define AUDIO_VOLUME_MAX 100U
#define AUDIO_AC97_BLOCK_FRAMES 480U
#define AUDIO_Q32_ONE (UINT64_C(1) << 32)

struct nexus_audio_stream {
    bool active;
    uint32_t id;
    uint32_t application_id;
    uint32_t device_id;
    enum nexus_audio_direction direction;
    uint32_t sample_rate;
    uint8_t channels;
    uint8_t volume_percent;
    bool muted;
    uint32_t read_index;
    uint32_t write_index;
    uint32_t queued_frames;
    uint64_t phase_q32;
    uint64_t step_q32;
    uint64_t underrun_frames;
    uint64_t overrun_frames;
    int16_t ring[NEXUS_AUDIO_STREAM_RING_FRAMES * 2U];
};

static struct nexus_audio_device devices[NEXUS_AUDIO_MAX_DEVICES];
static struct nexus_audio_stream streams[NEXUS_AUDIO_MAX_STREAMS];
static struct nexus_audio_stats statistics;
static uint32_t default_output_device;
static uint32_t default_capture_device;
static uint32_t next_device_id = 1U;
static uint32_t next_stream_id = 1U;
static uint8_t master_volume_percent = 75U;
static bool master_muted;
static int16_t ac97_mix_buffer[NEXUS_AUDIO_MIX_MAX_FRAMES * 2U];

static bool direction_valid(enum nexus_audio_direction direction) {
    return direction == NEXUS_AUDIO_PLAYBACK ||
           direction == NEXUS_AUDIO_CAPTURE;
}

static bool device_type_valid(enum nexus_audio_device_type type) {
    return type >= NEXUS_AUDIO_DEVICE_AC97 &&
           type <= NEXUS_AUDIO_DEVICE_BLUETOOTH;
}

static bool labels_equal(const char *left, const char *right) {
    if (left == NULL || right == NULL) return false;
    for (size_t i = 0U; i <= NEXUS_AUDIO_DEVICE_LABEL_MAX; ++i) {
        if (left[i] != right[i]) return false;
        if (left[i] == '\0') return true;
    }
    return false;
}

static bool label_valid(const char label[NEXUS_AUDIO_DEVICE_LABEL_MAX + 1U]) {
    if (label == NULL || label[0] == '\0') return false;
    for (size_t i = 0U; i <= NEXUS_AUDIO_DEVICE_LABEL_MAX; ++i) {
        const unsigned char value = (unsigned char)label[i];
        if (value == 0U) return i != 0U;
        if (value < 0x20U || value > 0x7EU) return false;
    }
    return false;
}

static struct nexus_audio_device *find_device(uint32_t id) {
    if (id == 0U) return NULL;
    for (size_t i = 0U; i < NEXUS_AUDIO_MAX_DEVICES; ++i)
        if (devices[i].present && devices[i].id == id)
            return &devices[i];
    return NULL;
}

static struct nexus_audio_stream *find_stream(uint32_t id) {
    if (id == 0U) return NULL;
    for (size_t i = 0U; i < NEXUS_AUDIO_MAX_STREAMS; ++i)
        if (streams[i].active && streams[i].id == id)
            return &streams[i];
    return NULL;
}

static uint32_t allocate_device_id(void) {
    for (size_t attempt = 0U; attempt <= NEXUS_AUDIO_MAX_DEVICES; ++attempt) {
        uint32_t candidate = next_device_id++;
        if (next_device_id == 0U) next_device_id = 1U;
        if (candidate != 0U && find_device(candidate) == NULL)
            return candidate;
    }
    return 0U;
}

static uint32_t allocate_stream_id(void) {
    for (size_t attempt = 0U; attempt <= NEXUS_AUDIO_MAX_STREAMS; ++attempt) {
        uint32_t candidate = next_stream_id++;
        if (next_stream_id == 0U) next_stream_id = 1U;
        if (candidate != 0U && find_stream(candidate) == NULL)
            return candidate;
    }
    return 0U;
}

static struct nexus_audio_device *default_device(
    enum nexus_audio_direction direction) {
    const uint32_t id = direction == NEXUS_AUDIO_PLAYBACK
        ? default_output_device : default_capture_device;
    struct nexus_audio_device *device = find_device(id);
    return device != NULL && device->ready &&
           device->description.direction == direction ? device : NULL;
}

static bool device_is_usable(uint32_t id,
                             enum nexus_audio_direction direction) {
    struct nexus_audio_device *device = id == 0U
        ? default_device(direction) : find_device(id);
    return device != NULL && device->ready &&
           device->description.direction == direction;
}

static void choose_replacement_default(enum nexus_audio_direction direction) {
    uint32_t *selected = direction == NEXUS_AUDIO_PLAYBACK
        ? &default_output_device : &default_capture_device;
    *selected = 0U;
    for (size_t i = 0U; i < NEXUS_AUDIO_MAX_DEVICES; ++i) {
        if (devices[i].present && devices[i].ready &&
            devices[i].description.direction == direction) {
            *selected = devices[i].id;
            return;
        }
    }
}

bool nexus_audio_init(void) {
    for (size_t i = 0U; i < NEXUS_AUDIO_MAX_DEVICES; ++i)
        devices[i] = (struct nexus_audio_device){0};
    for (size_t i = 0U; i < NEXUS_AUDIO_MAX_STREAMS; ++i)
        streams[i] = (struct nexus_audio_stream){0};
    statistics = (struct nexus_audio_stats){0};
    default_output_device = 0U;
    default_capture_device = 0U;
    next_device_id = 1U;
    next_stream_id = 1U;
    master_volume_percent = 75U;
    master_muted = false;

    if (ac97_is_ready()) {
        const struct nexus_audio_device_desc ac97 = {
            .type = NEXUS_AUDIO_DEVICE_AC97,
            .direction = NEXUS_AUDIO_PLAYBACK,
            .sample_rate = NEXUS_AUDIO_MIX_RATE,
            .channels = 2U,
            .capabilities = NEXUS_AUDIO_DEVICE_VOLUME_CONTROL |
                            NEXUS_AUDIO_DEVICE_MUTE_CONTROL |
                            NEXUS_AUDIO_DEVICE_LOW_LATENCY,
            .label = "AC97 Analog Output"
        };
        if (nexus_audio_register_device(&ac97) == 0U) return false;
        /* Use the software mixer for master/stream volume; leave hardware
         * attenuation at unity so the master gain is applied exactly once.
         */
        (void)ac97_set_master_volume(100U);
    }
    return true;
}

size_t nexus_audio_device_count(void) {
    size_t count = 0U;
    for (size_t i = 0U; i < NEXUS_AUDIO_MAX_DEVICES; ++i)
        if (devices[i].present) ++count;
    return count;
}

bool nexus_audio_device_get(size_t index, struct nexus_audio_device *device) {
    if (device == NULL) return false;
    size_t current = 0U;
    for (size_t i = 0U; i < NEXUS_AUDIO_MAX_DEVICES; ++i) {
        if (!devices[i].present) continue;
        if (current++ == index) {
            *device = devices[i];
            return true;
        }
    }
    return false;
}

uint32_t nexus_audio_default_device(enum nexus_audio_direction direction) {
    if (!direction_valid(direction)) return 0U;
    struct nexus_audio_device *device = default_device(direction);
    return device != NULL ? device->id : 0U;
}

uint32_t nexus_audio_register_device(
    const struct nexus_audio_device_desc *description) {
    if (description == NULL || !device_type_valid(description->type) ||
        !direction_valid(description->direction) ||
        description->sample_rate < NEXUS_AUDIO_MIN_SAMPLE_RATE ||
        description->sample_rate > NEXUS_AUDIO_MAX_SAMPLE_RATE ||
        description->channels == 0U || description->channels > 2U ||
        !label_valid(description->label))
        return 0U;

    for (size_t i = 0U; i < NEXUS_AUDIO_MAX_DEVICES; ++i) {
        if (devices[i].present &&
            devices[i].description.type == description->type &&
            devices[i].description.direction == description->direction &&
            devices[i].description.sample_rate == description->sample_rate &&
            devices[i].description.channels == description->channels &&
            labels_equal(devices[i].description.label, description->label)) {
            devices[i].description = *description;
            devices[i].ready = true;
            return devices[i].id;
        }
    }
    size_t slot = NEXUS_AUDIO_MAX_DEVICES;
    for (size_t i = 0U; i < NEXUS_AUDIO_MAX_DEVICES; ++i) {
        if (!devices[i].present) {
            slot = i;
            break;
        }
    }
    if (slot == NEXUS_AUDIO_MAX_DEVICES) return 0U;
    const uint32_t id = allocate_device_id();
    if (id == 0U) return 0U;
    devices[slot] = (struct nexus_audio_device){
        .id = id,
        .description = *description,
        .present = true,
        .ready = true
    };
    if (description->direction == NEXUS_AUDIO_PLAYBACK &&
        default_output_device == 0U)
        default_output_device = id;
    if (description->direction == NEXUS_AUDIO_CAPTURE &&
        default_capture_device == 0U)
        default_capture_device = id;
    return id;
}

bool nexus_audio_set_default_device(enum nexus_audio_direction direction,
                                    uint32_t device_id) {
    if (!direction_valid(direction)) return false;
    if (device_id != 0U && !device_is_usable(device_id, direction))
        return false;
    if (device_id == 0U) {
        if (direction == NEXUS_AUDIO_PLAYBACK) default_output_device = 0U;
        else default_capture_device = 0U;
        return true;
    }
    if (direction == NEXUS_AUDIO_PLAYBACK) default_output_device = device_id;
    else default_capture_device = device_id;
    return true;
}

bool nexus_audio_set_device_ready(uint32_t device_id, bool ready) {
    struct nexus_audio_device *device = find_device(device_id);
    if (device == NULL) return false;
    device->ready = ready;
    if (!ready &&
        ((device->description.direction == NEXUS_AUDIO_PLAYBACK &&
          default_output_device == device_id) ||
         (device->description.direction == NEXUS_AUDIO_CAPTURE &&
          default_capture_device == device_id)))
        choose_replacement_default(device->description.direction);
    return true;
}

bool nexus_audio_remove_device(uint32_t device_id) {
    struct nexus_audio_device *device = find_device(device_id);
    if (device == NULL) return false;
    const enum nexus_audio_direction direction = device->description.direction;
    device->present = false;
    device->ready = false;
    if ((direction == NEXUS_AUDIO_PLAYBACK &&
         default_output_device == device_id) ||
        (direction == NEXUS_AUDIO_CAPTURE &&
         default_capture_device == device_id))
        choose_replacement_default(direction);
    return true;
}

void nexus_audio_set_master_volume(uint8_t volume_percent) {
    if (volume_percent <= AUDIO_VOLUME_MAX)
        master_volume_percent = volume_percent;
}

uint8_t nexus_audio_get_master_volume(void) {
    return master_volume_percent;
}

void nexus_audio_set_master_mute(bool muted) {
    master_muted = muted;
}

bool nexus_audio_get_master_mute(void) {
    return master_muted;
}

bool nexus_audio_stream_open(
    uint32_t application_id, enum nexus_audio_direction direction,
    uint32_t device_id, uint32_t sample_rate, uint8_t channels,
    uint8_t volume_percent, uint32_t *stream_id) {
    if (stream_id == NULL || !direction_valid(direction) ||
        sample_rate < NEXUS_AUDIO_MIN_SAMPLE_RATE ||
        sample_rate > NEXUS_AUDIO_MAX_SAMPLE_RATE ||
        channels == 0U || channels > 2U ||
        volume_percent > AUDIO_VOLUME_MAX ||
        !device_is_usable(device_id, direction))
        return false;

    struct nexus_audio_device *device = device_id == 0U
        ? default_device(direction) : find_device(device_id);
    if (device == NULL) return false;
    if (direction == NEXUS_AUDIO_CAPTURE &&
        (sample_rate != device->description.sample_rate ||
         channels > device->description.channels))
        return false;

    size_t slot = NEXUS_AUDIO_MAX_STREAMS;
    for (size_t i = 0U; i < NEXUS_AUDIO_MAX_STREAMS; ++i) {
        if (!streams[i].active) {
            slot = i;
            break;
        }
    }
    if (slot == NEXUS_AUDIO_MAX_STREAMS) return false;
    const uint32_t id = allocate_stream_id();
    if (id == 0U) return false;

    streams[slot] = (struct nexus_audio_stream){
        .active = true,
        .id = id,
        .application_id = application_id,
        .device_id = device_id,
        .direction = direction,
        .sample_rate = sample_rate,
        .channels = channels,
        .volume_percent = volume_percent,
        .step_q32 = ((uint64_t)sample_rate << 32) / NEXUS_AUDIO_MIX_RATE
    };
    *stream_id = id;
    return true;
}

bool nexus_audio_stream_close(uint32_t stream_id) {
    struct nexus_audio_stream *stream = find_stream(stream_id);
    if (stream == NULL) return false;
    *stream = (struct nexus_audio_stream){0};
    return true;
}

bool nexus_audio_stream_get_info(size_t index,
                                 struct nexus_audio_stream_info *info) {
    if (info == NULL) return false;
    size_t current = 0U;
    for (size_t i = 0U; i < NEXUS_AUDIO_MAX_STREAMS; ++i) {
        if (!streams[i].active) continue;
        if (current++ == index) {
            const struct nexus_audio_stream *stream = &streams[i];
            *info = (struct nexus_audio_stream_info){
                .id = stream->id,
                .application_id = stream->application_id,
                .device_id = stream->device_id,
                .direction = stream->direction,
                .sample_rate = stream->sample_rate,
                .channels = stream->channels,
                .volume_percent = stream->volume_percent,
                .muted = stream->muted,
                .queued_frames = stream->queued_frames,
                .underrun_frames = stream->underrun_frames,
                .overrun_frames = stream->overrun_frames
            };
            return true;
        }
    }
    return false;
}

bool nexus_audio_stream_set_volume(uint32_t stream_id,
                                   uint8_t volume_percent) {
    struct nexus_audio_stream *stream = find_stream(stream_id);
    if (stream == NULL || volume_percent > AUDIO_VOLUME_MAX) return false;
    stream->volume_percent = volume_percent;
    return true;
}

bool nexus_audio_stream_set_mute(uint32_t stream_id, bool muted) {
    struct nexus_audio_stream *stream = find_stream(stream_id);
    if (stream == NULL) return false;
    stream->muted = muted;
    return true;
}

bool nexus_audio_stream_set_device(uint32_t stream_id, uint32_t device_id) {
    struct nexus_audio_stream *stream = find_stream(stream_id);
    if (stream == NULL ||
        !device_is_usable(device_id, stream->direction))
        return false;
    struct nexus_audio_device *device = device_id == 0U
        ? default_device(stream->direction) : find_device(device_id);
    if (device == NULL) return false;
    if (stream->direction == NEXUS_AUDIO_CAPTURE &&
        (stream->sample_rate != device->description.sample_rate ||
         stream->channels > device->description.channels))
        return false;
    stream->device_id = device_id;
    return true;
}

size_t nexus_audio_set_application_volume(
    uint32_t application_id, uint8_t volume_percent) {
    if (volume_percent > AUDIO_VOLUME_MAX) return 0U;
    size_t changed = 0U;
    for (size_t i = 0U; i < NEXUS_AUDIO_MAX_STREAMS; ++i) {
        if (streams[i].active &&
            streams[i].application_id == application_id) {
            streams[i].volume_percent = volume_percent;
            ++changed;
        }
    }
    return changed;
}

static void ring_write_frame(struct nexus_audio_stream *stream,
                             const int16_t *samples, uint8_t input_channels,
                             size_t input_frame) {
    const uint32_t slot = stream->write_index & AUDIO_RING_MASK;
    int16_t left;
    int16_t right;
    if (input_channels == 1U) {
        left = samples[input_frame];
        right = left;
    } else {
        left = samples[input_frame * 2U];
        right = samples[input_frame * 2U + 1U];
    }
    if (stream->channels == 1U) {
        const int16_t mono = input_channels == 1U ? left :
            (int16_t)(((int32_t)left + (int32_t)right) / 2);
        /* Duplicate mono into both mixer channels so playback remains
         * centered rather than being attenuated into the left channel only.
         */
        stream->ring[(size_t)slot * 2U] = mono;
        stream->ring[(size_t)slot * 2U + 1U] = mono;
    } else {
        stream->ring[(size_t)slot * 2U] = left;
        stream->ring[(size_t)slot * 2U + 1U] = right;
    }
    stream->write_index = (stream->write_index + 1U) & AUDIO_RING_MASK;
    ++stream->queued_frames;
}

size_t nexus_audio_stream_write_s16(
    uint32_t stream_id, const int16_t *samples, size_t frames) {
    struct nexus_audio_stream *stream = find_stream(stream_id);
    if (stream == NULL || stream->direction != NEXUS_AUDIO_PLAYBACK ||
        (frames != 0U && samples == NULL))
        return 0U;
    const size_t free_frames =
        NEXUS_AUDIO_STREAM_RING_FRAMES - stream->queued_frames;
    const size_t accepted = frames < free_frames ? frames : free_frames;
    for (size_t i = 0U; i < accepted; ++i)
        ring_write_frame(stream, samples, stream->channels, i);
    stream->overrun_frames =
        stream->overrun_frames > UINT64_MAX - (frames - accepted)
            ? UINT64_MAX : stream->overrun_frames + (frames - accepted);
    return accepted;
}

size_t nexus_audio_capture_publish(
    uint32_t device_id, const int16_t *samples, size_t frames) {
    struct nexus_audio_device *device = find_device(device_id);
    if (device == NULL || !device->ready ||
        device->description.direction != NEXUS_AUDIO_CAPTURE ||
        (frames != 0U && samples == NULL))
        return 0U;

    size_t published = 0U;
    for (size_t i = 0U; i < NEXUS_AUDIO_MAX_STREAMS; ++i) {
        struct nexus_audio_stream *stream = &streams[i];
        if (!stream->active || stream->direction != NEXUS_AUDIO_CAPTURE ||
            (stream->device_id != 0U && stream->device_id != device_id) ||
            (stream->device_id == 0U && device_id != default_capture_device))
            continue;
        const size_t free_frames =
            NEXUS_AUDIO_STREAM_RING_FRAMES - stream->queued_frames;
        const size_t accepted = frames < free_frames ? frames : free_frames;
        for (size_t frame = 0U; frame < accepted; ++frame)
            ring_write_frame(stream, samples, device->description.channels,
                             frame);
        const size_t dropped = frames - accepted;
        stream->overrun_frames =
            stream->overrun_frames > UINT64_MAX - dropped
                ? UINT64_MAX : stream->overrun_frames + dropped;
        if (dropped != 0U)
            statistics.capture_overrun_frames =
                statistics.capture_overrun_frames > UINT64_MAX - dropped
                    ? UINT64_MAX : statistics.capture_overrun_frames + dropped;
        published += accepted;
    }
    device->frames_captured =
        device->frames_captured > UINT64_MAX - frames
            ? UINT64_MAX : device->frames_captured + frames;
    return published;
}

size_t nexus_audio_stream_read_s16(
    uint32_t stream_id, int16_t *samples, size_t frame_capacity) {
    struct nexus_audio_stream *stream = find_stream(stream_id);
    if (stream == NULL || stream->direction != NEXUS_AUDIO_CAPTURE ||
        (frame_capacity != 0U && samples == NULL))
        return 0U;
    const size_t frames = frame_capacity < stream->queued_frames
        ? frame_capacity : stream->queued_frames;
    for (size_t i = 0U; i < frames; ++i) {
        const uint32_t slot = stream->read_index & AUDIO_RING_MASK;
        if (stream->channels == 1U) {
            samples[i] = stream->ring[(size_t)slot * 2U];
        } else {
            samples[i * 2U] = stream->ring[(size_t)slot * 2U];
            samples[i * 2U + 1U] = stream->ring[(size_t)slot * 2U + 1U];
        }
        stream->read_index = (stream->read_index + 1U) & AUDIO_RING_MASK;
        --stream->queued_frames;
    }
    return frames;
}

static int16_t clamp_s16(int64_t value) {
    if (value > INT16_MAX) return INT16_MAX;
    if (value < INT16_MIN) return INT16_MIN;
    return (int16_t)value;
}

static bool stream_next_resampled(struct nexus_audio_stream *stream,
                                  int32_t *left, int32_t *right) {
    if (stream->queued_frames == 0U) {
        stream->phase_q32 = 0U;
        stream->underrun_frames =
            stream->underrun_frames == UINT64_MAX
                ? UINT64_MAX : stream->underrun_frames + 1U;
        return false;
    }
    const uint64_t source_index = stream->phase_q32 >> 32;
    if (source_index >= stream->queued_frames) {
        stream->read_index =
            (stream->read_index + stream->queued_frames) & AUDIO_RING_MASK;
        stream->queued_frames = 0U;
        stream->phase_q32 = 0U;
        stream->underrun_frames =
            stream->underrun_frames == UINT64_MAX
                ? UINT64_MAX : stream->underrun_frames + 1U;
        return false;
    }

    const uint32_t first_slot =
        (stream->read_index + (uint32_t)source_index) & AUDIO_RING_MASK;
    const uint32_t next_slot =
        source_index + 1U < stream->queued_frames
            ? (first_slot + 1U) & AUDIO_RING_MASK : first_slot;
    const uint32_t fraction = (uint32_t)stream->phase_q32;
    const int32_t l0 = stream->ring[(size_t)first_slot * 2U];
    const int32_t r0 = stream->ring[(size_t)first_slot * 2U + 1U];
    const int32_t l1 = stream->ring[(size_t)next_slot * 2U];
    const int32_t r1 = stream->ring[(size_t)next_slot * 2U + 1U];
    *left = l0 + (int32_t)(((int64_t)(l1 - l0) * fraction) /
                           (int64_t)AUDIO_Q32_ONE);
    *right = r0 + (int32_t)(((int64_t)(r1 - r0) * fraction) /
                            (int64_t)AUDIO_Q32_ONE);

    stream->phase_q32 += stream->step_q32;
    uint64_t consumed = stream->phase_q32 >> 32;
    if (consumed > stream->queued_frames) {
        consumed = stream->queued_frames;
        stream->phase_q32 = 0U;
    } else {
        stream->phase_q32 &= UINT32_MAX;
    }
    stream->read_index =
        (stream->read_index + (uint32_t)consumed) & AUDIO_RING_MASK;
    stream->queued_frames -= (uint32_t)consumed;
    return true;
}

bool nexus_audio_mix_s16_stereo_48k(
    int16_t *output, uint32_t frames, bool *had_audio) {
    if (output == NULL || had_audio == NULL || frames == 0U ||
        frames > NEXUS_AUDIO_MIX_MAX_FRAMES)
        return false;
    *had_audio = false;
    struct nexus_audio_device *output_device =
        default_device(NEXUS_AUDIO_PLAYBACK);
    if (output_device == NULL) {
        for (uint32_t i = 0U; i < frames * 2U; ++i) output[i] = 0;
        return true;
    }

    for (uint32_t frame = 0U; frame < frames; ++frame) {
        int64_t mixed_left = 0;
        int64_t mixed_right = 0;
        for (size_t i = 0U; i < NEXUS_AUDIO_MAX_STREAMS; ++i) {
            struct nexus_audio_stream *stream = &streams[i];
            if (!stream->active ||
                stream->direction != NEXUS_AUDIO_PLAYBACK ||
                stream->muted || stream->volume_percent == 0U ||
                (stream->device_id != 0U &&
                 stream->device_id != output_device->id))
                continue;
            int32_t left = 0;
            int32_t right = 0;
            if (!stream_next_resampled(stream, &left, &right)) {
                statistics.playback_underrun_frames =
                    statistics.playback_underrun_frames == UINT64_MAX
                        ? UINT64_MAX : statistics.playback_underrun_frames + 1U;
                continue;
            }
            *had_audio = true;
            mixed_left += (int64_t)left * stream->volume_percent / 100;
            mixed_right += (int64_t)right * stream->volume_percent / 100;
        }
        if (master_muted) {
            mixed_left = 0;
            mixed_right = 0;
        } else {
            mixed_left = mixed_left * master_volume_percent / 100;
            mixed_right = mixed_right * master_volume_percent / 100;
        }
        output[(size_t)frame * 2U] = clamp_s16(mixed_left);
        output[(size_t)frame * 2U + 1U] = clamp_s16(mixed_right);
    }
    statistics.mix_calls =
        statistics.mix_calls == UINT64_MAX ? UINT64_MAX
                                            : statistics.mix_calls + 1U;
    statistics.mixed_frames =
        statistics.mixed_frames > UINT64_MAX - frames
            ? UINT64_MAX : statistics.mixed_frames + frames;
    return true;
}

void nexus_audio_poll(void) {
    if (!ac97_is_ready()) return;
    ac97_poll();
    if (!ac97_playback_idle()) return;
    struct nexus_audio_device *output = default_device(NEXUS_AUDIO_PLAYBACK);
    if (output == NULL || output->description.type != NEXUS_AUDIO_DEVICE_AC97)
        return;

    bool had_audio = false;
    if (!nexus_audio_mix_s16_stereo_48k(
            ac97_mix_buffer, AUDIO_AC97_BLOCK_FRAMES, &had_audio) ||
        !had_audio)
        return;
    const enum nexus_ac97_submit_result result =
        ac97_write_pcm_s16_stereo_48k(ac97_mix_buffer,
                                      AUDIO_AC97_BLOCK_FRAMES);
    if (result == NEXUS_AC97_SUBMIT_OK) {
        output->frames_submitted =
            output->frames_submitted > UINT64_MAX - AUDIO_AC97_BLOCK_FRAMES
                ? UINT64_MAX
                : output->frames_submitted + AUDIO_AC97_BLOCK_FRAMES;
    } else if (result != NEXUS_AC97_SUBMIT_BUSY) {
        output->errors = output->errors == UINT64_MAX
            ? UINT64_MAX : output->errors + 1U;
        statistics.backend_submit_errors =
            statistics.backend_submit_errors == UINT64_MAX
                ? UINT64_MAX : statistics.backend_submit_errors + 1U;
    }
}

void nexus_audio_get_stats(struct nexus_audio_stats *stats) {
    if (stats != NULL) *stats = statistics;
}
