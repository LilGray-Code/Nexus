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
#define VIRTIO_GPU_RESP_OK_NODATA 0x1100U
#define VIRTIO_GPU_RESP_ERR_UNSPEC 0x1200U
#define VIRTIO_GPU_COMMAND_TIMEOUT 200U
#define VIRTIO_GPU_COMMAND_BYTES 256U
#define VIRTIO_GPU_RESPONSE_OFFSET 512U

struct virtio_gpu_header {
    uint32_t type;
    uint32_t flags;
    uint64_t fence_id;
    uint32_t context_id;
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
static volatile uint8_t *command_memory;

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

static struct virtio_gpu_header make_header(uint32_t type) {
    return (struct virtio_gpu_header){
        .type = type,
        .flags = 0U,
        .fence_id = 0U,
        .context_id = 0U,
        .padding = 0U
    };
}

static bool submit_command(const void *command, size_t command_length) {
    if (!active || command == NULL || command_length == 0U ||
        command_length > VIRTIO_GPU_COMMAND_BYTES ||
        command_memory == NULL) return false;

    zero_bytes(command_memory, 4096U);
    copy_bytes(command_memory, (const uint8_t *)command, command_length);
    uint32_t response_length = 0U;
    if (!virtio_pci_queue_submit_pair(
            &control_queue, command_physical, (uint32_t)command_length,
            command_physical + VIRTIO_GPU_RESPONSE_OFFSET,
            64U, VIRTIO_GPU_COMMAND_TIMEOUT, &response_length)) {
        serial_write("VIRTIO GPU: control queue command timed out or failed type=");
        serial_write_hex(read_le32(command_memory));
        serial_write(" queue=");
        serial_write_hex(VIRTIO_GPU_QUEUE_CONTROL);
        serial_write(" avail=");
        serial_write_hex(control_queue.available_index);
        serial_write(" used=");
        serial_write_hex(control_queue.last_used_index);
        serial_write("\r\n");
        return false;
    }
    const uint32_t response_type =
        read_le32(command_memory + VIRTIO_GPU_RESPONSE_OFFSET);
    if (response_length < sizeof(struct virtio_gpu_header) ||
        response_type != VIRTIO_GPU_RESP_OK_NODATA) {
        serial_write("VIRTIO GPU: device rejected a 2D display command type=");
        serial_write_hex(response_type);
        serial_write("\r\n");
        return false;
    }
    return true;
}

static const struct nexus_pci_device *find_virtio_gpu(void) {
    for (uint32_t i = 0U; i < pci_stored_device_count(); ++i) {
        const struct nexus_pci_device *pci = pci_device_at(i);
        if (pci != NULL && pci->vendor_id == NEXUS_VIRTIO_PCI_VENDOR &&
            pci->device_id == NEXUS_VIRTIO_PCI_DEVICE_GPU) return pci;
    }
    return NULL;
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

    if (!virtio_pci_open(pci, 0U, 0U, &transport)) {
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

    if (!memory_alloc_dma_page(0x100000000ULL, &command_physical)) {
        (void)memory_free_contiguous_pages(framebuffer_physical,
                                           framebuffer_pages);
        framebuffer_pages = 0U;
        virtio_pci_fail(&transport);
        return false;
    }
    command_memory =
        (volatile uint8_t *)memory_phys_to_virt(command_physical);
    if (command_memory == NULL) {
        (void)memory_free_page(command_physical);
        command_physical = 0U;
        (void)memory_free_contiguous_pages(framebuffer_physical,
                                           framebuffer_pages);
        framebuffer_pages = 0U;
        virtio_pci_fail(&transport);
        return false;
    }
    zero_bytes(command_memory, 4096U);

    width_pixels = width;
    height_pixels = height;
    pitch_bytes = width * 4U;
    active = true;
    if (!gpu_create_resource()) {
        active = false;
        virtio_pci_fail(&transport);
        (void)memory_free_page(command_physical);
        command_physical = 0U;
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

bool virtio_gpu_active(void) {
    return active;
}

void virtio_gpu_stop(void) {
    active = false;
    if (transport.opened) virtio_pci_fail(&transport);
    serial_write("VIRTIO GPU: disabled; firmware framebuffer fallback selected\r\n");
}

