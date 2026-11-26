#ifndef NEXUS_VIRTIO_GPU_H
#define NEXUS_VIRTIO_GPU_H

#include "graphics.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Start a VirtIO-GPU 2D scanout and return a CPU-writable backing surface.
 * The CPU rasterizer draws to that resource; present uploads modified
 * rectangles through TRANSFER_TO_HOST_2D and RESOURCE_FLUSH.
 *
 * The optional Virgl path exposes a bounded, synchronous 3D command-stream
 * submission interface only when the device advertises a compatible capset,
 * creates a Virgl context, and completes a fence-backed protocol smoke test.
 * This is not a Vulkan/OpenGL API or proof of hardware-rendered draw calls.
 */
bool virtio_gpu_start(uint64_t width, uint64_t height,
                      const struct nexus_graphics_surface *firmware_surface,
                      struct nexus_graphics_surface *render_surface);
bool virtio_gpu_present(void);
bool virtio_gpu_present_region(uint64_t x, uint64_t y,
                               uint64_t width, uint64_t height);
/*
 * Virgl/Gallium resource description. target/format/bind follow the
 * selected capset's Gallium protocol values; this API does not translate
 * OpenGL or Vulkan enums.
 */
struct nexus_virtio_gpu_3d_resource_desc {
    uint32_t target;
    uint32_t format;
    uint32_t bind;
    uint32_t width;
    uint32_t height;
    uint32_t depth;
    uint32_t array_size;
    uint32_t last_level;
    uint32_t nr_samples;
    uint32_t flags;
};

#define NEXUS_VIRTIO_GPU_MAX_SCANOUTS 16U
struct nexus_virtio_gpu_scanout_info {
    bool enabled;
    uint32_t x, y, width, height;
    uint32_t flags;
};
struct nexus_virtio_gpu_display_info {
    bool valid;
    uint32_t scanout_count;
    uint32_t selected_scanout;
    struct nexus_virtio_gpu_scanout_info
        scanouts[NEXUS_VIRTIO_GPU_MAX_SCANOUTS];
};

struct nexus_virtio_gpu_3d_info {
    bool virgl_feature_negotiated;
    bool context_created;
    bool command_submission_ready;
    uint32_t capset_id;
    uint32_t capset_max_version;
    uint32_t capset_max_size;
    bool gfxstream_capset_available;
    uint32_t gfxstream_capset_max_version;
    uint32_t gfxstream_capset_max_size;
    bool venus_capset_available;
    uint32_t venus_capset_max_version;
    uint32_t venus_capset_max_size;
    uint64_t last_submitted_fence;
    uint64_t last_completed_fence;
    uint32_t failed_submissions;
    uint32_t resources_allocated;
    uint32_t resources_created_total;
    uint32_t clear_commands_fenced;
    uint32_t last_clear_resource_id;
};

bool virtio_gpu_display_get_info(
    struct nexus_virtio_gpu_display_info *info);
bool virtio_gpu_3d_get_info(struct nexus_virtio_gpu_3d_info *info);
/* Allocate a host-side Virgl resource and attach it to the active context. */
bool virtio_gpu_3d_resource_create(
    const struct nexus_virtio_gpu_3d_resource_desc *description,
    uint32_t *resource_id);
/* Detach and unreference a resource created by the API above. */
bool virtio_gpu_3d_resource_destroy(uint32_t resource_id);
/*
 * Execute a Virgl color clear on a tracked 2D render-target resource.
 * rgba contains IEEE-754 binary32 channel bit patterns. A true return means
 * the device acknowledged the command stream's completion fence; it does not
 * independently verify pixel readback or the host's physical-GPU backend.
 */
bool virtio_gpu_3d_clear_rgba32(uint32_t resource_id,
                               const uint32_t rgba[4],
                               uint64_t *completed_fence);
/* Submit a 4-byte-aligned Virgl command stream. Returns only after its fence
 * has been acknowledged; this call is synchronous and serialized.
 */
bool virtio_gpu_3d_submit(const void *stream, size_t stream_length,
                          uint64_t *completed_fence);
bool virtio_gpu_active(void);
void virtio_gpu_stop(void);

#endif
