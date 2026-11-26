#include "virtio_gpu.h"

#include "gpu.h"
#include "memory.h"
#include "pci.h"
#include "serial.h"
#include "timer.h"
#include "virtio_pci.h"
#include "virgl_protocol.h"

#include <stddef.h>
#include <stdint.h>

#define VIRTIO_GPU_QUEUE_CONTROL 0U
#define VIRTIO_GPU_QUEUE_SIZE 8U
#define VIRTIO_GPU_RESOURCE_ID 1U
#define VIRTIO_GPU_SCANOUT_ID 0U
#define VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM 2U
#define VIRTIO_GPU_CMD_GET_DISPLAY_INFO 0x0100U
#define VIRTIO_GPU_CMD_RESOURCE_CREATE_2D 0x0101U
#define VIRTIO_GPU_CMD_RESOURCE_UNREF 0x0102U
#define VIRTIO_GPU_CMD_SET_SCANOUT 0x0103U
#define VIRTIO_GPU_CMD_RESOURCE_FLUSH 0x0104U
#define VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D 0x0105U
#define VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING 0x0106U
#define VIRTIO_GPU_CMD_GET_CAPSET_INFO 0x0108U
#define VIRTIO_GPU_CMD_GET_EDID 0x010AU
#define VIRTIO_GPU_CMD_CTX_CREATE 0x0200U
#define VIRTIO_GPU_CMD_CTX_DESTROY 0x0201U
#define VIRTIO_GPU_CMD_CTX_ATTACH_RESOURCE 0x0202U
#define VIRTIO_GPU_CMD_CTX_DETACH_RESOURCE 0x0203U
#define VIRTIO_GPU_CMD_RESOURCE_CREATE_3D 0x0204U
#define VIRTIO_GPU_CMD_SUBMIT_3D 0x0207U
#define VIRTIO_GPU_RESP_OK_NODATA 0x1100U
#define VIRTIO_GPU_RESP_OK_DISPLAY_INFO 0x1101U
#define VIRTIO_GPU_RESP_ERR_UNSPEC 0x1200U
#define VIRTIO_GPU_RESP_OK_CAPSET_INFO 0x1102U
#define VIRTIO_GPU_RESP_OK_EDID 0x1104U
#define VIRTIO_GPU_F_VIRGL (1U << 0)
#define VIRTIO_GPU_F_EDID (1U << 1)
#define VIRTIO_GPU_F_CONTEXT_INIT (1U << 4)
#define VIRTIO_GPU_FLAG_FENCE (1U << 0)
#define VIRTIO_GPU_CAPSET_VIRGL 1U
#define VIRTIO_GPU_CAPSET_VIRGL2 2U
#define VIRTIO_GPU_CAPSET_GFXSTREAM 3U
#define VIRTIO_GPU_CAPSET_VENUS 4U
#define VIRTIO_GPU_CONTEXT_ID 1U
#define VIRTIO_GPU_MAX_3D_RESOURCES 64U
#define VIRTIO_GPU_MAX_3D_DIMENSION 16384U
#define VIRTIO_GPU_COMMAND_TIMEOUT 200U
#define VIRTIO_GPU_COMMAND_BYTES 65536U
#define VIRTIO_GPU_RESPONSE_OFFSET VIRTIO_GPU_COMMAND_BYTES
#define VIRTIO_GPU_RESPONSE_CAPACITY 1088U
#define VIRTIO_GPU_COMMAND_POOL_PAGES \
    ((VIRTIO_GPU_RESPONSE_OFFSET + 4096U + NEXUS_PAGE_SIZE - 1U) / \
     NEXUS_PAGE_SIZE)
#define VIRTIO_GPU_MAX_CAPSET_INDICES 32U

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

struct virtio_gpu_ctx_resource {
    struct virtio_gpu_header header;
    uint32_t resource_id;
    uint32_t padding;
} __attribute__((packed));

struct virtio_gpu_resource_unref {
    struct virtio_gpu_header header;
    uint32_t resource_id;
    uint32_t padding;
} __attribute__((packed));

struct virtio_gpu_resource_create_3d {
    struct virtio_gpu_header header;
    uint32_t resource_id;
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
    uint32_t padding;
} __attribute__((packed));

_Static_assert(sizeof(struct virtio_gpu_ctx_resource) == 32U,
               "VirtIO GPU context resource command ABI");
_Static_assert(sizeof(struct virtio_gpu_resource_create_3d) == 72U,
               "VirtIO GPU 3D resource command ABI");

struct virtio_gpu_rectangle {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
} __attribute__((packed));

struct virtio_gpu_display_one {
    struct virtio_gpu_rectangle rectangle;
    uint32_t enabled;
    uint32_t flags;
} __attribute__((packed));

struct virtio_gpu_resp_display_info {
    struct virtio_gpu_header header;
    struct virtio_gpu_display_one scanouts[
        NEXUS_VIRTIO_GPU_MAX_SCANOUTS];
} __attribute__((packed));

_Static_assert(sizeof(struct virtio_gpu_rectangle) == 16U,
               "VirtIO GPU display rectangle ABI");
_Static_assert(sizeof(struct virtio_gpu_display_one) == 24U,
               "VirtIO GPU display scanout ABI");
_Static_assert(sizeof(struct virtio_gpu_resp_display_info) == 408U,
               "VirtIO GPU display info response ABI");

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
static uint32_t selected_scanout_id;
static uint32_t device_scanout_count;
static uint32_t device_capset_count;
static struct nexus_virtio_gpu_display_info display_inventory;
static uint32_t original_scanout_x[NEXUS_VIRTIO_GPU_MAX_SCANOUTS];
static uint32_t original_scanout_y[NEXUS_VIRTIO_GPU_MAX_SCANOUTS];
static uint32_t active_scanout_count;
static uint32_t current_scanout_rect_width[NEXUS_VIRTIO_GPU_MAX_SCANOUTS];
static uint32_t current_scanout_rect_height[NEXUS_VIRTIO_GPU_MAX_SCANOUTS];
static uint64_t framebuffer_physical;
static uint64_t framebuffer_pages;
static volatile uint8_t *framebuffer_memory;
static uint64_t command_physical;
static uint64_t command_pages;
static volatile uint8_t *command_memory;
static bool virgl_feature_negotiated;
static bool virgl_context_created;
static bool virgl_command_submission_ready;
static bool virgl_smoke_test_in_progress;
static uint32_t virgl_capset_id;
static uint32_t virgl_capset_max_version;
static uint32_t virgl_capset_max_size;
static bool gfxstream_capset_available;
static uint32_t gfxstream_capset_max_version;
static uint32_t gfxstream_capset_max_size;
static bool venus_capset_available;
static uint32_t venus_capset_max_version;
static uint32_t venus_capset_max_size;
static uint64_t virgl_next_fence;
static uint64_t virgl_last_submitted_fence;
static uint64_t virgl_last_completed_fence;
static uint32_t virgl_failed_submissions;
static volatile uint32_t command_queue_lock;
static volatile uint32_t resource_table_lock;
static uint32_t next_3d_resource_id = VIRTIO_GPU_RESOURCE_ID + 1U;
struct virtio_gpu_tracked_resource {
    bool allocated;
    bool attached;
    uint32_t resource_id;
    struct nexus_virtio_gpu_3d_resource_desc description;
};


struct virtio_gpu_get_edid {
    struct virtio_gpu_header header;
    uint32_t scanout;
    uint32_t padding;
} __attribute__((packed));

struct virtio_gpu_resp_edid {
    struct virtio_gpu_header header;
    uint32_t size;
    uint32_t padding;
    uint8_t edid[1024];
} __attribute__((packed));

_Static_assert(sizeof(struct virtio_gpu_get_edid) == 32U,
               "VirtIO GPU GET_EDID request ABI");
_Static_assert(sizeof(struct virtio_gpu_resp_edid) == 1056U,
               "VirtIO GPU EDID response ABI");
static struct virtio_gpu_tracked_resource
    tracked_resources[VIRTIO_GPU_MAX_3D_RESOURCES];
static uint32_t tracked_resource_count;
static uint32_t tracked_resource_total_created;
static uint32_t virgl_clear_commands_fenced;
static uint32_t virgl_last_clear_resource_id;
static size_t gpu_find_tracked_resource(uint32_t resource_id);
static bool gpu_resource_destroy_locked(uint32_t resource_id);
static bool gpu_resource_unref(uint32_t resource_id);

static void zero_bytes(volatile uint8_t *output, size_t length) {
    for (size_t i = 0U; i < length; ++i) output[i] = 0U;
}

static void copy_bytes(volatile uint8_t *output, const uint8_t *input,
                       size_t length) {
    for (size_t i = 0U; i < length; ++i) output[i] = input[i];
}

static void copy_from_volatile(uint8_t *output,
                               const volatile uint8_t *input,
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

static void gpu_resource_table_lock(void) {
    while (__atomic_exchange_n(&resource_table_lock, 1U,
                               __ATOMIC_ACQUIRE) != 0U)
        __asm__ volatile ("pause" ::: "memory");
}

static void gpu_resource_table_unlock(void) {
    __atomic_store_n(&resource_table_lock, 0U, __ATOMIC_RELEASE);
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
                               uint32_t *response_length,
                               void *response_payload,
                               size_t response_payload_capacity) {
    if (!active || command == NULL || command_length == 0U ||
        command_length > VIRTIO_GPU_COMMAND_BYTES ||
        command_memory == NULL || response_type == NULL ||
        response_length == NULL ||
        (response_payload == NULL && response_payload_capacity != 0U))
        return false;

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
    if (response_payload != NULL) {
        if (actual_response_length > response_payload_capacity) {
            gpu_command_queue_unlock();
            return false;
        }
        copy_from_volatile((uint8_t *)response_payload,
            command_memory + VIRTIO_GPU_RESPONSE_OFFSET,
            actual_response_length);
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
                            &response_type, &response_length,
                            NULL, 0U)) return false;
    if (response_type != VIRTIO_GPU_RESP_OK_NODATA) {
        serial_write("VIRTIO GPU: device rejected command type=");
        serial_write_hex(response_type);
        serial_write("\r\n");
        return false;
    }
    return response_length >= sizeof(struct virtio_gpu_header);
}

static void gpu_read_device_configuration(void) {
    device_scanout_count = 0U;
    device_capset_count = 0U;
    volatile uint8_t *config = virtio_pci_device_config(&transport);
    if (config == NULL || transport.device_config_length < 16U) {
        serial_write("VIRTIO GPU: device configuration does not expose full scanout/capset counts\r\n");
        return;
    }

    const uint32_t scanouts = read_le32(config + 8U);
    const uint32_t capsets = read_le32(config + 12U);
    if (scanouts == 0U || scanouts > NEXUS_VIRTIO_GPU_MAX_SCANOUTS) {
        serial_write("VIRTIO GPU: invalid advertised scanout count=");
        serial_write_hex(scanouts);
        serial_write("\r\n");
    } else {
        device_scanout_count = scanouts;
    }
    if (capsets > VIRTIO_GPU_MAX_CAPSET_INDICES) {
        serial_write("VIRTIO GPU: capset list exceeds bounded discovery limit; entries beyond limit ignored\r\n");
        device_capset_count = VIRTIO_GPU_MAX_CAPSET_INDICES;
    } else {
        device_capset_count = capsets;
    }
    serial_write("VIRTIO GPU: advertised scanouts/capsets=");
    serial_write_hex(device_scanout_count);
    serial_write("/");
    serial_write_hex(device_capset_count);
    serial_write("\r\n");
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
         index < device_capset_count; ++index) {
        const struct virtio_gpu_get_capset_info request = {
            .header = make_header(VIRTIO_GPU_CMD_GET_CAPSET_INFO),
            .capset_index = index,
            .padding = 0U
        };
        uint32_t response_type = 0U, response_length = 0U;
        uint8_t response[sizeof(struct virtio_gpu_resp_capset_info)];
        if (!submit_command_raw(&request, sizeof(request),
                                &response_type, &response_length,
                                response, sizeof(response)))
            return false;
        if (response_type != VIRTIO_GPU_RESP_OK_CAPSET_INFO)
            break;
        if (response_length < sizeof(struct virtio_gpu_resp_capset_info))
            return false;
        const uint32_t id = read_le32(response + 24U);
        const uint32_t version = read_le32(response + 28U);
        const uint32_t size = read_le32(response + 32U);
        const bool context_init_supported =
            (transport.features & ((uint64_t)1U << VIRTIO_GPU_F_CONTEXT_INIT)) != 0U;
        if (version == 0U || size == 0U) continue;
        if (id == VIRTIO_GPU_CAPSET_GFXSTREAM) {
            gfxstream_capset_available = true;
            gfxstream_capset_max_version = version;
            gfxstream_capset_max_size = size;
            continue;
        }
        if (id == VIRTIO_GPU_CAPSET_VENUS) {
            venus_capset_available = true;
            venus_capset_max_version = version;
            venus_capset_max_size = size;
            continue;
        }
        if (id != VIRTIO_GPU_CAPSET_VIRGL &&
            id != VIRTIO_GPU_CAPSET_VIRGL2) continue;
        /* Without CONTEXT_INIT, the host chooses its default context type.
         * Do not advertise an explicitly discovered VIRGL2 capset as the
         * selected context unless the guest can request that capset.
         */
        if (id == VIRTIO_GPU_CAPSET_VIRGL2 && !context_init_supported)
            continue;
        if (!found || (context_init_supported &&
                       id == VIRTIO_GPU_CAPSET_VIRGL2)) {
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
            (transport.features & ((uint64_t)1U << VIRTIO_GPU_F_CONTEXT_INIT)) != 0U
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
    gpu_resource_table_lock();
    if (!active || !virgl_context_created) {
        gpu_resource_table_unlock();
        return;
    }

    /* Exclude concurrent create/clear/destroy operations for the entire
     * lifetime transition. Each helper below assumes this lock is held.
     */
    for (size_t i = 0U; i < VIRTIO_GPU_MAX_3D_RESOURCES; ++i) {
        if (tracked_resources[i].allocated)
            (void)gpu_resource_destroy_locked(
                tracked_resources[i].resource_id);
    }

    const struct virtio_gpu_ctx_destroy destroy = {
        .header = {
            .type = VIRTIO_GPU_CMD_CTX_DESTROY,
            .flags = 0U,
            .fence_id = 0U,
            .context_id = VIRTIO_GPU_CONTEXT_ID,
            .padding = 0U
        }
    };
    if (submit_command(&destroy, sizeof(destroy))) {
        virgl_context_created = false;
        /* A detach failure is recoverable once the context is gone: retry
         * RESOURCE_UNREF without a context detach to avoid leaking handles.
         */
        for (size_t i = 0U; i < VIRTIO_GPU_MAX_3D_RESOURCES; ++i) {
            struct virtio_gpu_tracked_resource *resource =
                &tracked_resources[i];
            if (!resource->allocated) continue;
            if (gpu_resource_unref(resource->resource_id)) {
                *resource = (struct virtio_gpu_tracked_resource){0};
                if (tracked_resource_count != 0U) --tracked_resource_count;
            }
        }
    }
    virgl_command_submission_ready = false;
    gpu_set_virtio_3d_submission_ready(false, 0U);
    gpu_resource_table_unlock();
}

static bool gpu_virgl_start(void) {
    virgl_feature_negotiated =
        device_capset_count != 0U &&
        (transport.features & ((uint64_t)1U << VIRTIO_GPU_F_VIRGL)) != 0U;
    virgl_context_created = false;
    virgl_command_submission_ready = false;
    virgl_smoke_test_in_progress = false;
    virgl_capset_id = 0U;
    virgl_capset_max_version = 0U;
    virgl_capset_max_size = 0U;
    gfxstream_capset_available = false;
    gfxstream_capset_max_version = 0U;
    gfxstream_capset_max_size = 0U;
    venus_capset_available = false;
    venus_capset_max_version = 0U;
    venus_capset_max_size = 0U;
    virgl_next_fence = 1U;
    virgl_last_submitted_fence = 0U;
    virgl_last_completed_fence = 0U;
    virgl_failed_submissions = 0U;
    next_3d_resource_id = VIRTIO_GPU_RESOURCE_ID + 1U;
    tracked_resource_count = 0U;
    tracked_resource_total_created = 0U;
    virgl_clear_commands_fenced = 0U;
    virgl_last_clear_resource_id = 0U;
    for (size_t i = 0U; i < VIRTIO_GPU_MAX_3D_RESOURCES; ++i)
        tracked_resources[i] = (struct virtio_gpu_tracked_resource){0};
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
    virgl_smoke_test_in_progress = true;
    const bool smoke_test_ok =
        virtio_gpu_3d_submit(&virgl_nop, sizeof(virgl_nop), NULL);
    virgl_smoke_test_in_progress = false;
    if (!smoke_test_ok) {
        gpu_virgl_destroy_context();
        serial_write("VIRTIO GPU: Virgl command/fence smoke test failed\r\n");
        return false;
    }
    virgl_command_submission_ready = true;
    gpu_set_virtio_3d_submission_ready(true, virgl_capset_id);
    serial_write("VIRTIO GPU: Virgl context and fence-backed command queue ready; draw acceleration not yet enabled\r\n");
    return true;
}

static bool gpu_query_display_info(uint64_t preferred_width,
                                   uint64_t preferred_height) {
    const struct virtio_gpu_header request =
        make_header(VIRTIO_GPU_CMD_GET_DISPLAY_INFO);
    uint32_t response_type = 0U, response_length = 0U;
    uint8_t response[sizeof(struct virtio_gpu_resp_display_info)];
    if (!submit_command_raw(&request, sizeof(request),
                            &response_type, &response_length,
                            response, sizeof(response)) ||
        response_type != VIRTIO_GPU_RESP_OK_DISPLAY_INFO ||
        response_length < sizeof(struct virtio_gpu_resp_display_info)) {
        display_inventory.valid = false;
        selected_scanout_id = VIRTIO_GPU_SCANOUT_ID;
        serial_write("VIRTIO GPU: display-info query unavailable; defaulting to scanout 0\r\n");
        return false;
    }

    display_inventory = (struct nexus_virtio_gpu_display_info){0};
    if (device_scanout_count == 0U) {
        display_inventory = (struct nexus_virtio_gpu_display_info){0};
        selected_scanout_id = VIRTIO_GPU_SCANOUT_ID;
        return false;
    }
    display_inventory.valid = true;
    display_inventory.scanout_count = device_scanout_count;
    bool have_enabled = false;
    bool have_exact = false;
    uint32_t selected = VIRTIO_GPU_SCANOUT_ID;

    for (uint32_t i = 0U; i < device_scanout_count; ++i) {
        const size_t offset = sizeof(struct virtio_gpu_header) +
            (size_t)i * sizeof(struct virtio_gpu_display_one);
        struct nexus_virtio_gpu_scanout_info *saved =
            &display_inventory.scanouts[i];
        saved->x = read_le32(response + offset);
        saved->y = read_le32(response + offset + 4U);
        original_scanout_x[i] = saved->x;
        original_scanout_y[i] = saved->y;
        saved->width = read_le32(response + offset + 8U);
        saved->height = read_le32(response + offset + 12U);
        current_scanout_rect_width[i] = saved->width;
        current_scanout_rect_height[i] = saved->height;
        saved->enabled = read_le32(response + offset + 16U) != 0U;
        saved->flags = read_le32(response + offset + 20U);
        if (!saved->enabled || saved->width == 0U ||
            saved->height == 0U) continue;

        if (!have_enabled) {
            selected = i;
            have_enabled = true;
        }
        if (!have_exact && saved->width == preferred_width &&
            saved->height == preferred_height) {
            selected = i;
            have_exact = true;
        }
    }

    selected_scanout_id = have_enabled ? selected : VIRTIO_GPU_SCANOUT_ID;
    display_inventory.selected_scanout = selected_scanout_id;
    serial_write("VIRTIO GPU: scanouts discovered/enabled/selected=");
    serial_write_hex(display_inventory.scanout_count);
    serial_write("/");
    uint32_t enabled_count = 0U;
    for (uint32_t i = 0U; i < display_inventory.scanout_count; ++i) {
        const struct nexus_virtio_gpu_scanout_info *scanout =
            &display_inventory.scanouts[i];
        if (scanout->enabled && scanout->width != 0U &&
            scanout->height != 0U) ++enabled_count;
    }
    active_scanout_count = enabled_count;
    serial_write_hex(enabled_count);
    serial_write("/");
    serial_write_hex(selected_scanout_id);
    serial_write("\r\n");
    return have_enabled;
}

static bool gpu_compute_display_extent(uint64_t preferred_width,
                                      uint64_t preferred_height,
                                      uint64_t *extent_width,
                                      uint64_t *extent_height) {
    if (extent_width == NULL || extent_height == NULL ||
        preferred_width == 0U || preferred_height == 0U) return false;

    uint64_t rightmost = 0U;
    uint64_t bottommost = 0U;
    uint32_t active_scanouts = 0U;
    for (uint32_t i = 0U; i < display_inventory.scanout_count; ++i) {
        const struct nexus_virtio_gpu_scanout_info *scanout =
            &display_inventory.scanouts[i];
        if (!scanout->enabled || scanout->width == 0U ||
            scanout->height == 0U) continue;
        const uint64_t right = (uint64_t)scanout->x + scanout->width;
        const uint64_t bottom = (uint64_t)scanout->y + scanout->height;
        if (right > rightmost) rightmost = right;
        if (bottom > bottommost) bottommost = bottom;
        ++active_scanouts;
    }

    if (active_scanouts == 0U) {
        *extent_width = preferred_width;
        *extent_height = preferred_height;
        return true;
    }

    /* Scanout rectangles are expressed in the common resource coordinate
     * space. Keep the advertised host positions to build an extended desktop;
     * identical/overlapping rectangles continue to mirror the same pixels.
     */
    if (rightmost < preferred_width) rightmost = preferred_width;
    if (bottommost < preferred_height) bottommost = preferred_height;
    if (rightmost == 0U || bottommost == 0U ||
        rightmost > 16384U || bottommost > 16384U ||
        rightmost > UINT64_MAX / 4U ||
        bottommost > UINT64_MAX / (rightmost * 4U) ||
        rightmost * bottommost * 4U > UINT32_MAX) return false;
    *extent_width = rightmost;
    *extent_height = bottommost;
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

    bool attached_scanout = false;
    if (display_inventory.valid) {
        for (uint32_t i = 0U; i < display_inventory.scanout_count; ++i) {
            const struct nexus_virtio_gpu_scanout_info *scanout =
                &display_inventory.scanouts[i];
            if (!scanout->enabled || scanout->width == 0U ||
                scanout->height == 0U) continue;
            const struct virtio_gpu_set_scanout set = {
                .header = make_header(VIRTIO_GPU_CMD_SET_SCANOUT),
                .rectangle = {
                    scanout->x, scanout->y,
                    scanout->width, scanout->height
                },
                .scanout_id = i,
                .resource_id = VIRTIO_GPU_RESOURCE_ID
            };
            if (!submit_command(&set, sizeof(set))) return false;
            attached_scanout = true;
        }
    }

    if (!attached_scanout) {
        const struct virtio_gpu_set_scanout set = {
            .header = make_header(VIRTIO_GPU_CMD_SET_SCANOUT),
            .rectangle = {0U, 0U, (uint32_t)width_pixels,
                          (uint32_t)height_pixels},
            .scanout_id = selected_scanout_id,
            .resource_id = VIRTIO_GPU_RESOURCE_ID
        };
        if (!submit_command(&set, sizeof(set))) return false;
    }
    return true;
}

bool virtio_gpu_start(uint64_t width, uint64_t height,
                      const struct nexus_graphics_surface *firmware_surface,
                      struct nexus_graphics_surface *render_surface) {
    active = false;
    virgl_feature_negotiated = false;
    virgl_context_created = false;
    virgl_command_submission_ready = false;
    virgl_smoke_test_in_progress = false;
    virgl_capset_id = 0U;
    virgl_capset_max_version = 0U;
    virgl_capset_max_size = 0U;
    virgl_next_fence = 1U;
    virgl_last_submitted_fence = 0U;
    virgl_last_completed_fence = 0U;
    virgl_failed_submissions = 0U;
    gfxstream_capset_available = false;
    gfxstream_capset_max_version = 0U;
    gfxstream_capset_max_size = 0U;
    venus_capset_available = false;
    venus_capset_max_version = 0U;
    venus_capset_max_size = 0U;
    selected_scanout_id = VIRTIO_GPU_SCANOUT_ID;
    active_scanout_count = 0U;
    for (uint32_t i = 0U; i < NEXUS_VIRTIO_GPU_MAX_SCANOUTS; ++i) {
        original_scanout_x[i] = 0U;
        original_scanout_y[i] = 0U;
        current_scanout_rect_width[i] = 0U;
        current_scanout_rect_height[i] = 0U;
    }
    device_scanout_count = 0U;
    device_capset_count = 0U;
    display_inventory = (struct nexus_virtio_gpu_display_info){0};
    next_3d_resource_id = VIRTIO_GPU_RESOURCE_ID + 1U;
    tracked_resource_count = 0U;
    tracked_resource_total_created = 0U;
    virgl_clear_commands_fenced = 0U;
    virgl_last_clear_resource_id = 0U;
    for (size_t i = 0U; i < VIRTIO_GPU_MAX_3D_RESOURCES; ++i)
        tracked_resources[i] = (struct virtio_gpu_tracked_resource){0};
    gpu_set_virtio_3d_submission_ready(false, 0U);

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
        (1U << VIRTIO_GPU_F_VIRGL) | (1U << VIRTIO_GPU_F_EDID) |
        (1U << VIRTIO_GPU_F_CONTEXT_INIT);
    if (!virtio_pci_open(pci, 0U, optional_gpu_features, &transport)) {
        serial_write("VIRTIO GPU: modern PCI capability negotiation unavailable\r\n");
        return false;
    }
    gpu_read_device_configuration();
    if (!virtio_pci_queue_init(&transport, &control_queue,
                               VIRTIO_GPU_QUEUE_CONTROL,
                               VIRTIO_GPU_QUEUE_SIZE)) {
        virtio_pci_fail(&transport);
        serial_write("VIRTIO GPU: unable to initialize control queue\r\n");
        return false;
    }
    if (!virtio_pci_finish(&transport)) return false;

    /* Allocate the command queue before querying outputs; all later
     * framebuffer dimensions are derived from the device's scanout rectangles.
     */
    command_pages = VIRTIO_GPU_COMMAND_POOL_PAGES;
    if (!memory_alloc_contiguous_pages(command_pages, 0x100000000ULL,
                                       &command_physical)) {
        virtio_pci_fail(&transport);
        serial_write("VIRTIO GPU: command DMA pool allocation failed\r\n");
        return false;
    }
    command_memory =
        (volatile uint8_t *)memory_phys_to_virt(command_physical);
    if (command_memory == NULL) {
        (void)memory_free_contiguous_pages(command_physical, command_pages);
        command_physical = 0U;
        command_pages = 0U;
        virtio_pci_fail(&transport);
        return false;
    }
    zero_bytes(command_memory,
               VIRTIO_GPU_RESPONSE_OFFSET + VIRTIO_GPU_RESPONSE_CAPACITY);

    active = true;
    (void)gpu_query_display_info(width, height);
    uint64_t desktop_width = width;
    uint64_t desktop_height = height;
    if (!gpu_compute_display_extent(width, height,
                                    &desktop_width, &desktop_height)) {
        active = false;
        virtio_pci_fail(&transport);
        (void)memory_free_contiguous_pages(command_physical, command_pages);
        command_physical = 0U;
        command_pages = 0U;
        command_memory = NULL;
        serial_write("VIRTIO GPU: combined scanout dimensions exceed safe framebuffer bounds\r\n");
        return false;
    }

    width_pixels = desktop_width;
    height_pixels = desktop_height;
    pitch_bytes = desktop_width * 4U;
    const uint64_t total_bytes = pitch_bytes * desktop_height;
    framebuffer_pages = (total_bytes + NEXUS_PAGE_SIZE - 1U) /
                        NEXUS_PAGE_SIZE;
    if (total_bytes > UINT32_MAX || framebuffer_pages == 0U ||
        !memory_alloc_contiguous_pages(framebuffer_pages, 0x100000000ULL,
                                       &framebuffer_physical)) {
        active = false;
        virtio_pci_fail(&transport);
        (void)memory_free_contiguous_pages(command_physical, command_pages);
        command_physical = 0U;
        command_pages = 0U;
        command_memory = NULL;
        serial_write("VIRTIO GPU: contiguous extended-desktop framebuffer allocation failed\r\n");
        return false;
    }
    framebuffer_memory =
        (volatile uint8_t *)memory_phys_to_virt(framebuffer_physical);
    if (framebuffer_memory == NULL) {
        (void)memory_free_contiguous_pages(framebuffer_physical,
                                           framebuffer_pages);
        framebuffer_pages = 0U;
        (void)memory_free_contiguous_pages(command_physical, command_pages);
        command_physical = 0U;
        command_pages = 0U;
        command_memory = NULL;
        active = false;
        virtio_pci_fail(&transport);
        return false;
    }
    for (uint64_t i = 0U; i < framebuffer_pages * NEXUS_PAGE_SIZE; ++i)
        framebuffer_memory[i] = 0U;

    if (!gpu_create_resource()) {
        active = false;
        virtio_pci_fail(&transport);
        (void)memory_free_contiguous_pages(command_physical, command_pages);
        command_physical = 0U;
        command_pages = 0U;
        command_memory = NULL;
        (void)memory_free_contiguous_pages(framebuffer_physical,
                                           framebuffer_pages);
        framebuffer_pages = 0U;
        framebuffer_physical = 0U;
        framebuffer_memory = NULL;
        serial_write("VIRTIO GPU: unable to attach the shared desktop resource to every scanout\r\n");
        return false;
    }

    *render_surface = *firmware_surface;
    render_surface->address = framebuffer_memory;
    render_surface->width = width_pixels;
    render_surface->height = height_pixels;
    render_surface->pitch = pitch_bytes;
    serial_write("VIRTIO GPU: shared desktop framebuffer attached to discovered scanouts\r\n");
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
    if (!active || !virgl_context_created ||
        (!virgl_command_submission_ready && !virgl_smoke_test_in_progress) ||
        stream == NULL || stream_length == 0U || (stream_length & 3U) != 0U ||
        stream_length > VIRTIO_GPU_COMMAND_BYTES -
                        sizeof(struct virtio_gpu_submit_3d) ||
        command_memory == NULL) return false;

    gpu_command_queue_lock();
    if (!active || !virgl_context_created ||
        (!virgl_command_submission_ready && !virgl_smoke_test_in_progress) ||
        virgl_next_fence == UINT64_MAX) {
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
        virgl_command_submission_ready = false;
        gpu_set_virtio_3d_submission_ready(false, 0U);
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
        virgl_command_submission_ready = false;
        gpu_set_virtio_3d_submission_ready(false, 0U);
        gpu_command_queue_unlock();
        return false;
    }

    virgl_last_completed_fence = response_fence;
    if (completed_fence != NULL) *completed_fence = response_fence;
    gpu_command_queue_unlock();
    return true;
}

static size_t gpu_find_tracked_resource(uint32_t resource_id) {
    for (size_t i = 0U; i < VIRTIO_GPU_MAX_3D_RESOURCES; ++i)
        if (tracked_resources[i].allocated &&
            tracked_resources[i].resource_id == resource_id) return i;
    return VIRTIO_GPU_MAX_3D_RESOURCES;
}

static bool gpu_resource_unref(uint32_t resource_id) {
    const struct virtio_gpu_resource_unref request = {
        .header = make_header(VIRTIO_GPU_CMD_RESOURCE_UNREF),
        .resource_id = resource_id,
        .padding = 0U
    };
    return submit_command(&request, sizeof(request));
}

bool virtio_gpu_3d_resource_create(
    const struct nexus_virtio_gpu_3d_resource_desc *description,
    uint32_t *resource_id) {
    if (description == NULL || resource_id == NULL ||
        !active || !virgl_context_created ||
        !virgl_command_submission_ready ||
        description->width == 0U || description->height == 0U ||
        description->depth == 0U || description->array_size == 0U ||
        description->width > VIRTIO_GPU_MAX_3D_DIMENSION ||
        description->height > VIRTIO_GPU_MAX_3D_DIMENSION ||
        description->depth > VIRTIO_GPU_MAX_3D_DIMENSION ||
        description->array_size > VIRTIO_GPU_MAX_3D_DIMENSION ||
        description->last_level > 15U) return false;

    gpu_resource_table_lock();
    size_t free_slot = VIRTIO_GPU_MAX_3D_RESOURCES;
    for (size_t i = 0U; i < VIRTIO_GPU_MAX_3D_RESOURCES; ++i) {
        if (!tracked_resources[i].allocated) {
            free_slot = i;
            break;
        }
    }
    if (free_slot == VIRTIO_GPU_MAX_3D_RESOURCES ||
        next_3d_resource_id == 0U ||
        next_3d_resource_id == UINT32_MAX) {
        gpu_resource_table_unlock();
        return false;
    }

    const uint32_t id = next_3d_resource_id++;
    const struct virtio_gpu_resource_create_3d create = {
        .header = make_header(VIRTIO_GPU_CMD_RESOURCE_CREATE_3D),
        .resource_id = id,
        .target = description->target,
        .format = description->format,
        .bind = description->bind,
        .width = description->width,
        .height = description->height,
        .depth = description->depth,
        .array_size = description->array_size,
        .last_level = description->last_level,
        .nr_samples = description->nr_samples,
        .flags = description->flags,
        .padding = 0U
    };
    if (!submit_command(&create, sizeof(create))) {
        gpu_resource_table_unlock();
        return false;
    }

    tracked_resources[free_slot] = (struct virtio_gpu_tracked_resource){
        .allocated = true,
        .attached = false,
        .resource_id = id,
        .description = *description
    };
    ++tracked_resource_count;
    if (tracked_resource_total_created != UINT32_MAX)
        ++tracked_resource_total_created;

    const struct virtio_gpu_ctx_resource attach = {
        .header = {
            .type = VIRTIO_GPU_CMD_CTX_ATTACH_RESOURCE,
            .flags = 0U,
            .fence_id = 0U,
            .context_id = VIRTIO_GPU_CONTEXT_ID,
            .padding = 0U
        },
        .resource_id = id,
        .padding = 0U
    };
    if (!submit_command(&attach, sizeof(attach))) {
        if (gpu_resource_unref(id)) {
            tracked_resources[free_slot] =
                (struct virtio_gpu_tracked_resource){0};
            --tracked_resource_count;
        }
        gpu_resource_table_unlock();
        return false;
    }
    tracked_resources[free_slot].attached = true;
    *resource_id = id;
    gpu_resource_table_unlock();
    return true;
}

static bool gpu_resource_destroy_locked(uint32_t resource_id) {
    const size_t slot = gpu_find_tracked_resource(resource_id);
    if (slot == VIRTIO_GPU_MAX_3D_RESOURCES) return false;

    struct virtio_gpu_tracked_resource *resource = &tracked_resources[slot];
    if (resource->attached) {
        const struct virtio_gpu_ctx_resource detach = {
            .header = {
                .type = VIRTIO_GPU_CMD_CTX_DETACH_RESOURCE,
                .flags = 0U,
                .fence_id = 0U,
                .context_id = VIRTIO_GPU_CONTEXT_ID,
                .padding = 0U
            },
            .resource_id = resource_id,
            .padding = 0U
        };
        if (!submit_command(&detach, sizeof(detach))) return false;
        resource->attached = false;
    }

    if (!gpu_resource_unref(resource_id)) return false;
    *resource = (struct virtio_gpu_tracked_resource){0};
    if (tracked_resource_count != 0U) --tracked_resource_count;
    return true;
}

bool virtio_gpu_3d_resource_destroy(uint32_t resource_id) {
    if (!active || !virgl_context_created ||
        resource_id <= VIRTIO_GPU_RESOURCE_ID) return false;
    gpu_resource_table_lock();
    const bool destroyed = gpu_resource_destroy_locked(resource_id);
    gpu_resource_table_unlock();
    return destroyed;
}

bool virtio_gpu_display_get_info(
    struct nexus_virtio_gpu_display_info *info) {
    if (info == NULL || !display_inventory.valid) return false;
    *info = display_inventory;
    return true;
}

static bool gpu_set_scanout_rectangle(uint32_t scanout_id,
                                      uint32_t x, uint32_t y,
                                      uint32_t width, uint32_t height) {
    const struct virtio_gpu_set_scanout set = {
        .header = make_header(VIRTIO_GPU_CMD_SET_SCANOUT),
        .rectangle = {x, y, width, height},
        .scanout_id = scanout_id,
        .resource_id = VIRTIO_GPU_RESOURCE_ID
    };
    return submit_command(&set, sizeof(set));
}

bool virtio_gpu_display_set_primary(uint32_t scanout_id) {
    if (!active || !display_inventory.valid ||
        scanout_id >= display_inventory.scanout_count ||
        !display_inventory.scanouts[scanout_id].enabled) return false;
    selected_scanout_id = scanout_id;
    display_inventory.selected_scanout = scanout_id;
    return true;
}

bool virtio_gpu_display_set_topology(uint32_t topology) {
    if (!active || !display_inventory.valid || active_scanout_count < 2U ||
        (topology != NEXUS_VIRTIO_GPU_TOPOLOGY_EXTENDED &&
         topology != NEXUS_VIRTIO_GPU_TOPOLOGY_MIRRORED)) return false;

    uint32_t mirror_width = UINT32_MAX;
    uint32_t mirror_height = UINT32_MAX;
    for (uint32_t i = 0U; i < display_inventory.scanout_count; ++i) {
        const struct nexus_virtio_gpu_scanout_info *scanout =
            &display_inventory.scanouts[i];
        if (!scanout->enabled || scanout->width == 0U ||
            scanout->height == 0U) continue;
        if (scanout->width < mirror_width) mirror_width = scanout->width;
        if (scanout->height < mirror_height) mirror_height = scanout->height;
    }
    if (topology == NEXUS_VIRTIO_GPU_TOPOLOGY_MIRRORED &&
        (mirror_width == UINT32_MAX || mirror_height == UINT32_MAX ||
         mirror_width == 0U || mirror_height == 0U)) return false;

    /* Do not claim "extended" if the host's saved output rectangles overlap.
     * The backing resource was sized from these positions during startup, so
     * inventing new coordinates here would exceed its allocated extent.
     */
    if (topology == NEXUS_VIRTIO_GPU_TOPOLOGY_EXTENDED) {
        for (uint32_t i = 0U; i < display_inventory.scanout_count; ++i) {
            const struct nexus_virtio_gpu_scanout_info *a =
                &display_inventory.scanouts[i];
            if (!a->enabled || a->width == 0U || a->height == 0U) continue;
            for (uint32_t j = i + 1U;
                 j < display_inventory.scanout_count; ++j) {
                const struct nexus_virtio_gpu_scanout_info *b =
                    &display_inventory.scanouts[j];
                if (!b->enabled || b->width == 0U || b->height == 0U)
                    continue;
                const uint64_t ar = (uint64_t)original_scanout_x[i] +
                                    a->width;
                const uint64_t ab = (uint64_t)original_scanout_y[i] +
                                    a->height;
                const uint64_t br = (uint64_t)original_scanout_x[j] +
                                    b->width;
                const uint64_t bb = (uint64_t)original_scanout_y[j] +
                                    b->height;
                const bool overlap =
                    original_scanout_x[i] < br &&
                    original_scanout_x[j] < ar &&
                    original_scanout_y[i] < bb &&
                    original_scanout_y[j] < ab;
                if (overlap) return false;
            }
        }
    }

    struct nexus_virtio_gpu_scanout_info previous[
        NEXUS_VIRTIO_GPU_MAX_SCANOUTS];
    uint32_t previous_rect_width[NEXUS_VIRTIO_GPU_MAX_SCANOUTS];
    uint32_t previous_rect_height[NEXUS_VIRTIO_GPU_MAX_SCANOUTS];
    for (uint32_t i = 0U; i < display_inventory.scanout_count; ++i) {
        previous[i] = display_inventory.scanouts[i];
        previous_rect_width[i] = current_scanout_rect_width[i];
        previous_rect_height[i] = current_scanout_rect_height[i];
    }

    for (uint32_t i = 0U; i < display_inventory.scanout_count; ++i) {
        const struct nexus_virtio_gpu_scanout_info *scanout =
            &display_inventory.scanouts[i];
        if (!scanout->enabled || scanout->width == 0U ||
            scanout->height == 0U) continue;

        const uint32_t x = topology == NEXUS_VIRTIO_GPU_TOPOLOGY_MIRRORED
            ? 0U : original_scanout_x[i];
        const uint32_t y = topology == NEXUS_VIRTIO_GPU_TOPOLOGY_MIRRORED
            ? 0U : original_scanout_y[i];
        const uint32_t width =
            topology == NEXUS_VIRTIO_GPU_TOPOLOGY_MIRRORED
                ? mirror_width : scanout->width;
        const uint32_t height =
            topology == NEXUS_VIRTIO_GPU_TOPOLOGY_MIRRORED
                ? mirror_height : scanout->height;
        if (width == 0U || height == 0U ||
            (uint64_t)x + width > width_pixels ||
            (uint64_t)y + height > height_pixels ||
            !gpu_set_scanout_rectangle(i, x, y, width, height)) {
            /* A lost error reply is ambiguous: restore the failed output too,
             * because it may have accepted SET_SCANOUT before timing out.
             */
            bool rollback_ok = true;
            for (uint32_t j = 0U; j <= i; ++j) {
                if (!previous[j].enabled || previous_rect_width[j] == 0U ||
                    previous_rect_height[j] == 0U) continue;
                if (!gpu_set_scanout_rectangle(
                        j, previous[j].x, previous[j].y,
                        previous_rect_width[j], previous_rect_height[j]))
                    rollback_ok = false;
            }
            for (uint32_t j = 0U; j < display_inventory.scanout_count; ++j) {
                display_inventory.scanouts[j] = previous[j];
                current_scanout_rect_width[j] = previous_rect_width[j];
                current_scanout_rect_height[j] = previous_rect_height[j];
            }
            if (!rollback_ok) {
                serial_write("VIRTIO GPU: scanout topology rollback failed; disabling display command path\r\n");
                display_inventory.valid = false;
                active = false;
                virtio_pci_fail(&transport);
            }
            return false;
        }
        display_inventory.scanouts[i].x = x;
        display_inventory.scanouts[i].y = y;
        current_scanout_rect_width[i] = width;
        current_scanout_rect_height[i] = height;
    }
    return true;
}

bool virtio_gpu_display_get_edid(uint32_t scanout_id,
                                 uint8_t *edid, size_t edid_capacity,
                                 size_t *edid_length) {
    if (!active || edid == NULL || edid_length == NULL ||
        edid_capacity < 128U ||
        (transport.features & ((uint64_t)1U << VIRTIO_GPU_F_EDID)) == 0U ||
        scanout_id >= device_scanout_count) return false;

    const struct virtio_gpu_get_edid request = {
        .header = make_header(VIRTIO_GPU_CMD_GET_EDID),
        .scanout = scanout_id,
        .padding = 0U
    };
    uint8_t response[sizeof(struct virtio_gpu_resp_edid)];
    uint32_t response_type = 0U, response_length = 0U;
    if (!submit_command_raw(&request, sizeof(request),
                            &response_type, &response_length,
                            response, sizeof(response)) ||
        response_type != VIRTIO_GPU_RESP_OK_EDID ||
        response_length < offsetof(struct virtio_gpu_resp_edid, edid))
        return false;

    const uint32_t size = read_le32(
        (const volatile uint8_t *)response +
        offsetof(struct virtio_gpu_resp_edid, size));
    if (size < 128U || size > sizeof(((struct virtio_gpu_resp_edid *)0)->edid) ||
        (size % 128U) != 0U ||
        size > response_length -
            offsetof(struct virtio_gpu_resp_edid, edid) ||
        size > edid_capacity) return false;

    for (uint32_t i = 0U; i < size; ++i)
        edid[i] = response[offsetof(struct virtio_gpu_resp_edid, edid) + i];
    *edid_length = size;
    return true;
}

bool virtio_gpu_3d_clear_rgba32(uint32_t resource_id,
                               const uint32_t rgba[4],
                               uint64_t *completed_fence) {
    if (resource_id <= VIRTIO_GPU_RESOURCE_ID || rgba == NULL ||
        !active || !virgl_context_created ||
        !virgl_command_submission_ready) return false;

    gpu_resource_table_lock();
    const size_t slot = gpu_find_tracked_resource(resource_id);
    if (slot == VIRTIO_GPU_MAX_3D_RESOURCES) {
        gpu_resource_table_unlock();
        return false;
    }
    const struct virtio_gpu_tracked_resource *resource =
        &tracked_resources[slot];
    const struct nexus_virtio_gpu_3d_resource_desc *description =
        &resource->description;
    if (!resource->attached ||
        description->target != NEXUS_VIRGL_PIPE_TEXTURE_2D ||
        (description->bind & NEXUS_VIRGL_PIPE_BIND_RENDER_TARGET) == 0U ||
        description->width == 0U || description->height == 0U ||
        description->depth != 1U || description->array_size != 1U ||
        description->last_level != 0U ||
        description->nr_samples > 1U) {
        gpu_resource_table_unlock();
        return false;
    }

    uint32_t stream[NEXUS_VIRGL_CLEAR_SEQUENCE_DWORDS];
    size_t stream_dwords = 0U;
    if (!nexus_virgl_encode_clear_rgba32(
            stream, NEXUS_VIRGL_CLEAR_SEQUENCE_DWORDS,
            resource_id, resource_id, description->format,
            rgba, &stream_dwords)) {
        gpu_resource_table_unlock();
        return false;
    }

    uint64_t fence = 0U;
    const bool completed = virtio_gpu_3d_submit(
        stream, stream_dwords * sizeof(stream[0]), &fence);
    if (completed) {
        if (virgl_clear_commands_fenced != UINT32_MAX)
            ++virgl_clear_commands_fenced;
        virgl_last_clear_resource_id = resource_id;
        if (completed_fence != NULL) *completed_fence = fence;
    }
    gpu_resource_table_unlock();
    return completed;
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
        .gfxstream_capset_available = gfxstream_capset_available,
        .gfxstream_capset_max_version = gfxstream_capset_max_version,
        .gfxstream_capset_max_size = gfxstream_capset_max_size,
        .venus_capset_available = venus_capset_available,
        .venus_capset_max_version = venus_capset_max_version,
        .venus_capset_max_size = venus_capset_max_size,
        .last_submitted_fence = virgl_last_submitted_fence,
        .last_completed_fence = virgl_last_completed_fence,
        .failed_submissions = virgl_failed_submissions,
        .resources_allocated = tracked_resource_count,
        .resources_created_total = tracked_resource_total_created,
        .clear_commands_fenced = virgl_clear_commands_fenced,
        .last_clear_resource_id = virgl_last_clear_resource_id
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

