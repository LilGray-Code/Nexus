#include "virtio_gpu.h"

#include "gpu.h"
#include "memory.h"
#include "pci.h"
#include "serial.h"
#include "timer.h"
#include "virtio_pci.h"

#include <stddef.h>
#include <stdint.h>

#define VIRTIO_GPU_QUEUE_CONTROL 0U
#define VIRTIO_GPU_QUEUE_SIZE 8U
#define VIRTIO_GPU_RESOURCE_ID 1U
#define VIRTIO_GPU_SCANOUT_ID 0U
#define VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM 2U
#define VIRTIO_GPU_CMD_RESOURCE_CREATE_2D 0x0101U
#define VIRTIO_GPU_CMD_SET_SCANOUT 0x0103U
#define VIRTIO_GPU_CMD_RESOURCE_FLUSH 0x0104U
#define VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D 0x0105U
#define VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING 0x0106U
#define VIRTIO_GPU_CMD_GET_CAPSET_INFO 0x0108U
#define VIRTIO_GPU_CMD_CTX_CREATE 0x0200U
#define VIRTIO_GPU_CMD_CTX_DESTROY 0x0201U
#define VIRTIO_GPU_CMD_SUBMIT_3D 0x0205U
#define VIRTIO_GPU_RESP_OK_NODATA 0x1100U
#define VIRTIO_GPU_RESP_ERR_UNSPEC 0x1200U
#define VIRTIO_GPU_RESP_OK_CAPSET_INFO 0x1102U
#define VIRTIO_GPU_F_VIRGL (1U << 0)
#define VIRTIO_GPU_F_CONTEXT_INIT (1U << 4)
#define VIRTIO_GPU_FLAG_FENCE (1U << 0)
#define VIRTIO_GPU_CAPSET_VIRGL 1U
#define VIRTIO_GPU_CAPSET_VIRGL2 2U
#define VIRTIO_GPU_CONTEXT_ID 1U
#define VIRTIO_GPU_COMMAND_TIMEOUT 200U
#define VIRTIO_GPU_COMMAND_BYTES 65536U
#define VIRTIO_GPU_RESPONSE_OFFSET VIRTIO_GPU_COMMAND_BYTES
#define VIRTIO_GPU_RESPONSE_CAPACITY 64U
#define VIRTIO_GPU_COMMAND_POOL_PAGES \
    ((VIRTIO_GPU_RESPONSE_OFFSET + 4096U + NEXUS_PAGE_SIZE - 1U) / \
     NEXUS_PAGE_SIZE)
#define VIRTIO_GPU_MAX_CAPSET_INDICES 8U

struct virtio_gpu_header {
    uint32_t type;
    uint32_t flags;
    uint64_t fence_id;
    uint32_t context_id;
    uint32_t padding;
} __attribute__((packed));

_Static_assert(sizeof(struct virtio_gpu_header) == 24U,
               "VirtIO GPU control header ABI");

struct virtio_gpu_get_capset_info {
    struct virtio_gpu_header header;
    uint32_t capset_index;
    uint32_t padding;
} __attribute__((packed));

struct virtio_gpu_resp_capset_info {
    struct virtio_gpu_header header;
    uint32_t capset_id;
    uint32_t max_version;
    uint32_t max_size;
    uint32_t padding;
} __attribute__((packed));

struct virtio_gpu_ctx_create {
    struct virtio_gpu_header header;
    uint32_t name_length;
    uint32_t context_init;
    char debug_name[64];
} __attribute__((packed));

struct virtio_gpu_ctx_destroy {
    struct virtio_gpu_header header;
} __attribute__((packed));

struct virtio_gpu_submit_3d {
    struct virtio_gpu_header header;
    uint32_t size;
    uint32_t padding;
} __attribute__((packed));

struct virtio_gpu_rectangle {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
} __attribute__((packed));

struct virtio_gpu_create_2d {
    struct virtio_gpu_header header;
    uint32_t resource_id;
    uint32_t format;
    uint32_t width;
    uint32_t height;
} __attribute__((packed));

struct virtio_gpu_memory_entry {
    uint64_t address;
    uint32_t length;
    uint32_t padding;
} __attribute__((packed));

struct virtio_gpu_attach_backing {
    struct virtio_gpu_header header;
    uint32_t resource_id;
    uint32_t entry_count;
    struct virtio_gpu_memory_entry entry;
} __attribute__((packed));

struct virtio_gpu_set_scanout {
    struct virtio_gpu_header header;
    struct virtio_gpu_rectangle rectangle;
    uint32_t scanout_id;
    uint32_t resource_id;
} __attribute__((packed));

struct virtio_gpu_transfer {
    struct virtio_gpu_header header;
    struct virtio_gpu_rectangle rectangle;
    uint64_t offset;
    uint32_t resource_id;
    uint32_t padding;
} __attribute__((packed));

struct virtio_gpu_flush {
    struct virtio_gpu_header header;
    struct virtio_gpu_rectangle rectangle;
    uint32_t resource_id;
    uint32_t padding;
} __attribute__((packed));

static struct nexus_virtio_pci transport;
static struct nexus_virtio_queue control_queue;
static bool active;
static uint64_t width_pixels;
static uint64_t height_pixels;
static uint64_t pitch_bytes;
static uint64_t framebuffer_physical;
static uint64_t framebuffer_pages;
static volatile uint8_t *framebuffer_memory;
static uint64_t command_physical;
static uint64_t command_pages;
static volatile uint8_t *command_memory;
static bool virgl_feature_negotiated;
static bool virgl_context_created;
static bool virgl_command_submission_ready;
static uint32_t virgl_capset_id;
static uint32_t virgl_capset_max_version;
static uint32_t virgl_capset_max_size;
static uint64_t virgl_next_fence;
static uint64_t virgl_last_submitted_fence;
static uint64_t virgl_last_completed_fence;
static uint32_t virgl_failed_submissions;
static volatile uint32_t command_queue_lock;

static void zero_bytes(volatile uint8_t *output, size_t length) {
    for (size_t i = 0U; i < length; ++i) output[i] = 0U;
}

static void copy_bytes(volatile uint8_t *output, const uint8_t *input,
                       size_t length) {
    for (size_t i = 0U; i < length; ++i) output[i] = input[i];
}

static uint32_t read_le32(const volatile uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t read_le64(const volatile uint8_t *p) {
    return (uint64_t)read_le32(p) |
           ((uint64_t)read_le32(p + 4U) << 32);
}

static void gpu_command_queue_lock(void) {
    while (__atomic_exchange_n(&command_queue_lock, 1U,
                               __ATOMIC_ACQUIRE) != 0U)
        __asm__ volatile ("pause" ::: "memory");
}

static void gpu_command_queue_unlock(void) {
    __atomic_store_n(&command_queue_lock, 0U, __ATOMIC_RELEASE);
}

static struct virtio_gpu_header make_header(uint32_t type) {
    return (struct virtio_gpu_header){
        .type = type,
        .flags = 0U,
        .fence_id = 0U,
        .context_id = 0U,
        .padding = 0U
    };
}

static bool submit_command_raw(const void *command,
                               size_t command_length,
                               uint32_t *response_type,
                               uint32_t *response_length) {
    if (!active || command == NULL || command_length == 0U ||
        command_length > VIRTIO_GPU_COMMAND_BYTES ||
        command_memory == NULL || response_type == NULL ||
        response_length == NULL) return false;

    gpu_command_queue_lock();
    zero_bytes(command_memory, command_length);
    copy_bytes(command_memory, (const uint8_t *)command, command_length);
    zero_bytes(command_memory + VIRTIO_GPU_RESPONSE_OFFSET,
               VIRTIO_GPU_RESPONSE_CAPACITY);

    uint32_t actual_response_length = 0U;
    if (!virtio_pci_queue_submit_pair(
            &control_queue, command_physical, (uint32_t)command_length,
            command_physical + VIRTIO_GPU_RESPONSE_OFFSET,
            VIRTIO_GPU_RESPONSE_CAPACITY, VIRTIO_GPU_COMMAND_TIMEOUT,
            &actual_response_length)) {
        serial_write("VIRTIO GPU: control queue command timed out or failed type=");
        serial_write_hex(read_le32(command_memory));
        serial_write(" avail=");
        serial_write_hex(control_queue.available_index);
        serial_write(" used=");
        serial_write_hex(control_queue.last_used_index);
        serial_write("\r\n");
        gpu_command_queue_unlock();
        return false;
    }
    if (actual_response_length < sizeof(struct virtio_gpu_header)) {
        gpu_command_queue_unlock();
        return false;
    }
    *response_type =
        read_le32(command_memory + VIRTIO_GPU_RESPONSE_OFFSET);
    *response_length = actual_response_length;
    gpu_command_queue_unlock();
    return true;
}

static bool submit_command(const void *command, size_t command_length) {
    uint32_t response_type = 0U, response_length = 0U;
    if (!submit_command_raw(command, command_length,
                            &response_type, &response_length)) return false;
    if (response_type != VIRTIO_GPU_RESP_OK_NODATA) {
        serial_write("VIRTIO GPU: device rejected command type=");
        serial_write_hex(response_type);
        serial_write("\r\n");
        return false;
    }
    return response_length >= sizeof(struct virtio_gpu_header);
}

static const struct nexus_pci_device *find_virtio_gpu(void) {
    for (uint32_t i = 0U; i < pci_stored_device_count(); ++i) {
        const struct nexus_pci_device *pci = pci_device_at(i);
        if (pci != NULL && pci->vendor_id == NEXUS_VIRTIO_PCI_VENDOR &&
            pci->device_id == NEXUS_VIRTIO_PCI_DEVICE_GPU) return pci;
    }
    return NULL;
}

static bool gpu_virgl_query_capset(void) {
    if (!virgl_feature_negotiated) return false;
    bool found = false;
    for (uint32_t index = 0U;
         index < VIRTIO_GPU_MAX_CAPSET_INDICES; ++index) {
        const struct virtio_gpu_get_capset_info request = {
            .header = make_header(VIRTIO_GPU_CMD_GET_CAPSET_INFO),
            .capset_index = index,
            .padding = 0U
        };
        uint32_t response_type = 0U, response_length = 0U;
        if (!submit_command_raw(&request, sizeof(request),
                                &response_type, &response_length))
            return false;
        if (response_type != VIRTIO_GPU_RESP_OK_CAPSET_INFO)
            break;
        if (response_length < sizeof(struct virtio_gpu_resp_capset_info))
            return false;
        const volatile uint8_t *response =
            command_memory + VIRTIO_GPU_RESPONSE_OFFSET;
        const uint32_t id = read_le32(response + 24U);
        const uint32_t version = read_le32(response + 28U);
        const uint32_t size = read_le32(response + 32U);
        if ((id != VIRTIO_GPU_CAPSET_VIRGL &&
             id != VIRTIO_GPU_CAPSET_VIRGL2) || version == 0U || size == 0U)
            continue;
        if (!found || id == VIRTIO_GPU_CAPSET_VIRGL2) {
            virgl_capset_id = id;
            virgl_capset_max_version = version;
            virgl_capset_max_size = size;
            found = true;
        }
    }
    return found;
}

static bool gpu_virgl_create_context(void) {
    if (!gpu_virgl_query_capset()) return false;
    static const char name[] = "Nexus Virgl Context";
    struct virtio_gpu_ctx_create create = {
        .header = make_header(VIRTIO_GPU_CMD_CTX_CREATE),
        .name_length = (uint32_t)(sizeof(name) - 1U),
        .context_init =
            (transport.features & ((uint64_t)1U <<
                                   (32U + VIRTIO_GPU_F_CONTEXT_INIT))) != 0U
            ? virgl_capset_id : 0U
    };
    create.header.context_id = VIRTIO_GPU_CONTEXT_ID;
    for (size_t i = 0U; i < sizeof(name); ++i)
        create.debug_name[i] = name[i];
    if (!submit_command(&create, sizeof(create))) return false;
    virgl_context_created = true;
    return true;
}

static void gpu_virgl_destroy_context(void) {
    if (!active || !virgl_context_created) return;
    const struct virtio_gpu_ctx_destroy destroy = {
        .header = {
            .type = VIRTIO_GPU_CMD_CTX_DESTROY,
            .flags = 0U,
            .fence_id = 0U,
            .context_id = VIRTIO_GPU_CONTEXT_ID,
            .padding = 0U
        }
    };
    (void)submit_command(&destroy, sizeof(destroy));
    virgl_context_created = false;
    virgl_command_submission_ready = false;
    gpu_set_virtio_3d_submission_ready(false, 0U);
}

static bool gpu_virgl_start(void) {
    virgl_feature_negotiated =
        (transport.features & ((uint64_t)1U << VIRTIO_GPU_F_VIRGL)) != 0U;
    virgl_context_created = false;
    virgl_command_submission_ready = false;
    virgl_capset_id = 0U;
    virgl_capset_max_version = 0U;
    virgl_capset_max_size = 0U;
    virgl_next_fence = 1U;
    virgl_last_submitted_fence = 0U;
    virgl_last_completed_fence = 0U;
    virgl_failed_submissions = 0U;
    gpu_set_virtio_3d_submission_ready(false, 0U);

    if (!virgl_feature_negotiated) {
        serial_write("VIRTIO GPU: host does not advertise Virgl 3D support\r\n");
        return false;
    }
    if (!gpu_virgl_create_context()) {
        serial_write("VIRTIO GPU: Virgl feature present but no usable 3D capset/context\r\n");
        return false;
    }

    /* A zero-length NOP verifies the context-specific command path and fence
     * completion without claiming that a rendering workload was executed.
     */
    const uint32_t virgl_nop = 0U;
    if (!virtio_gpu_3d_submit(&virgl_nop, sizeof(virgl_nop), NULL)) {
        gpu_virgl_destroy_context();
        serial_write("VIRTIO GPU: Virgl command/fence smoke test failed\r\n");
        return false;
    }
    virgl_command_submission_ready = true;
    gpu_set_virtio_3d_submission_ready(true, virgl_capset_id);
    serial_write("VIRTIO GPU: Virgl context and fence-backed command queue ready; draw acceleration not yet enabled\r\n");
    return true;
}

static bool gpu_create_resource(void) {
    const struct virtio_gpu_create_2d create = {
        .header = make_header(VIRTIO_GPU_CMD_RESOURCE_CREATE_2D),
        .resource_id = VIRTIO_GPU_RESOURCE_ID,
        .format = VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM,
        .width = (uint32_t)width_pixels,
        .height = (uint32_t)height_pixels
    };
    if (!submit_command(&create, sizeof(create))) return false;

    const struct virtio_gpu_attach_backing backing = {
        .header = make_header(VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING),
        .resource_id = VIRTIO_GPU_RESOURCE_ID,
        .entry_count = 1U,
        .entry = {
            .address = framebuffer_physical,
            .length = (uint32_t)(pitch_bytes * height_pixels),
            .padding = 0U
        }
    };
    if (!submit_command(&backing, sizeof(backing))) return false;

    const struct virtio_gpu_set_scanout scanout = {
        .header = make_header(VIRTIO_GPU_CMD_SET_SCANOUT),
        .rectangle = {0U, 0U, (uint32_t)width_pixels,
                      (uint32_t)height_pixels},
        .scanout_id = VIRTIO_GPU_SCANOUT_ID,
        .resource_id = VIRTIO_GPU_RESOURCE_ID
    };
    return submit_command(&scanout, sizeof(scanout));
}

bool virtio_gpu_start(uint64_t width, uint64_t height,
                      const struct nexus_graphics_surface *firmware_surface,
                      struct nexus_graphics_surface *render_surface) {
    active = false;
    if (firmware_surface == NULL || render_surface == NULL ||
        width == 0U || height == 0U ||
        width > 4096U || height > 2160U ||
        width > UINT64_MAX / 4U || height > UINT64_MAX / (width * 4U) ||
        firmware_surface->bits_per_pixel != 32U ||
        firmware_surface->red_mask_size != 8U ||
        firmware_surface->red_mask_shift != 16U ||
        firmware_surface->green_mask_size != 8U ||
        firmware_surface->green_mask_shift != 8U ||
        firmware_surface->blue_mask_size != 8U ||
        firmware_surface->blue_mask_shift != 0U) return false;

    const struct nexus_pci_device *pci = find_virtio_gpu();
    if (pci == NULL) {
        serial_write("VIRTIO GPU: modern PCI display device not present; using firmware framebuffer\r\n");
        return false;
    }

    const uint32_t optional_gpu_features =
        (1U << VIRTIO_GPU_F_VIRGL) | (1U << VIRTIO_GPU_F_CONTEXT_INIT);
    if (!virtio_pci_open(pci, 0U, optional_gpu_features, &transport)) {
        serial_write("VIRTIO GPU: modern PCI capability negotiation unavailable\r\n");
        return false;
    }
    if (!virtio_pci_queue_init(&transport, &control_queue,
                               VIRTIO_GPU_QUEUE_CONTROL,
                               VIRTIO_GPU_QUEUE_SIZE)) {
        virtio_pci_fail(&transport);
        serial_write("VIRTIO GPU: unable to initialize control queue\r\n");
        return false;
    }
    if (!virtio_pci_finish(&transport)) return false;

    const uint64_t total_bytes = width * height * 4U;
    framebuffer_pages = (total_bytes + NEXUS_PAGE_SIZE - 1U) /
                        NEXUS_PAGE_SIZE;
    if (total_bytes > UINT32_MAX || framebuffer_pages == 0U ||
        !memory_alloc_contiguous_pages(framebuffer_pages, 0x100000000ULL,
                                       &framebuffer_physical)) {
        virtio_pci_fail(&transport);
        serial_write("VIRTIO GPU: contiguous DMA framebuffer allocation failed\r\n");
        return false;
    }
    framebuffer_memory =
        (volatile uint8_t *)memory_phys_to_virt(framebuffer_physical);
    if (framebuffer_memory == NULL) {
        (void)memory_free_contiguous_pages(framebuffer_physical,
                                           framebuffer_pages);
        framebuffer_pages = 0U;
        virtio_pci_fail(&transport);
        return false;
    }
    for (uint64_t i = 0U; i < framebuffer_pages * NEXUS_PAGE_SIZE; ++i)
        framebuffer_memory[i] = 0U;

    command_pages = VIRTIO_GPU_COMMAND_POOL_PAGES;
    if (!memory_alloc_contiguous_pages(command_pages, 0x100000000ULL,
                                       &command_physical)) {
        (void)memory_free_contiguous_pages(framebuffer_physical,
                                           framebuffer_pages);
        framebuffer_pages = 0U;
        virtio_pci_fail(&transport);
        return false;
    }
    command_memory =
        (volatile uint8_t *)memory_phys_to_virt(command_physical);
    if (command_memory == NULL) {
        (void)memory_free_contiguous_pages(command_physical, command_pages);
        command_physical = 0U;
        command_pages = 0U;
        (void)memory_free_contiguous_pages(framebuffer_physical,
                                           framebuffer_pages);
        framebuffer_pages = 0U;
        virtio_pci_fail(&transport);
        return false;
    }
    zero_bytes(command_memory,
               VIRTIO_GPU_RESPONSE_OFFSET + VIRTIO_GPU_RESPONSE_CAPACITY);

    width_pixels = width;
    height_pixels = height;
    pitch_bytes = width * 4U;
    active = true;
    if (!gpu_create_resource()) {
        active = false;
        virtio_pci_fail(&transport);
        (void)memory_free_contiguous_pages(command_physical, command_pages);
        command_physical = 0U;
        command_pages = 0U;
        (void)memory_free_contiguous_pages(framebuffer_physical,
                                           framebuffer_pages);
        framebuffer_pages = 0U;
        framebuffer_memory = NULL;
        return false;
    }

    *render_surface = *firmware_surface;
    render_surface->address = framebuffer_memory;
    render_surface->pitch = pitch_bytes;
    serial_write("VIRTIO GPU: 2D resource attached; CPU raster buffer now drives GPU scanout\r\n");
    (void)gpu_virgl_start();
    return true;
}

bool virtio_gpu_present_region(uint64_t x, uint64_t y,
                               uint64_t width, uint64_t height) {
    if (!active || width == 0U || height == 0U ||
        x >= width_pixels || y >= height_pixels) return false;
    if (width > width_pixels - x) width = width_pixels - x;
    if (height > height_pixels - y) height = height_pixels - y;
    if (width == 0U || height == 0U ||
        y > UINT64_MAX / pitch_bytes ||
        x > (UINT64_MAX - y * pitch_bytes) / 4U) return false;

    const uint64_t offset = y * pitch_bytes + x * 4U;
    const struct virtio_gpu_transfer transfer = {
        .header = make_header(VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D),
        .rectangle = {(uint32_t)x, (uint32_t)y,
                      (uint32_t)width, (uint32_t)height},
        .offset = offset,
        .resource_id = VIRTIO_GPU_RESOURCE_ID,
        .padding = 0U
    };
    if (!submit_command(&transfer, sizeof(transfer))) return false;

    const struct virtio_gpu_flush flush = {
        .header = make_header(VIRTIO_GPU_CMD_RESOURCE_FLUSH),
        .rectangle = {(uint32_t)x, (uint32_t)y,
                      (uint32_t)width, (uint32_t)height},
        .resource_id = VIRTIO_GPU_RESOURCE_ID,
        .padding = 0U
    };
    return submit_command(&flush, sizeof(flush));
}

bool virtio_gpu_present(void) {
    return virtio_gpu_present_region(0U, 0U, width_pixels, height_pixels);
}


bool virtio_gpu_3d_submit(const void *stream, size_t stream_length,
                          uint64_t *completed_fence) {
    if (!active || !virgl_context_created || stream == NULL ||
        stream_length == 0U || (stream_length & 3U) != 0U ||
        stream_length > VIRTIO_GPU_COMMAND_BYTES -
                        sizeof(struct virtio_gpu_submit_3d) ||
        command_memory == NULL) return false;

    gpu_command_queue_lock();
    if (!active || !virgl_context_created || virgl_next_fence == UINT64_MAX) {
        gpu_command_queue_unlock();
        return false;
    }
    const uint64_t fence = virgl_next_fence++;
    const struct virtio_gpu_submit_3d submit = {
        .header = {
            .type = VIRTIO_GPU_CMD_SUBMIT_3D,
            .flags = VIRTIO_GPU_FLAG_FENCE,
            .fence_id = fence,
            .context_id = VIRTIO_GPU_CONTEXT_ID,
            .padding = 0U
        },
        .size = (uint32_t)stream_length,
        .padding = 0U
    };
    const size_t request_length = sizeof(submit) + stream_length;
    zero_bytes(command_memory, request_length);
    copy_bytes(command_memory, (const uint8_t *)&submit, sizeof(submit));
    copy_bytes(command_memory + sizeof(submit),
               (const uint8_t *)stream, stream_length);
    zero_bytes(command_memory + VIRTIO_GPU_RESPONSE_OFFSET,
               VIRTIO_GPU_RESPONSE_CAPACITY);
    virgl_last_submitted_fence = fence;

    uint32_t response_length = 0U;
    if (!virtio_pci_queue_submit_pair(
            &control_queue, command_physical, (uint32_t)request_length,
            command_physical + VIRTIO_GPU_RESPONSE_OFFSET,
            VIRTIO_GPU_RESPONSE_CAPACITY, VIRTIO_GPU_COMMAND_TIMEOUT,
            &response_length) ||
        response_length < sizeof(struct virtio_gpu_header)) {
        if (virgl_failed_submissions != UINT32_MAX)
            ++virgl_failed_submissions;
        gpu_command_queue_unlock();
        return false;
    }

    const volatile uint8_t *response =
        command_memory + VIRTIO_GPU_RESPONSE_OFFSET;
    const uint32_t response_type = read_le32(response);
    const uint32_t response_flags = read_le32(response + 4U);
    const uint64_t response_fence = read_le64(response + 8U);
    if (response_type != VIRTIO_GPU_RESP_OK_NODATA ||
        (response_flags & VIRTIO_GPU_FLAG_FENCE) == 0U ||
        response_fence != fence) {
        if (virgl_failed_submissions != UINT32_MAX)
            ++virgl_failed_submissions;
        serial_write("VIRTIO GPU: 3D submission response/fence mismatch type/fence=");
        serial_write_hex(response_type);
        serial_write("/");
        serial_write_hex(response_fence);
        serial_write("\r\n");
        gpu_command_queue_unlock();
        return false;
    }

    virgl_last_completed_fence = response_fence;
    if (completed_fence != NULL) *completed_fence = response_fence;
    gpu_command_queue_unlock();
    return true;
}

bool virtio_gpu_3d_get_info(struct nexus_virtio_gpu_3d_info *info) {
    if (info == NULL) return false;
    *info = (struct nexus_virtio_gpu_3d_info){
        .virgl_feature_negotiated = virgl_feature_negotiated,
        .context_created = virgl_context_created,
        .command_submission_ready = virgl_command_submission_ready,
        .capset_id = virgl_capset_id,
        .capset_max_version = virgl_capset_max_version,
        .capset_max_size = virgl_capset_max_size,
        .last_submitted_fence = virgl_last_submitted_fence,
        .last_completed_fence = virgl_last_completed_fence,
        .failed_submissions = virgl_failed_submissions
    };
    return true;
}

bool virtio_gpu_active(void) {
    return active;
}

void virtio_gpu_stop(void) {
    gpu_virgl_destroy_context();
    virgl_command_submission_ready = false;
    gpu_set_virtio_3d_submission_ready(false, 0U);
    active = false;
    if (transport.opened) virtio_pci_fail(&transport);
    serial_write("VIRTIO GPU: disabled; firmware framebuffer fallback selected\r\n");
}

