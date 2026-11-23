#include "usb_hid_gamepad.h"

#include <limits.h>

#define HID_LOCAL_USAGES 32U
#define HID_GLOBAL_STACK 4U
#define HID_MAX_COLLECTION_DEPTH 8U

struct hid_globals {
    uint32_t usage_page;
    int32_t logical_min;
    int32_t logical_max;
    uint32_t report_size;
    uint32_t report_count;
    uint8_t report_id;
};

struct hid_local {
    uint32_t usages[HID_LOCAL_USAGES];
    uint8_t count;
    bool have_min;
    bool have_max;
    uint32_t usage_min;
    uint32_t usage_max;
};

bool nexus_hid_gamepad_find_interface(
    const uint8_t *configuration, size_t length,
    struct nexus_hid_gamepad_interface *interface) {
    if (configuration == NULL || interface == NULL || length < 9U ||
        configuration[0] < 9U || configuration[1] != 2U ||
        configuration[0] > length || configuration[4] == 0U ||
        configuration[5] == 0U) return false;

    const uint16_t total_length = (uint16_t)(
        configuration[2] | ((uint16_t)configuration[3] << 8));
    if (total_length < configuration[0] || total_length > length)
        return false;

    bool gamepad_interface = false;
    bool have_report_descriptor = false;
    uint8_t current_interface = 0U;
    uint8_t expected_endpoints = 0U;
    uint8_t seen_endpoints = 0U;
    uint16_t current_report_length = 0U;
    bool have_candidate = false;
    struct nexus_hid_gamepad_interface candidate = {0};
    for (size_t offset = configuration[0]; offset < total_length;) {
        if (total_length - offset < 2U) return false;
        const uint8_t descriptor_length = configuration[offset];
        const uint8_t descriptor_type = configuration[offset + 1U];
        if (descriptor_length < 2U ||
            descriptor_length > total_length - offset) return false;

        if (descriptor_type == 4U) {
            if (descriptor_length < 9U) return false;
            if (gamepad_interface) {
                if (seen_endpoints != expected_endpoints) return false;
                if (have_candidate) {
                    *interface = candidate;
                    return true;
                }
            }
            gamepad_interface = configuration[offset + 3U] == 0U &&
                configuration[offset + 5U] == 3U &&
                configuration[offset + 6U] == 0U &&
                configuration[offset + 7U] == 0U &&
                configuration[offset + 4U] != 0U;
            current_interface = gamepad_interface
                ? configuration[offset + 2U] : 0U;
            expected_endpoints = gamepad_interface
                ? configuration[offset + 4U] : 0U;
            seen_endpoints = 0U;
            have_candidate = false;
            have_report_descriptor = false;
            current_report_length = 0U;
        } else if (descriptor_type == 0x21U && gamepad_interface) {
            if (seen_endpoints != 0U || descriptor_length < 6U)
                return false;
            const uint8_t count = configuration[offset + 5U];
            if ((size_t)count > (descriptor_length - 6U) / 3U)
                return false;
            for (uint8_t i = 0U; i < count; ++i) {
                const size_t item = offset + 6U + (size_t)i * 3U;
                if (configuration[item] != 0x22U) continue;
                const uint16_t report_length = (uint16_t)(
                    configuration[item + 1U] |
                    ((uint16_t)configuration[item + 2U] << 8));
                if (report_length == 0U ||
                    report_length > NEXUS_HID_GAMEPAD_MAX_DESCRIPTOR_BYTES)
                    return false;
                current_report_length = report_length;
                have_report_descriptor = true;
                break;
            }
        } else if (descriptor_type == 5U && gamepad_interface) {
            if (descriptor_length < 7U ||
                seen_endpoints >= expected_endpoints) return false;
            ++seen_endpoints;
            if (!have_report_descriptor) return false;
            const uint8_t address = configuration[offset + 2U];
            const uint8_t attributes = configuration[offset + 3U] & 0x03U;
            const uint16_t raw_packet = (uint16_t)(
                configuration[offset + 4U] |
                ((uint16_t)configuration[offset + 5U] << 8));
            const uint16_t packet = raw_packet & 0x07FFU;
            const uint8_t interval = configuration[offset + 6U];
            if ((address & 0x70U) != 0U || (address & 0x0FU) == 0U ||
                attributes != 3U || (raw_packet & 0xF800U) != 0U ||
                packet == 0U || packet > NEXUS_HID_GAMEPAD_MAX_REPORT_BYTES ||
                interval == 0U) return false;
            if ((address & 0x80U) != 0U) {
                candidate = (struct nexus_hid_gamepad_interface){
                    .interface_number = current_interface,
                    .configuration_value = configuration[5],
                    .endpoint_address = address,
                    .interval = interval,
                    .max_packet = packet,
                    .report_descriptor_length = current_report_length
                };
                have_candidate = true;
            }
        }
        offset += descriptor_length;
    }
    if (gamepad_interface && seen_endpoints != expected_endpoints)
        return false;
    if (have_candidate) {
        *interface = candidate;
        return true;
    }
    return false;
}

static uint32_t read_unsigned(const uint8_t *p, uint8_t n) {
    uint32_t value = 0U;
    for (uint8_t i = 0U; i < n; ++i) value |= (uint32_t)p[i] << (8U * i);
    return value;
}

static int32_t read_signed(const uint8_t *p, uint8_t n) {
    const uint32_t value = read_unsigned(p, n);
    if (n == 0U || n == 4U) return (int32_t)value;
    const uint8_t bits = (uint8_t)(n * 8U);
    const uint32_t sign = 1U << (bits - 1U);
    return (int32_t)((value ^ sign) - sign);
}

static uint32_t local_usage(const struct hid_local *local, uint32_t index,
                            uint32_t page) {
    if (index < local->count) {
        const uint32_t usage = local->usages[index];
        return usage <= 0xFFFFU ? (page << 16) | usage : usage;
    }
    if (local->have_min && local->have_max &&
        local->usage_min <= local->usage_max) {
        const uint32_t range_index = index - local->count;
        const uint32_t usage = local->usage_min + range_index;
        if (usage <= local->usage_max)
            return (page << 16) | usage;
    }
    if (local->count != 0U) {
        const uint32_t usage = local->usages[local->count - 1U];
        return usage <= 0xFFFFU ? (page << 16) | usage : usage;
    }
    return 0U;
}

static void clear_local(struct hid_local *local) {
    local->count = 0U;
    local->have_min = false;
    local->have_max = false;
    local->usage_min = 0U;
    local->usage_max = 0U;
}

static bool add_input_fields(const struct hid_globals *g,
                             const struct hid_local *local,
                             uint32_t flags, bool gamepad,
                             uint32_t *bit_offset,
                             struct nexus_hid_gamepad_layout *layout) {
    if (g->report_size == 0U || g->report_size > 16U ||
        g->report_count == 0U || g->report_count > 64U ||
        *bit_offset > NEXUS_HID_GAMEPAD_MAX_REPORT_BYTES * 8U ||
        g->report_count > (NEXUS_HID_GAMEPAD_MAX_REPORT_BYTES * 8U -
                           *bit_offset) / g->report_size) return false;

    const uint32_t start = *bit_offset;
    *bit_offset += g->report_size * g->report_count;
    if ((flags & 0x01U) != 0U || (flags & 0x02U) == 0U || !gamepad)
        return true;

    for (uint32_t i = 0U; i < g->report_count; ++i) {
        const uint32_t usage = local_usage(local, i, g->usage_page);
        const uint32_t page = usage >> 16;
        const uint32_t id = usage & 0xFFFFU;
        const uint32_t offset = start + i * g->report_size;
        if (offset > UINT16_MAX) return false;

        if (page == 0x09U) {
            if (layout->button_count >= NEXUS_HID_GAMEPAD_MAX_BUTTONS)
                return false;
            layout->button_bit_offsets[layout->button_count++] =
                (uint16_t)offset;
        } else if (page == 0x01U && id >= 0x30U && id <= 0x37U) {
            if (g->logical_min > g->logical_max ||
                layout->axis_count >= NEXUS_HID_GAMEPAD_MAX_AXES)
                return false;
            struct nexus_hid_gamepad_axis *axis =
                &layout->axes[layout->axis_count++];
            axis->bit_offset = (uint16_t)offset;
            axis->bit_size = (uint8_t)g->report_size;
            axis->logical_min = g->logical_min;
            axis->logical_max = g->logical_max;
            axis->usage = (uint16_t)id;
        } else if (page == 0x01U && id == 0x39U) {
            if (g->logical_min > g->logical_max || layout->has_hat)
                return false;
            layout->has_hat = true;
            layout->hat_bit_offset = (uint16_t)offset;
            layout->hat_bit_size = (uint8_t)g->report_size;
            layout->hat_logical_min = g->logical_min;
            layout->hat_logical_max = g->logical_max;
        }
    }
    return true;
}

bool nexus_hid_gamepad_parse_descriptor(
    const uint8_t *descriptor, size_t length,
    struct nexus_hid_gamepad_layout *layout) {
    if (descriptor == NULL || layout == NULL || length == 0U ||
        length > NEXUS_HID_GAMEPAD_MAX_DESCRIPTOR_BYTES) return false;

    struct nexus_hid_gamepad_layout parsed = {0};
    struct hid_globals globals = {0};
    struct hid_globals stack[HID_GLOBAL_STACK];
    uint8_t stack_depth = 0U;
    struct hid_local local = {0};
    bool collections[HID_MAX_COLLECTION_DEPTH] = {false};
    uint8_t collection_depth = 0U;
    bool gamepad_application = false;
    uint32_t report_bits = 0U;
    size_t pos = 0U;

    while (pos < length) {
        const uint8_t prefix = descriptor[pos++];
        if (prefix == 0xFEU) {
            if (length - pos < 2U) return false;
            const uint8_t data_length = descriptor[pos++];
            ++pos; /* Long-item tag. */
            if (data_length > length - pos) return false;
            pos += data_length;
            continue;
        }
        uint8_t size = (uint8_t)(prefix & 0x03U);
        if (size == 3U) size = 4U;
        const uint8_t type = (uint8_t)((prefix >> 2) & 0x03U);
        const uint8_t tag = (uint8_t)(prefix >> 4);
        if (size > length - pos) return false;
        const uint8_t *data = descriptor + pos;
        const uint32_t u = read_unsigned(data, size);
        const int32_t s = read_signed(data, size);
        pos += size;

        if (type == 1U) { /* Global items. */
            switch (tag) {
            case 0U: globals.usage_page = u; break;
            case 1U: globals.logical_min = s; break;
            case 2U:
                globals.logical_max = globals.logical_min < 0 ? s :
                    (u > INT32_MAX ? INT32_MAX : (int32_t)u);
                break;
            case 7U: globals.report_size = u; break;
            case 8U:
                if (u == 0U || u > 255U) return false;
                if (parsed.has_report_id && parsed.report_id != (uint8_t)u)
                    return false;
                parsed.has_report_id = true;
                parsed.report_id = (uint8_t)u;
                break;
            case 9U: globals.report_count = u; break;
            case 10U:
                if (stack_depth >= HID_GLOBAL_STACK) return false;
                stack[stack_depth++] = globals;
                break;
            case 11U:
                if (stack_depth == 0U) return false;
                globals = stack[--stack_depth];
                break;
            default: break;
            }
        } else if (type == 2U) { /* Local items. */
            switch (tag) {
            case 0U:
                if (local.count >= HID_LOCAL_USAGES) return false;
                local.usages[local.count++] = u;
                break;
            case 1U: local.usage_min = u; local.have_min = true; break;
            case 2U: local.usage_max = u; local.have_max = true; break;
            default: break;
            }
        } else if (type == 0U) { /* Main items. */
            if (tag == 10U) { /* Collection. */
                if (collection_depth >= HID_MAX_COLLECTION_DEPTH)
                    return false;
                const uint32_t usage = local_usage(&local, 0U,
                                                    globals.usage_page);
                const bool app = u == 1U && (usage >> 16) == 1U &&
                    (((usage & 0xFFFFU) == 4U) ||
                     ((usage & 0xFFFFU) == 5U) ||
                     ((usage & 0xFFFFU) == 8U));
                collections[collection_depth++] =
                    gamepad_application || app;
                gamepad_application = collections[collection_depth - 1U];
            } else if (tag == 12U) { /* End Collection. */
                if (collection_depth == 0U) return false;
                --collection_depth;
                gamepad_application = collection_depth == 0U ? false :
                    collections[collection_depth - 1U];
            } else if (tag == 8U) { /* Input. */
                if (!add_input_fields(&globals, &local, u,
                                      gamepad_application, &report_bits,
                                      &parsed)) return false;
            }
            clear_local(&local);
        }
    }

    if (collection_depth != 0U || report_bits == 0U ||
        (parsed.axis_count == 0U && parsed.button_count == 0U &&
         !parsed.has_hat)) return false;
    parsed.report_bytes = (uint8_t)((report_bits + 7U) / 8U);
    if (parsed.report_bytes == 0U ||
        parsed.report_bytes > NEXUS_HID_GAMEPAD_MAX_REPORT_BYTES) return false;
    *layout = parsed;
    return true;
}

static bool extract_bits(const uint8_t *data, size_t bytes,
                         uint16_t offset, uint8_t width, uint32_t *value) {
    if (data == NULL || value == NULL || width == 0U || width > 16U ||
        (size_t)offset + width > bytes * 8U) return false;
    uint32_t result = 0U;
    for (uint8_t bit = 0U; bit < width; ++bit) {
        const size_t source = (size_t)offset + bit;
        if ((data[source / 8U] & (uint8_t)(1U << (source % 8U))) != 0U)
            result |= 1U << bit;
    }
    *value = result;
    return true;
}

static int32_t sign_extend(uint32_t value, uint8_t width) {
    const uint32_t sign = 1U << (width - 1U);
    return (int32_t)((value ^ sign) - sign);
}

bool nexus_hid_gamepad_decode_report(
    const struct nexus_hid_gamepad_layout *layout,
    const uint8_t *report, size_t length,
    struct nexus_hid_gamepad_state *state) {
    if (layout == NULL || report == NULL || state == NULL ||
        layout->report_bytes == 0U ||
        layout->report_bytes > NEXUS_HID_GAMEPAD_MAX_REPORT_BYTES ||
        layout->axis_count > NEXUS_HID_GAMEPAD_MAX_AXES ||
        layout->button_count > NEXUS_HID_GAMEPAD_MAX_BUTTONS ||
        (layout->has_hat &&
         (layout->hat_bit_size == 0U || layout->hat_bit_size > 16U ||
          layout->hat_logical_min > layout->hat_logical_max))) return false;
    const size_t prefix = layout->has_report_id ? 1U : 0U;
    if (length != (size_t)layout->report_bytes + prefix ||
        (layout->has_report_id && report[0] != layout->report_id))
        return false;
    const uint8_t *payload = report + prefix;
    struct nexus_hid_gamepad_state decoded = {0};
    for (uint8_t i = 0U; i < layout->axis_count; ++i) {
        const struct nexus_hid_gamepad_axis *axis = &layout->axes[i];
        if (axis->logical_min > axis->logical_max) return false;
        uint32_t raw = 0U;
        if (!extract_bits(payload, layout->report_bytes, axis->bit_offset,
                          axis->bit_size, &raw)) return false;
        decoded.axes[i] = axis->logical_min < 0 ?
            sign_extend(raw, axis->bit_size) : (int32_t)raw;
        if (decoded.axes[i] < axis->logical_min ||
            decoded.axes[i] > axis->logical_max) return false;
    }
    for (uint8_t i = 0U; i < layout->button_count; ++i) {
        uint32_t pressed = 0U;
        if (!extract_bits(payload, layout->report_bytes,
                          layout->button_bit_offsets[i], 1U, &pressed))
            return false;
        if (pressed != 0U) decoded.buttons |= 1U << i;
    }
    if (layout->has_hat) {
        uint32_t raw = 0U;
        if (!extract_bits(payload, layout->report_bytes,
                          layout->hat_bit_offset, layout->hat_bit_size, &raw))
            return false;
        decoded.hat = (int32_t)raw;
        if (decoded.hat < layout->hat_logical_min ||
            decoded.hat > layout->hat_logical_max) return false;
    }
    *state = decoded;
    return true;
}
