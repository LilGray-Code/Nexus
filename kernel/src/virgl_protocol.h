#ifndef NEXUS_VIRGL_PROTOCOL_H
#define NEXUS_VIRGL_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NEXUS_VIRGL_SURFACE_OBJECT 8U
#define NEXUS_VIRGL_CMD_CREATE_OBJECT 1U
#define NEXUS_VIRGL_CMD_SET_FRAMEBUFFER_STATE 5U
#define NEXUS_VIRGL_CMD_CLEAR 7U
#define NEXUS_VIRGL_CMD_DESTROY_OBJECT 3U
#define NEXUS_VIRGL_SURFACE_PAYLOAD_DWORDS 5U
#define NEXUS_VIRGL_FRAMEBUFFER_ONE_COLOR_DWORDS 3U
#define NEXUS_VIRGL_CLEAR_PAYLOAD_DWORDS 8U
#define NEXUS_VIRGL_FRAMEBUFFER_EMPTY_DWORDS 2U
#define NEXUS_VIRGL_DESTROY_PAYLOAD_DWORDS 1U
#define NEXUS_VIRGL_CLEAR_SEQUENCE_DWORDS 24U

/* Gallium constants encoded by the Virgl wire protocol. */
#define NEXUS_VIRGL_PIPE_TEXTURE_2D 2U
#define NEXUS_VIRGL_PIPE_BIND_RENDER_TARGET (1U << 1)
#define NEXUS_VIRGL_PIPE_CLEAR_COLOR0 (1U << 2)

/*
 * Encode a bounded color-clear stream for one 2D render-target surface.
 * rgba contains the four IEEE-754 binary32 bit patterns in R,G,B,A order.
 * The returned stream is made of little-endian Virgl command dwords.
 */
bool nexus_virgl_encode_clear_rgba32(
    uint32_t *stream, size_t stream_capacity_dwords,
    uint32_t surface_handle, uint32_t resource_id,
    uint32_t format, const uint32_t rgba[4],
    size_t *stream_dwords);

#endif
