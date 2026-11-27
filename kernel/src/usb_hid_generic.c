#include "usb_hid_generic.h"

#include <limits.h>
#include <string.h>

#define HID_USAGE_PAGE_GENERIC_DESKTOP 0x01U
#define HID_USAGE_PAGE_KEYBOARD        0x07U
#define HID_USAGE_PAGE_BUTTON          0x09U
#define HID_USAGE_KEYBOARD             0x06U
#define HID_USAGE_MOUSE                0x02U
#define HID_USAGE_X                    0x30U
#define HID_USAGE_Y                    0x31U
#define HID_USAGE_WHEEL                0x38U
#define HID_ITEM_LONG                 0xFEU
#define HID_MAX_REPORT_BITS           512U
#define HID_MAX_LOCAL_USAGES          32U
#define HID_GLOBAL_STACK_DEPTH        4U

struct hid_global_state {
    uint32_t usage_page;
    int32_t logical_minimum;
    int32_t logical_maximum;
    uint32_t report_size;
    uint32_t report_count;
    uint8_t report_id;
};

struct hid_local_state {
    uint32_t usages[HID_MAX_LOCAL_USAGES];
    uint8_t usage_count;
    uint32_t usage_minimum;
    uint32_t usage_maximum;
    bool has_usage_range;
};

static uint32_t hid_unsigned(const uint8_t *bytes, uint8_t size) {
    uint32_t value = 0U;
    for (uint8_t i = 0U; i < size; ++i)
        value |= (uint32_t)bytes[i] << (8U * i);
    return value;
}

static int32_t hid_signed(uint32_t value, uint8_t size) {
    if (size == 0U) return 0;
    if (size >= 4U) return (int32_t)value;
    const uint8_t bits = (uint8_t)(size * 8U);
    const uint32_t sign = 1U << (bits - 1U);
    if ((value & sign) != 0U)
        value |= ~((1U << bits) - 1U);
    return (int32_t)value;
}

static uint32_t hid_usage(uint32_t raw, uint8_t size, uint32_t page) {
    return size == 4U ? raw : ((page & 0xFFFFU) << 16) | (raw & 0xFFFFU);
}

static void hid_local_reset(struct hid_local_state *local) {
    *local = (struct hid_local_state){0};
}

static bool hid_local_get_usage(const struct hid_local_state *local,
                                uint32_t index, uint32_t page,
                                uint32_t *usage) {
    if (local == NULL || usage == NULL) return false;
    if (index < local->usage_count) {
        *usage = local->usages[index];
        return true;
    }
    if (local->has_usage_range &&
        local->usage_minimum <= local->usage_maximum &&
        index <= local->usage_maximum - local->usage_minimum) {
        *usage = ((page & 0xFFFFU) << 16) |
                 (local->usage_minimum + index);
        return true;
    }
    return false;
}

static void hid_add_usage(struct hid_local_state *local, uint32_t usage) {
    if (local->usage_count < HID_MAX_LOCAL_USAGES)
        local->usages[local->usage_count++] = usage;
}

static bool hid_layout_set_report_id(
    struct nexus_usb_hid_generic_layout *layout,
    bool *report_id_initialized, uint8_t report_id) {
    if (!*report_id_initialized) {
        layout->has_report_id = report_id != 0U;
        layout->report_id = report_id;
        *report_id_initialized = true;
        return true;
    }
    return layout->has_report_id == (report_id != 0U) &&
           (!layout->has_report_id || layout->report_id == report_id);
}

static bool hid_record_input(
    struct nexus_usb_hid_generic_layout *layout,
    const struct hid_global_state *global,
    const struct hid_local_state *local,
    uint32_t flags, uint32_t *input_bit_offset,
    bool in_target_application) {
    if (layout == NULL || global == NULL || local == NULL ||
        input_bit_offset == NULL || global->report_size == 0U ||
        global->report_size > 32U || global->report_count == 0U ||
        global->report_count > 64U) return false;

    const uint64_t field_bits =
        (uint64_t)global->report_size * global->report_count;
    if (field_bits > HID_MAX_REPORT_BITS ||
        (uint64_t)*input_bit_offset + field_bits > HID_MAX_REPORT_BITS)
        return false;

    if (in_target_application && (flags & 0x01U) == 0U) {
        const bool variable = (flags & 0x02U) != 0U;
        if (layout->kind == NEXUS_USB_HID_GENERIC_KEYBOARD &&
            global->usage_page == HID_USAGE_PAGE_KEYBOARD) {
            if (variable && global->report_size == 1U) {
                for (uint32_t i = 0U; i < global->report_count; ++i) {
                    uint32_t usage = 0U;
                    if (!hid_local_get_usage(local, i,
                            global->usage_page, &usage)) continue;
                    if ((usage >> 16) != HID_USAGE_PAGE_KEYBOARD) continue;
                    const uint32_t id = usage & 0xFFFFU;
                    if (id >= 0xE0U && id <= 0xE7U) {
                        const uint8_t modifier = (uint8_t)(id - 0xE0U);
                        layout->modifier_bit_offset[modifier] =
                            (uint16_t)(*input_bit_offset + i);
                        layout->modifier_mask |= (uint8_t)(1U << modifier);
                    }
                }
            } else if (!variable && global->report_size == 8U &&
                       layout->key_array_count == 0U &&
                       global->report_count >= 1U &&
                       global->report_count <= 8U) {
                uint32_t minimum = local->has_usage_range
                    ? local->usage_minimum : (HID_USAGE_PAGE_KEYBOARD << 16);
                uint32_t maximum = local->has_usage_range
                    ? local->usage_maximum
                    : ((HID_USAGE_PAGE_KEYBOARD << 16) | 0x65U);
                if ((minimum >> 16) != HID_USAGE_PAGE_KEYBOARD ||
                    (maximum >> 16) != HID_USAGE_PAGE_KEYBOARD)
                    return false;
                minimum &= 0xFFFFU;
                maximum &= 0xFFFFU;
                if (minimum > 0xFFU || maximum > 0xFFU ||
                    minimum > maximum) return false;
                layout->key_array_bit_offset = (uint16_t)*input_bit_offset;
                layout->key_array_size = 8U;
                layout->key_array_count = (uint8_t)global->report_count;
                layout->key_usage_minimum = (uint8_t)minimum;
                layout->key_usage_maximum = (uint8_t)maximum;
            }
        } else if (layout->kind == NEXUS_USB_HID_GENERIC_MOUSE) {
            if (global->usage_page == HID_USAGE_PAGE_BUTTON &&
                variable && global->report_size == 1U) {
                for (uint32_t i = 0U; i < global->report_count; ++i) {
                    uint32_t usage = 0U;
                    if (!hid_local_get_usage(local, i,
                            global->usage_page, &usage)) continue;
                    if ((usage >> 16) != HID_USAGE_PAGE_BUTTON) continue;
                    const uint32_t id = usage & 0xFFFFU;
                    if (id >= 1U && id <= 8U) {
                        const uint8_t button = (uint8_t)(id - 1U);
                        layout->button_bit_offset[button] =
                            (uint16_t)(*input_bit_offset + i);
                        if (layout->button_count <= button)
                            layout->button_count = (uint8_t)(button + 1U);
                    }
                }
            } else if (global->usage_page ==
                       HID_USAGE_PAGE_GENERIC_DESKTOP && variable &&
                       global->report_size >= 1U &&
                       global->report_size <= 16U) {
                for (uint32_t i = 0U; i < global->report_count; ++i) {
                    uint32_t usage = 0U;
                    if (!hid_local_get_usage(local, i,
                            global->usage_page, &usage)) continue;
                    if ((usage >> 16) != HID_USAGE_PAGE_GENERIC_DESKTOP)
                        continue;
                    const uint32_t id = usage & 0xFFFFU;
                    const uint16_t bit_offset = (uint16_t)(
                        *input_bit_offset + i * global->report_size);
                    if (id == HID_USAGE_X) {
                        layout->x_bit_offset = bit_offset;
                        layout->x_size = (uint8_t)global->report_size;
                        layout->x_logical_minimum = global->logical_minimum;
                        layout->x_relative = (flags & 0x04U) != 0U;
                    } else if (id == HID_USAGE_Y) {
                        layout->y_bit_offset = bit_offset;
                        layout->y_size = (uint8_t)global->report_size;
                        layout->y_logical_minimum = global->logical_minimum;
                        layout->y_relative = (flags & 0x04U) != 0U;
                    } else if (id == HID_USAGE_WHEEL) {
                        layout->wheel_bit_offset = bit_offset;
                        layout->wheel_size = (uint8_t)global->report_size;
                        layout->wheel_logical_minimum =
                            global->logical_minimum;
                        layout->wheel_relative = (flags & 0x04U) != 0U;
                    }
                }
            }
        }
    }

    *input_bit_offset += field_bits;
    return true;
}

bool usb_hid_generic_parse_descriptor(
    const uint8_t *descriptor, size_t descriptor_length,
    struct nexus_usb_hid_generic_layout *layout) {
    if (descriptor == NULL || layout == NULL ||
        descriptor_length == 0U || descriptor_length > 512U) return false;

    *layout = (struct nexus_usb_hid_generic_layout){0};
    struct hid_global_state global = {
        .usage_page = 0U,
        .logical_minimum = 0,
        .logical_maximum = 0,
        .report_size = 0U,
        .report_count = 0U,
        .report_id = 0U
    };
    struct hid_global_state global_stack[HID_GLOBAL_STACK_DEPTH];
    uint8_t global_depth = 0U;
    struct hid_local_state local = {0};
    bool in_target_application = false;
    uint8_t collection_depth = 0U;
    bool report_id_initialized = false;
    uint32_t input_bit_offset = 0U;
    size_t offset = 0U;

    while (offset < descriptor_length) {
        const uint8_t prefix = descriptor[offset++];
        if (prefix == HID_ITEM_LONG) {
            if (offset + 2U > descriptor_length) return false;
            const uint8_t size = descriptor[offset];
            offset += 2U;
            if (offset + size > descriptor_length) return false;
            offset += size;
            continue;
        }

        const uint8_t size_code = (uint8_t)(prefix & 0x03U);
        const uint8_t item_size = size_code == 3U ? 4U : size_code;
        const uint8_t item_type = (uint8_t)((prefix >> 2) & 0x03U);
        const uint8_t item_tag = (uint8_t)(prefix >> 4);
        if (offset + item_size > descriptor_length) return false;
        const uint32_t value = hid_unsigned(&descriptor[offset], item_size);
        offset += item_size;

        if (item_type == 1U) { /* Global item. */
            switch (item_tag) {
                case 0U: global.usage_page = value; break;
                case 1U:
                    global.logical_minimum = hid_signed(value, item_size);
                    break;
                case 2U:
                    global.logical_maximum = global.logical_minimum < 0
                        ? hid_signed(value, item_size) : (int32_t)value;
                    break;
                case 7U: global.report_size = value; break;
                case 8U:
                    if (value == 0U || value > 255U) return false;
                    global.report_id = (uint8_t)value;
                    break;
                case 9U: global.report_count = value; break;
                case 10U: /* Push */
                    if (global_depth >= HID_GLOBAL_STACK_DEPTH) return false;
                    global_stack[global_depth++] = global;
                    break;
                case 11U: /* Pop */
                    if (global_depth == 0U) return false;
                    global = global_stack[--global_depth];
                    break;
                default: break;
            }
        } else if (item_type == 2U) { /* Local item. */
            if (item_tag == 0U) {
                hid_add_usage(&local,
                    hid_usage(value, item_size, global.usage_page));
            } else if (item_tag == 1U) {
                local.usage_minimum =
                    hid_usage(value, item_size, global.usage_page);
                local.has_usage_range = true;
            } else if (item_tag == 2U) {
                local.usage_maximum =
                    hid_usage(value, item_size, global.usage_page);
                local.has_usage_range = true;
            }
        } else if (item_type == 0U) { /* Main item. */
            if (item_tag == 10U) { /* Collection */
                if (collection_depth == 0U && value == 1U &&
                    local.usage_count != 0U) {
                    const uint32_t usage = local.usages[0];
                    const uint32_t page = usage >> 16;
                    const uint32_t id = usage & 0xFFFFU;
                    if (page == HID_USAGE_PAGE_GENERIC_DESKTOP &&
                        (id == HID_USAGE_KEYBOARD || id == HID_USAGE_MOUSE)) {
                        layout->kind = id == HID_USAGE_KEYBOARD
                            ? NEXUS_USB_HID_GENERIC_KEYBOARD
                            : NEXUS_USB_HID_GENERIC_MOUSE;
                        in_target_application = true;
                    }
                }
                if (collection_depth == UINT8_MAX) return false;
                ++collection_depth;
                hid_local_reset(&local);
            } else if (item_tag == 12U) { /* End Collection */
                if (collection_depth == 0U) return false;
                --collection_depth;
                if (collection_depth == 0U) in_target_application = false;
                hid_local_reset(&local);
            } else if (item_tag == 8U) { /* Input */
                if (in_target_application) {
                    if (!hid_layout_set_report_id(layout,
                            &report_id_initialized, global.report_id))
                        return false;
                    if (!hid_record_input(layout, &global, &local, value,
                            &input_bit_offset, true)) return false;
                } else if (collection_depth != 0U) {
                    /* Unrelated collection data still advances the report. */
                    const uint64_t bits =
                        (uint64_t)global.report_size * global.report_count;
                    if (bits > HID_MAX_REPORT_BITS ||
                        (uint64_t)input_bit_offset + bits >
                            HID_MAX_REPORT_BITS) return false;
                    input_bit_offset += (uint32_t)bits;
                }
                hid_local_reset(&local);
            } else if (item_tag == 9U || item_tag == 11U) {
                hid_local_reset(&local);
            } else {
                hid_local_reset(&local);
            }
        }
    }

    if (layout->kind == NEXUS_USB_HID_GENERIC_KEYBOARD) {
        if (layout->key_array_count == 0U ||
            (layout->modifier_mask == 0U &&
             layout->key_array_count == 0U)) return false;
    } else if (layout->kind == NEXUS_USB_HID_GENERIC_MOUSE) {
        if (layout->x_size == 0U || layout->y_size == 0U ||
            layout->button_count == 0U) return false;
    } else {
        return false;
    }
    if (input_bit_offset == 0U ||
        input_bit_offset > HID_MAX_REPORT_BITS) return false;
    layout->input_bytes = (uint8_t)((input_bit_offset + 7U) / 8U);
    if (layout->input_bytes == 0U ||
        (uint16_t)layout->input_bytes +
            (layout->has_report_id ? 1U : 0U) > 64U) return false;
    return true;
}

static bool hid_report_payload(
    const struct nexus_usb_hid_generic_layout *layout,
    const uint8_t *report, size_t report_length,
    const uint8_t **payload) {
    if (layout == NULL || report == NULL || payload == NULL) return false;
    const size_t prefix = layout->has_report_id ? 1U : 0U;
    if (layout->has_report_id &&
        (report_length == 0U || report[0] != layout->report_id)) return false;
    if (report_length < prefix + layout->input_bytes) return false;
    *payload = report + prefix;
    return true;
}

static bool hid_read_bits(const uint8_t *bytes, size_t length,
                          uint16_t bit_offset, uint8_t bit_count,
                          uint32_t *value) {
    if (bytes == NULL || value == NULL || bit_count == 0U ||
        bit_count > 32U ||
        (uint32_t)bit_offset + bit_count > length * 8U) return false;
    uint32_t result = 0U;
    for (uint8_t i = 0U; i < bit_count; ++i) {
        const uint32_t bit = (uint32_t)bit_offset + i;
        if ((bytes[bit / 8U] & (uint8_t)(1U << (bit & 7U))) != 0U)
            result |= 1U << i;
    }
    *value = result;
    return true;
}

bool usb_hid_generic_decode_keyboard(
    const struct nexus_usb_hid_generic_layout *layout,
    const uint8_t *report, size_t report_length, uint8_t boot_report[8]) {
    if (layout == NULL || layout->kind != NEXUS_USB_HID_GENERIC_KEYBOARD ||
        boot_report == NULL) return false;
    const uint8_t *payload = NULL;
    if (!hid_report_payload(layout, report, report_length, &payload))
        return false;
    memset(boot_report, 0, 8U);

    for (uint8_t modifier = 0U; modifier < 8U; ++modifier) {
        if ((layout->modifier_mask & (uint8_t)(1U << modifier)) == 0U)
            continue;
        uint32_t down = 0U;
        if (!hid_read_bits(payload, layout->input_bytes,
                layout->modifier_bit_offset[modifier], 1U, &down))
            return false;
        if (down != 0U) boot_report[0] |= (uint8_t)(1U << modifier);
    }

    for (uint8_t i = 0U; i < layout->key_array_count && i < 6U; ++i) {
        uint32_t usage = 0U;
        const uint16_t bit =
            (uint16_t)(layout->key_array_bit_offset +
                       (uint16_t)i * layout->key_array_size);
        if (!hid_read_bits(payload, layout->input_bytes, bit,
                layout->key_array_size, &usage)) return false;
        if (usage == 0U) continue;
        if (usage < layout->key_usage_minimum ||
            usage > layout->key_usage_maximum) continue;
        boot_report[2U + i] = (uint8_t)usage;
    }
    return true;
}

static int32_t hid_signed_value(uint32_t value, uint8_t width,
                                int32_t logical_minimum) {
    if (logical_minimum >= 0) return (int32_t)value;
    if (width >= 32U) return (int32_t)value;
    const uint32_t sign = 1U << (width - 1U);
    if ((value & sign) != 0U)
        value |= ~((1U << width) - 1U);
    return (int32_t)value;
}

static int16_t hid_clamp_axis(int32_t value) {
    if (value < -127) return -127;
    if (value > 127) return 127;
    return (int16_t)value;
}

bool usb_hid_generic_decode_mouse(
    const struct nexus_usb_hid_generic_layout *layout,
    const uint8_t *report, size_t report_length,
    struct nexus_usb_mouse_event *event) {
    if (layout == NULL || layout->kind != NEXUS_USB_HID_GENERIC_MOUSE ||
        event == NULL) return false;
    const uint8_t *payload = NULL;
    if (!hid_report_payload(layout, report, report_length, &payload))
        return false;

    *event = (struct nexus_usb_mouse_event){0};
    for (uint8_t i = 0U; i < layout->button_count && i < 8U; ++i) {
        uint32_t down = 0U;
        if (!hid_read_bits(payload, layout->input_bytes,
                layout->button_bit_offset[i], 1U, &down)) return false;
        if (down != 0U) event->buttons |= (uint8_t)(1U << i);
    }

    uint32_t raw = 0U;
    if (!hid_read_bits(payload, layout->input_bytes,
            layout->x_bit_offset, layout->x_size, &raw)) return false;
    event->dx = hid_clamp_axis(hid_signed_value(raw, layout->x_size,
                                       layout->x_logical_minimum));

    if (!hid_read_bits(payload, layout->input_bytes,
            layout->y_bit_offset, layout->y_size, &raw)) return false;
    event->dy = (int16_t)-hid_clamp_axis(hid_signed_value(
        raw, layout->y_size, layout->y_logical_minimum));

    if (layout->wheel_size != 0U &&
        hid_read_bits(payload, layout->input_bytes,
            layout->wheel_bit_offset, layout->wheel_size, &raw)) {
        const int16_t wheel = hid_clamp_axis(hid_signed_value(
            raw, layout->wheel_size, layout->wheel_logical_minimum));
        event->wheel = (int8_t)wheel;
    }
    return true;
}
