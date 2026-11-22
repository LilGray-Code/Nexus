#include "usb_hid.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static uint8_t previous_report[8];
static bool have_previous_report;

static uint8_t usage_to_set1(uint8_t usage) {
    static const uint8_t map[104] = {
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
        [73]=0x52, [74]=0x47, [75]=0x49, [76]=0x53,
        [77]=0x4F, [78]=0x51, [79]=0x4D, [80]=0x4B,
        [81]=0x50, [82]=0x48
    };
    return usage < sizeof(map) ? map[usage] : 0U;
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
                               uint8_t *scancodes,
                               size_t capacity) {
    if (report == NULL || (capacity != 0U && scancodes == NULL)) return 0U;

    /* HID usage 1..3 indicates ErrorRollOver / POSTFail / ErrorUndefined. */
    for (size_t i = 2U; i < 8U; ++i) {
        if (report[i] >= 1U && report[i] <= 3U) return 0U;
    }

    size_t count = 0U;
    for (size_t i = 2U; i < 8U; ++i) {
        const uint8_t usage = report[i];
        if (usage == 0U || (have_previous_report &&
                            report_has_usage(previous_report, usage))) {
            continue;
        }
        const uint8_t code = usage_to_set1(usage);
        if (code == 0U || count >= capacity) continue;
        scancodes[count++] = code;
    }

    for (size_t i = 0U; i < sizeof(previous_report); ++i)
        previous_report[i] = report[i];
    have_previous_report = true;
    return count;
}
