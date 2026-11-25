#include "usb_hid.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static uint8_t previous_report[8];
static bool have_previous_report;

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

void usb_hid_keyboard_reset(void) {
    for (size_t i = 0U; i < sizeof(previous_report); ++i)
        previous_report[i] = 0U;
    have_previous_report = false;
}

size_t usb_hid_keyboard_decode(const uint8_t report[8],
                               uint16_t *scancodes,
                               size_t capacity) {
    if (report == NULL || (capacity != 0U && scancodes == NULL)) return 0U;

    /* HID usage 1..3 indicates ErrorRollOver / POSTFail / ErrorUndefined. */
    for (size_t i = 2U; i < 8U; ++i) {
        if (report[i] >= 1U && report[i] <= 3U) return 0U;
    }

    size_t count = 0U;
    const uint8_t old_modifiers =
        have_previous_report ? previous_report[0] : 0U;
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

    if (have_previous_report) {
        for (size_t i = 2U; i < 8U; ++i) {
            const uint8_t usage = previous_report[i];
            if (usage == 0U || report_has_usage(report, usage)) continue;
            const uint16_t code = usage_to_set1(usage);
            if (code == 0U || count >= capacity) continue;
            scancodes[count++] = (uint16_t)(code | 0x80U);
        }
    }

    for (size_t i = 2U; i < 8U; ++i) {
        const uint8_t usage = report[i];
        if (usage == 0U || (have_previous_report &&
                            report_has_usage(previous_report, usage))) {
            continue;
        }
        const uint16_t code = usage_to_set1(usage);
        if (code == 0U || count >= capacity) continue;
        scancodes[count++] = code;
    }

    for (size_t i = 0U; i < sizeof(previous_report); ++i)
        previous_report[i] = report[i];
    have_previous_report = true;
    return count;
}

bool usb_hid_mouse_decode(const uint8_t report[3],
                          struct nexus_usb_mouse_event *event) {
    if (report == NULL || event == NULL) return false;
    event->buttons = (uint8_t)(report[0] & 0x07U);
    event->dx = (int16_t)(int8_t)report[1];
    event->dy = (int16_t)-(int16_t)(int8_t)report[2];
    event->wheel = 0;
    return true;
}
