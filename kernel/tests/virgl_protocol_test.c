#include "virgl_protocol.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static void test_clear_stream_layout(void) {
    uint32_t stream[NEXUS_VIRGL_CLEAR_SEQUENCE_DWORDS] = {0U};
    const uint32_t rgba[4] = {
        0x3F800000U, 0x3E800000U, 0x3D800000U, 0x3F800000U
    };
    size_t dwords = 0U;
    assert(nexus_virgl_encode_clear_rgba32(
        stream, NEXUS_VIRGL_CLEAR_SEQUENCE_DWORDS,
        7U, 7U, 0x12345678U, rgba, &dwords));
    assert(dwords == NEXUS_VIRGL_CLEAR_SEQUENCE_DWORDS);

    assert(stream[0] ==
        (NEXUS_VIRGL_CMD_CREATE_OBJECT |
         (NEXUS_VIRGL_SURFACE_OBJECT << 8) |
         (NEXUS_VIRGL_SURFACE_PAYLOAD_DWORDS << 16)));
    assert(stream[1] == 7U && stream[2] == 7U);
    assert(stream[3] == 0x12345678U);
    assert(stream[4] == 0U && stream[5] == 0U);

    assert(stream[6] ==
        (NEXUS_VIRGL_CMD_SET_FRAMEBUFFER_STATE |
         (NEXUS_VIRGL_FRAMEBUFFER_ONE_COLOR_DWORDS << 16)));
    assert(stream[7] == 1U && stream[8] == 0U && stream[9] == 7U);

    assert(stream[10] ==
        (NEXUS_VIRGL_CMD_CLEAR |
         (NEXUS_VIRGL_CLEAR_PAYLOAD_DWORDS << 16)));
    assert(stream[11] == NEXUS_VIRGL_PIPE_CLEAR_COLOR0);
    assert(stream[12] == rgba[0] && stream[13] == rgba[1]);
    assert(stream[14] == rgba[2] && stream[15] == rgba[3]);
    assert(stream[16] == 0U && stream[17] == 0x3FF00000U);
    assert(stream[18] == 0U);

    assert(stream[19] ==
        (NEXUS_VIRGL_CMD_SET_FRAMEBUFFER_STATE |
         (NEXUS_VIRGL_FRAMEBUFFER_EMPTY_DWORDS << 16)));
    assert(stream[20] == 0U && stream[21] == 0U);
    assert(stream[22] ==
        (NEXUS_VIRGL_CMD_DESTROY_OBJECT |
         (NEXUS_VIRGL_SURFACE_OBJECT << 8) |
         (NEXUS_VIRGL_DESTROY_PAYLOAD_DWORDS << 16)));
    assert(stream[23] == 7U);
}

static void test_bounds_and_invalid_arguments(void) {
    uint32_t stream[NEXUS_VIRGL_CLEAR_SEQUENCE_DWORDS] = {0U};
    const uint32_t rgba[4] = {0U, 0U, 0U, 0x3F800000U};
    size_t dwords = 99U;
    assert(!nexus_virgl_encode_clear_rgba32(
        stream, NEXUS_VIRGL_CLEAR_SEQUENCE_DWORDS - 1U,
        1U, 1U, 1U, rgba, &dwords));
    assert(dwords == 99U);
    assert(!nexus_virgl_encode_clear_rgba32(
        stream, NEXUS_VIRGL_CLEAR_SEQUENCE_DWORDS,
        0U, 1U, 1U, rgba, &dwords));
    assert(!nexus_virgl_encode_clear_rgba32(
        stream, NEXUS_VIRGL_CLEAR_SEQUENCE_DWORDS,
        1U, 0U, 1U, rgba, &dwords));
    assert(!nexus_virgl_encode_clear_rgba32(
        stream, NEXUS_VIRGL_CLEAR_SEQUENCE_DWORDS,
        1U, 1U, 1U, NULL, &dwords));
    assert(!nexus_virgl_encode_clear_rgba32(
        stream, NEXUS_VIRGL_CLEAR_SEQUENCE_DWORDS,
        1U, 1U, 1U, rgba, NULL));
}

int main(void) {
    test_clear_stream_layout();
    test_bounds_and_invalid_arguments();
    puts("NEXUS VIRGL CLEAR PROTOCOL TESTS PASSED");
    return 0;
}
