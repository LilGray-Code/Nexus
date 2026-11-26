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
struct nexus_virtio_gpu_3d_info {
    bool virgl_feature_negotiated;
    bool context_created;
    bool command_submission_ready;
    uint32_t capset_id;
    uint32_t capset_max_version;
    uint32_t capset_max_size;
    uint64_t last_submitted_fence;
    uint64_t last_completed_fence;
    uint32_t failed_submissions;
};

bool virtio_gpu_3d_get_info(struct nexus_virtio_gpu_3d_info *info);
/* Submit a 4-byte-aligned Virgl command stream. Returns only after its fence
 * has been acknowledged; this call is synchronous and serialized.
 */
bool virtio_gpu_3d_submit(const void *stream, size_t stream_length,
                          uint64_t *completed_fence);
bool virtio_gpu_active(void);
void virtio_gpu_stop(void);

#endif
