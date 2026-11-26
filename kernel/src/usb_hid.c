#include "usb_hid.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static struct nexus_usb_keyboard_state default_keyboard_state;

/*
 * Values use bit 8 for an E0/extended Set-1 key. The low byte keeps the
 * traditional Set-1 make/break representation used by the desktop.
 */
static uint16_t usage_to_set1(uint8_t usage) {
    static const uint16_t map[256] = {
        [4]=0x1E, [5]=0x30, [6]=0x2E, [7]=0x20, [8]=0x12,
        [9]=0x21, [10]=0x22, [11]=0x23, [12]=0x17, [13]=0x24,
        [14]=0x25, [15]=0x26, [16]=0x32, [17]=0x31, [18]=0x18,
        [19]=0x19, [20]=0x10, [21]=0x13, [22]=0x1F, [23]=0x14,
        [24]=0x16, [25]=0x2F, [26]=0x11, [27]=0x2D, [28]=0x15,
        [29]=0x2C,
        [30]=0x02, [31]=0x03, [32]=0x04, [33]=0x05, [34]=0x06,
        [35]=0x07, [36]=0x08, [37]=0x09, [38]=0x0A, [39]=0x0B,
        [40]=0x1C, [41]=0x01, [42]=0x0E, [43]=0x0F, [44]=0x39,
        [45]=0x0C, [46]=0x0D, [47]=0x1A, [48]=0x1B, [49]=0x2B,
        [51]=0x27, [52]=0x28, [53]=0x29, [54]=0x33, [55]=0x34,
        [56]=0x35, [57]=0x3A,
        [58]=0x3B, [59]=0x3C, [60]=0x3D, [61]=0x3E, [62]=0x3F,
        [63]=0x40, [64]=0x41, [65]=0x42, [66]=0x43, [67]=0x44,
        [68]=0x57, [69]=0x58,
        [73]=0x152, [74]=0x147, [75]=0x149, [76]=0x153,
        [77]=0x14F, [78]=0x151, [79]=0x14D, [80]=0x14B,
        [81]=0x150, [82]=0x148,
        [84]=0x135, [85]=0x37, [86]=0x4A, [87]=0x4E,
        [88]=0x1C, [89]=0x4F, [90]=0x50, [91]=0x51,
        [92]=0x4B, [93]=0x4C, [94]=0x4D, [95]=0x47,
        [96]=0x48, [97]=0x49, [98]=0x52, [99]=0x53,
        [100]=0x56, [101]=0x15D, [103]=0x59,
        [104]=0x64, [105]=0x65, [106]=0x66, [107]=0x67,
        [108]=0x68, [109]=0x69, [110]=0x6A, [111]=0x6B,
        [112]=0x6C, [113]=0x6D, [114]=0x6E, [115]=0x6F
    };
    return map[usage];
}

static bool report_has_usage(const uint8_t report[8], uint8_t usage) {
    for (size_t i = 2U; i < 8U; ++i) {
        if (report[i] == usage) return true;
    }
    return false;
}

void usb_hid_keyboard_state_reset(struct nexus_usb_keyboard_state *state) {
    if (state == NULL) return;
    for (size_t i = 0U; i < sizeof(state->previous_report); ++i)
        state->previous_report[i] = 0U;
    state->have_previous_report = false;
}

void usb_hid_keyboard_reset(void) {
    usb_hid_keyboard_state_reset(&default_keyboard_state);
}

size_t usb_hid_keyboard_decode_state(struct nexus_usb_keyboard_state *state,
                                     const uint8_t report[8],
                                     uint16_t *scancodes,
                                     size_t capacity) {
    if (state == NULL || report == NULL ||
        (capacity != 0U && scancodes == NULL)) return 0U;

    /* HID usage 1..3 indicates ErrorRollOver / POSTFail / ErrorUndefined. */
    for (size_t i = 2U; i < 8U; ++i) {
        if (report[i] >= 1U && report[i] <= 3U) return 0U;
    }

    size_t count = 0U;
    const uint8_t old_modifiers =
        state->have_previous_report ? state->previous_report[0] : 0U;
    const uint8_t changed_modifiers = old_modifiers ^ report[0];
    static const struct {
        uint8_t mask;
        uint16_t set1;
    } modifiers[] = {
        {0x01U, 0x01DU}, /* Left Ctrl */
        {0x02U, 0x02AU}, /* Left Shift */
        {0x04U, 0x038U}, /* Left Alt */
        {0x08U, 0x15BU}, /* Left GUI */
        {0x10U, 0x11DU}, /* Right Ctrl (E0) */
        {0x20U, 0x036U}, /* Right Shift */
        {0x40U, 0x138U}, /* Right Alt (E0) */
        {0x80U, 0x15CU}  /* Right GUI (E0) */
    };
    for (size_t i = 0U; i < sizeof(modifiers) /
                                sizeof(modifiers[0]); ++i) {
        const uint8_t mask = modifiers[i].mask;
        if ((changed_modifiers & mask) == 0U) continue;
        uint16_t code = modifiers[i].set1;
        if ((report[0] & mask) == 0U) code |= 0x80U;
        if (count < capacity) scancodes[count++] = code;
    }

    if (state->have_previous_report) {
        for (size_t i = 2U; i < 8U; ++i) {
            const uint8_t usage = state->previous_report[i];
            if (usage == 0U || report_has_usage(report, usage)) continue;
            const uint16_t code = usage_to_set1(usage);
            if (code == 0U || count >= capacity) continue;
            scancodes[count++] = (uint16_t)(code | 0x80U);
        }
    }

    for (size_t i = 2U; i < 8U; ++i) {
        const uint8_t usage = report[i];
        if (usage == 0U || (have_previous_report &&
                            report_has_usage(state->previous_report, usage))) {
            continue;
        }
        const uint16_t code = usage_to_set1(usage);
        if (code == 0U || count >= capacity) continue;
        scancodes[count++] = code;
    }

    for (size_t i = 0U; i < sizeof(state->previous_report); ++i)
        state->previous_report[i] = report[i];
    state->have_previous_report = true;
    return count;
}

bool usb_hid_mouse_decode_report(const uint8_t *report,
                                 size_t report_length,
                                 struct nexus_usb_mouse_event *event) {
    if (report == NULL || event == NULL || report_length < 3U) return false;
    event->buttons = (uint8_t)(report[0] & 0x07U);
    event->dx = (int16_t)(int8_t)report[1];
    event->dy = (int16_t)-(int16_t)(int8_t)report[2];
    event->wheel = report_length >= 4U ? (int8_t)report[3] : 0;
    return true;
}

bool usb_hid_mouse_decode(const uint8_t report[3],
                          struct nexus_usb_mouse_event *event) {
    return usb_hid_mouse_decode_report(report, 3U, event);
}



size_t usb_hid_keyboard_decode(const uint8_t report[8],
                               uint16_t *scancodes,
                               size_t capacity) {
    return usb_hid_keyboard_decode_state(&default_keyboard_state, report,
                                         scancodes, capacity);
}

/* ---------------- Bounded generic HID game-controller reports ---------------- */

#define HID_GAMEPAD_MAX_LOCAL_USAGES 32U
#define HID_GAMEPAD_GLOBAL_STACK 4U
#define HID_PAGE_GENERIC_DESKTOP 0x01U
#define HID_PAGE_BUTTON 0x09U

struct hid_global_state {
    uint16_t usage_page;
    int32_t logical_minimum;
    int32_t logical_maximum;
    uint32_t report_size;
    uint32_t report_count;
    uint8_t report_id;
};

struct hid_local_state {
    uint32_t usages[HID_GAMEPAD_MAX_LOCAL_USAGES];
    uint8_t usage_count;
    uint32_t usage_minimum;
    uint32_t usage_maximum;
    bool has_usage_range;
};

static uint32_t hid_item_unsigned(const uint8_t *bytes, uint8_t size) {
    uint32_t value = 0U;
    for (uint8_t i = 0U; i < size; ++i)
        value |= (uint32_t)bytes[i] << (8U * i);
    return value;
}

static int32_t hid_item_signed(uint32_t value, uint8_t size) {
    if (size == 0U) return 0;
    if (size == 4U) return (int32_t)value;
    const uint8_t bits = (uint8_t)(size * 8U);
    const uint32_t sign = 1U << (bits - 1U);
    if ((value & sign) != 0U)
        value |= ~((1U << bits) - 1U);
    return (int32_t)value;
}

static uint32_t hid_local_usage(uint32_t raw, uint16_t page, uint8_t size) {
    return size == 4U ? raw : ((uint32_t)page << 16) | (raw & 0xFFFFU);
}

static bool hid_gamepad_usage_for_field(
    const struct hid_local_state *local, uint32_t index,
    uint16_t page, uint32_t *usage) {
    if (local == NULL || usage == NULL) return false;
    if (index < local->usage_count) {
        *usage = local->usages[index];
        return true;
    }
    if (local->has_usage_range &&
        local->usage_minimum <= local->usage_maximum &&
        index <= local->usage_maximum - local->usage_minimum) {
        *usage = ((uint32_t)page << 16) |
                 (local->usage_minimum + index);
        return true;
    }
    return false;
}

static bool hid_gamepad_add_input(
    struct nexus_usb_gamepad_layout *layout,
    const struct hid_global_state *global,
    const struct hid_local_state *local,
    uint32_t flags, uint32_t *bit_offset, bool in_gamepad) {
    if (layout == NULL || global == NULL || local == NULL ||
        bit_offset == NULL || global->report_size == 0U ||
        global->report_size > 32U || global->report_count == 0U ||
        global->report_count > 64U ||
        global->report_size * global->report_count > 512U ||
        *bit_offset > 512U - global->report_size * global->report_count)
        return false;

    const bool variable = (flags & 0x02U) != 0U;
    if (in_gamepad && variable && global->logical_maximum >
        global->logical_minimum) {
        for (uint32_t i = 0U; i < global->report_count; ++i) {
            uint32_t usage = 0U;
            if (!hid_gamepad_usage_for_field(local, i,
                    global->usage_page, &usage)) continue;
            const uint16_t page = (uint16_t)(usage >> 16);
            const uint16_t id = (uint16_t)usage;
            const uint16_t field_offset =
                (uint16_t)(*bit_offset + i * global->report_size);

            if (page == HID_PAGE_GENERIC_DESKTOP &&
                ((id >= 0x30U && id <= 0x37U) || id == 0x39U)) {
                if (id == 0x39U) {
                    if (layout->hat_size != 0U) return false;
                    layout->hat_offset = field_offset;
                    layout->hat_size = (uint8_t)global->report_size;
                    layout->hat_minimum = global->logical_minimum;
                    layout->hat_maximum = global->logical_maximum;
                } else {
                    if (layout->axis_count >= NEXUS_USB_GAMEPAD_MAX_AXES)
                        return false;
                    struct nexus_usb_gamepad_axis *axis =
                        &layout->axes[layout->axis_count++];
                    axis->bit_offset = field_offset;
                    axis->bit_size = (uint8_t)global->report_size;
                    axis->usage = (uint8_t)id;
                    axis->logical_minimum = global->logical_minimum;
                    axis->logical_maximum = global->logical_maximum;
                }
            } else if (page == HID_PAGE_BUTTON && id >= 1U &&
                       id <= NEXUS_USB_GAMEPAD_MAX_BUTTONS) {
                if (layout->button_count >=
                    NEXUS_USB_GAMEPAD_MAX_BUTTONS) return false;
                const uint8_t button = (uint8_t)(id - 1U);
                for (uint8_t prior = 0U; prior < layout->button_count; ++prior) {
                    if ((uint8_t)(prior + 1U) == id) return false;
                }
                layout->button_offsets[button] = field_offset;
                layout->button_sizes[button] = (uint8_t)global->report_size;
                if (layout->button_count <= button)
                    layout->button_count = (uint8_t)(button + 1U);
            }
        }
    }
    *bit_offset += global->report_size * global->report_count;
    return true;
}

bool usb_hid_gamepad_parse_descriptor(
    const uint8_t *descriptor, size_t length,
    struct nexus_usb_gamepad_layout *layout) {
    if (descriptor == NULL || layout == NULL || length == 0U ||
        length > 512U) return false;

    struct nexus_usb_gamepad_layout parsed = {0};
    struct hid_global_state global = {0};
    struct hid_global_state global_stack[HID_GAMEPAD_GLOBAL_STACK];
    struct hid_local_state local = {0};
    uint8_t global_depth = 0U;
    uint8_t collection_depth = 0U;
    bool in_gamepad = false;
    bool saw_report_id = false;
    bool saw_report_without_id = false;
    uint8_t selected_report_id = 0U;
    uint32_t bit_offset = 0U;

    for (size_t offset = 0U; offset < length;) {
        const uint8_t prefix = descriptor[offset++];
        if (prefix == 0xFEU) return false; /* Long items are not needed here. */
        const uint8_t size_code = prefix & 0x03U;
        const uint8_t item_size = size_code == 3U ? 4U : size_code;
        const uint8_t item_type = (prefix >> 2) & 0x03U;
        const uint8_t item_tag = prefix >> 4;
        if (offset + item_size > length) return false;
        const uint32_t value = hid_item_unsigned(
            descriptor + offset, item_size);
        offset += item_size;

        if (item_type == 1U) { /* Global item. */
            switch (item_tag) {
                case 0U: /* Usage Page */
                    if (value > 0xFFFFU) return false;
                    global.usage_page = (uint16_t)value;
                    break;
                case 1U: /* Logical Minimum */
                    global.logical_minimum =
                        hid_item_signed(value, item_size);
                    break;
                case 2U: /* Logical Maximum */
                    global.logical_maximum =
                        global.logical_minimum < 0
                        ? hid_item_signed(value, item_size)
                        : (value > INT32_MAX ? INT32_MAX : (int32_t)value);
                    break;
                case 7U: /* Report Size */
                    if (value > 32U) return false;
                    global.report_size = value;
                    break;
                case 8U: /* Report ID */
                    if (value == 0U || value > 255U) return false;
                    global.report_id = (uint8_t)value;
                    break;
                case 9U: /* Report Count */
                    if (value > 64U) return false;
                    global.report_count = value;
                    break;
                case 10U: /* Push */
                    if (item_size != 0U ||
                        global_depth >= HID_GAMEPAD_GLOBAL_STACK)
                        return false;
                    global_stack[global_depth++] = global;
                    break;
                case 11U: /* Pop */
                    if (item_size != 0U || global_depth == 0U)
                        return false;
                    global = global_stack[--global_depth];
                    break;
                default:
                    break;
            }
            continue;
        }

        if (item_type == 2U) { /* Local item. */
            const uint32_t usage = hid_local_usage(
                value, global.usage_page, item_size);
            switch (item_tag) {
                case 0U: /* Usage */
                    if (local.usage_count >= HID_GAMEPAD_MAX_LOCAL_USAGES)
                        return false;
                    local.usages[local.usage_count++] = usage;
                    break;
                case 1U: /* Usage Minimum */
                    local.usage_minimum = value & 0xFFFFU;
                    local.has_usage_range = true;
                    break;
                case 2U: /* Usage Maximum */
                    local.usage_maximum = value & 0xFFFFU;
                    break;
                default:
                    break;
            }
            continue;
        }

        if (item_type != 0U) continue; /* Reserved type. */

        if (item_tag == 10U) { /* Collection */
            if (collection_depth >= 8U) return false;
            uint32_t usage = 0U;
            if (hid_gamepad_usage_for_field(&local, 0U,
                    global.usage_page, &usage) &&
                (value & 0xFFU) == 1U &&
                (usage >> 16) == HID_PAGE_GENERIC_DESKTOP &&
                ((uint16_t)usage == 0x04U ||
                 (uint16_t)usage == 0x05U)) {
                in_gamepad = true;
            }
            ++collection_depth;
        } else if (item_tag == 12U) { /* End Collection */
            if (item_size != 0U || collection_depth == 0U) return false;
            --collection_depth;
            if (collection_depth == 0U) in_gamepad = false;
        } else if (item_tag == 8U) { /* Input */
            if (global.report_id != 0U) {
                if (saw_report_without_id ||
                    (saw_report_id &&
                     selected_report_id != global.report_id))
                    return false;
                saw_report_id = true;
                selected_report_id = global.report_id;
                if (bit_offset == 0U) bit_offset = 8U;
            } else if (saw_report_id) {
                return false;
            } else {
                saw_report_without_id = true;
            }
            if (!hid_gamepad_add_input(&parsed, &global, &local,
                    value, &bit_offset, in_gamepad)) return false;
        }
        /* Local usage state is scoped to one Main item. */
        local = (struct hid_local_state){0};
    }

    if (collection_depth != 0U || parsed.axis_count == 0U ||
        (parsed.button_count == 0U && parsed.hat_size == 0U) ||
        bit_offset == 0U || bit_offset > 512U) return false;
    parsed.report_id = selected_report_id;
    parsed.report_bytes = (uint8_t)((bit_offset + 7U) / 8U);
    if (parsed.report_bytes == 0U ||
        parsed.report_bytes > NEXUS_USB_GAMEPAD_MAX_REPORT_BYTES)
        return false;
    *layout = parsed;
    return true;
}

static bool hid_gamepad_extract_bits(const uint8_t *report,
                                     size_t report_length,
                                     uint16_t bit_offset, uint8_t bit_size,
                                     uint32_t *value) {
    if (report == NULL || value == NULL || bit_size == 0U ||
        bit_size > 32U || bit_offset > report_length * 8U ||
        bit_size > report_length * 8U - bit_offset) return false;
    uint32_t result = 0U;
    for (uint8_t bit = 0U; bit < bit_size; ++bit) {
        const uint32_t source = (uint32_t)bit_offset + bit;
        if ((report[source / 8U] & (uint8_t)(1U << (source & 7U))) != 0U)
            result |= 1U << bit;
    }
    *value = result;
    return true;
}

static int32_t hid_gamepad_signed_value(uint32_t value, uint8_t bit_size) {
    if (bit_size == 32U) return (int32_t)value;
    const uint32_t sign = 1U << (bit_size - 1U);
    if ((value & sign) != 0U)
        value |= ~((1U << bit_size) - 1U);
    return (int32_t)value;
}

static int16_t hid_gamepad_normalize_axis(
    int32_t raw, int32_t minimum, int32_t maximum) {
    const int64_t low = minimum;
    const int64_t high = maximum;
    const int64_t center = low + (high - low) / 2;
    if (high <= low) return 0;
    int64_t normalized = 0;
    if ((int64_t)raw < center) {
        const int64_t span = center - low;
        if (span > 0)
            normalized = ((int64_t)raw - center) * 32767 / span;
    } else {
        const int64_t span = high - center;
        if (span > 0)
            normalized = ((int64_t)raw - center) * 32767 / span;
    }
    if (normalized < -32767) normalized = -32767;
    if (normalized > 32767) normalized = 32767;
    return (int16_t)normalized;
}

bool usb_hid_gamepad_decode(
    const struct nexus_usb_gamepad_layout *layout,
    const uint8_t *report, size_t report_length,
    uint16_t device_id, uint16_t vendor_id, uint16_t product_id,
    struct nexus_usb_gamepad_event *event) {
    if (layout == NULL || report == NULL || event == NULL ||
        layout->report_bytes == 0U ||
        layout->report_bytes > NEXUS_USB_GAMEPAD_MAX_REPORT_BYTES ||
        layout->axis_count > NEXUS_USB_GAMEPAD_MAX_AXES ||
        report_length < layout->report_bytes || report_length > 4096U)
        return false;
    if (layout->report_id != 0U &&
        (report_length == 0U || report[0] != layout->report_id))
        return false;

    const struct nexus_usb_gamepad_layout *p = layout;
    struct nexus_usb_gamepad_event decoded = {0};
    decoded.device_id = device_id;
    decoded.vendor_id = vendor_id;
    decoded.product_id = product_id;
    decoded.axis_count = p->axis_count;
    decoded.hat = 0xFFU;

    for (uint8_t i = 0U; i < p->axis_count; ++i) {
        const struct nexus_usb_gamepad_axis *axis = &p->axes[i];
        uint32_t raw_bits = 0U;
        if (!hid_gamepad_extract_bits(report, report_length,
                axis->bit_offset, axis->bit_size, &raw_bits)) return false;
        const int32_t raw = axis->logical_minimum < 0
            ? hid_gamepad_signed_value(raw_bits, axis->bit_size)
            : (raw_bits > INT32_MAX ? INT32_MAX : (int32_t)raw_bits);
        decoded.axes[i] = hid_gamepad_normalize_axis(
            raw, axis->logical_minimum, axis->logical_maximum);
    }

    for (uint8_t i = 0U; i < p->button_count; ++i) {
        if (p->button_sizes[i] == 0U) continue;
        uint32_t raw = 0U;
        if (!hid_gamepad_extract_bits(report, report_length,
                p->button_offsets[i], p->button_sizes[i], &raw))
            return false;
        if (raw != 0U) decoded.buttons |= 1U << i;
    }

    if (p->hat_size != 0U) {
        uint32_t raw_bits = 0U;
        if (!hid_gamepad_extract_bits(report, report_length,
                p->hat_offset, p->hat_size, &raw_bits)) return false;
        const int32_t raw = p->hat_minimum < 0
            ? hid_gamepad_signed_value(raw_bits, p->hat_size)
            : (int32_t)raw_bits;
        if (raw >= p->hat_minimum && raw <= p->hat_maximum &&
            p->hat_maximum >= p->hat_minimum) {
            const int64_t range =
                (int64_t)p->hat_maximum - (int64_t)p->hat_minimum + 1LL;
            const int64_t position =
                (int64_t)raw - (int64_t)p->hat_minimum;
            if (range > 0LL) {
                decoded.hat = (uint8_t)((position * 8LL) / range);
                if (decoded.hat > 7U) decoded.hat = 7U;
            }
        }
    }
    *event = decoded;
    return true;
}
