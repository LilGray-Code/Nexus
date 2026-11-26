#include "display_edid.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void finish_checksum(uint8_t *block) {
    uint8_t sum = 0U;
    block[127] = 0U;
    for (size_t i = 0U; i < 127U; ++i) sum = (uint8_t)(sum + block[i]);
    block[127] = (uint8_t)(0U - sum);
}

static void make_1080p_dtd(uint8_t *dtd, bool interlaced) {
    memset(dtd, 0, 18U);
    const uint16_t pixel_clock = 14850U; /* 148.50 MHz, in 10 kHz units */
    dtd[0] = (uint8_t)pixel_clock;
    dtd[1] = (uint8_t)(pixel_clock >> 8);
    dtd[2] = 0x80U; /* 1920 active, low byte */
    dtd[3] = 0x18U; /* 280 blanking, low byte */
    dtd[4] = 0x71U; /* active high nibble 7, blanking high nibble 1 */
    dtd[5] = 0x38U; /* 1080 active, low byte */
    dtd[6] = 0x2DU; /* 45 blanking */
    dtd[7] = 0x40U; /* active high nibble 4 */
    dtd[8] = 0x58U; /* horizontal sync offset */
    dtd[9] = 0x2CU; /* horizontal sync pulse */
    dtd[10] = 0x45U; /* vertical sync offset/pulse */
    dtd[17] = interlaced ? 0x9EU : 0x1EU;
}

static void make_edid(uint8_t *edid) {
    memset(edid, 0, NEXUS_EDID_BLOCK_SIZE);
    static const uint8_t signature[8] = {
        0x00U, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0x00U
    };
    memcpy(edid, signature, sizeof(signature));
    edid[8] = 0x4CU; edid[9] = 0x2DU; /* manufacturer code */
    edid[10] = 0x34U; edid[11] = 0x12U;
    edid[12] = 0x78U; edid[13] = 0x56U;
    edid[14] = 0x34U; edid[15] = 0x12U;
    edid[18] = 1U;
    edid[19] = 4U;
    edid[24] = 0x02U; /* first detailed timing is preferred */
    edid[126] = 0U;
    make_1080p_dtd(edid + 54U, false);
    finish_checksum(edid);
}

static void test_valid_preferred_timing(void) {
    uint8_t edid[NEXUS_EDID_BLOCK_SIZE];
    make_edid(edid);
    struct nexus_edid_info info;
    memset(&info, 0xA5, sizeof(info));
    assert(nexus_edid_parse(edid, sizeof(edid), &info));
    assert(info.version == 1U && info.revision == 4U);
    assert(info.manufacturer_id == 0x4C2DU);
    assert(info.product_code == 0x1234U);
    assert(info.serial_number == 0x12345678U);
    assert(info.extension_count == 0U);
    assert(info.mode_count == 1U);
    assert(info.preferred_mode_index == 0);
    assert(info.modes[0].width == 1920U);
    assert(info.modes[0].height == 1080U);
    assert(info.modes[0].horizontal_total == 2200U);
    assert(info.modes[0].vertical_total == 1125U);
    assert(info.modes[0].pixel_clock_khz == 148500U);
    assert(info.modes[0].refresh_millihz >= 59900U);
    assert(info.modes[0].refresh_millihz <= 60100U);
    assert(!info.modes[0].interlaced);
}

static void test_invalid_header_and_checksum(void) {
    uint8_t edid[NEXUS_EDID_BLOCK_SIZE];
    struct nexus_edid_info info;
    make_edid(edid);
    edid[0] = 0x01U;
    assert(!nexus_edid_parse(edid, sizeof(edid), &info));
    make_edid(edid);
    edid[30] ^= 0x80U;
    assert(!nexus_edid_parse(edid, sizeof(edid), &info));
    assert(!nexus_edid_parse(edid, 64U, &info));
}

static void test_extension_validation(void) {
    uint8_t edid[NEXUS_EDID_BLOCK_SIZE * 2U];
    struct nexus_edid_info info;
    make_edid(edid);
    edid[126] = 1U;
    finish_checksum(edid);
    assert(!nexus_edid_parse(edid, NEXUS_EDID_BLOCK_SIZE, &info));
    memset(edid + NEXUS_EDID_BLOCK_SIZE, 0, NEXUS_EDID_BLOCK_SIZE);
    finish_checksum(edid + NEXUS_EDID_BLOCK_SIZE);
    assert(nexus_edid_parse(edid, sizeof(edid), &info));
    assert(info.extension_count == 1U);
    edid[NEXUS_EDID_BLOCK_SIZE + 10U] = 1U;
    assert(!nexus_edid_parse(edid, sizeof(edid), &info));
}

static void test_interlaced_and_non_timing_descriptors(void) {
    uint8_t edid[NEXUS_EDID_BLOCK_SIZE];
    make_edid(edid);
    memset(edid + 54U, 0, 18U); /* monitor descriptor, not a DTD */
    make_1080p_dtd(edid + 72U, true);
    finish_checksum(edid);
    struct nexus_edid_info info;
    assert(nexus_edid_parse(edid, sizeof(edid), &info));
    assert(info.mode_count == 1U);
    assert(info.preferred_mode_index == -1);
    assert(info.modes[0].interlaced);
}

int main(void) {
    test_valid_preferred_timing();
    test_invalid_header_and_checksum();
    test_extension_validation();
    test_interlaced_and_non_timing_descriptors();
    puts("NEXUS EDID DISPLAY TIMING TESTS PASSED");
    return 0;
}
