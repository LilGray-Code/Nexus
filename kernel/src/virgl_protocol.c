#include "virgl_protocol.h"

static uint32_t command_header(uint8_t command, uint8_t object,
                               uint16_t payload_dwords) {
    return (uint32_t)command |
           ((uint32_t)object << 8) |
           ((uint32_t)payload_dwords << 16);
}

bool nexus_virgl_encode_clear_rgba32(
    uint32_t *stream, size_t stream_capacity_dwords,
    uint32_t surface_handle, uint32_t resource_id,
    uint32_t format, const uint32_t rgba[4],
    size_t *stream_dwords) {
    if (stream == NULL || rgba == NULL || stream_dwords == NULL ||
        stream_capacity_dwords < NEXUS_VIRGL_CLEAR_SEQUENCE_DWORDS ||
        surface_handle == 0U || resource_id == 0U) return false;

    size_t cursor = 0U;

    /* Create a surface object over the previously-created 3D resource. */
    stream[cursor++] = command_header(
        NEXUS_VIRGL_CMD_CREATE_OBJECT, NEXUS_VIRGL_SURFACE_OBJECT,
        NEXUS_VIRGL_SURFACE_PAYLOAD_DWORDS);
    stream[cursor++] = surface_handle;
    stream[cursor++] = resource_id;
    stream[cursor++] = format;
    stream[cursor++] = 0U; /* mip level */
    stream[cursor++] = 0U; /* first_layer | (last_layer << 16) */

    /* Bind one color surface and no depth/stencil surface. */
    stream[cursor++] = command_header(
        NEXUS_VIRGL_CMD_SET_FRAMEBUFFER_STATE, 0U,
        NEXUS_VIRGL_FRAMEBUFFER_ONE_COLOR_DWORDS);
    stream[cursor++] = 1U;
    stream[cursor++] = 0U;
    stream[cursor++] = surface_handle;

    /* Clear COLOR0 to the requested binary32 RGBA values. */
    stream[cursor++] = command_header(
        NEXUS_VIRGL_CMD_CLEAR, 0U,
        NEXUS_VIRGL_CLEAR_PAYLOAD_DWORDS);
    stream[cursor++] = NEXUS_VIRGL_PIPE_CLEAR_COLOR0;
    stream[cursor++] = rgba[0];
    stream[cursor++] = rgba[1];
    stream[cursor++] = rgba[2];
    stream[cursor++] = rgba[3];
    stream[cursor++] = 0U;          /* low dword of double 1.0 depth */
    stream[cursor++] = 0x3FF00000U; /* high dword of double 1.0 depth */
    stream[cursor++] = 0U;          /* stencil */

    /* Unbind before destroying the temporary surface object. */
    stream[cursor++] = command_header(
        NEXUS_VIRGL_CMD_SET_FRAMEBUFFER_STATE, 0U,
        NEXUS_VIRGL_FRAMEBUFFER_EMPTY_DWORDS);
    stream[cursor++] = 0U;
    stream[cursor++] = 0U;

    stream[cursor++] = command_header(
        NEXUS_VIRGL_CMD_DESTROY_OBJECT, NEXUS_VIRGL_SURFACE_OBJECT,
        NEXUS_VIRGL_DESTROY_PAYLOAD_DWORDS);
    stream[cursor++] = surface_handle;

    if (cursor != NEXUS_VIRGL_CLEAR_SEQUENCE_DWORDS) return false;
    *stream_dwords = cursor;
    return true;
}
