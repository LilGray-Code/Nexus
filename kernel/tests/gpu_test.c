#include "gpu.h"
#include "pci.h"
#include "serial.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

static struct nexus_pci_device devices[2];
static uint32_t device_count;

uint32_t pci_stored_device_count(void) { return device_count; }
const struct nexus_pci_device *pci_device_at(uint32_t index) {
    return index < device_count ? &devices[index] : NULL;
}
void serial_write(const char *text) { (void)text; }
void serial_write_hex(uint64_t value) { (void)value; }

static void test_intel_adapter_and_cpu_fallback(void) {
    devices[0] = (struct nexus_pci_device){
        .class_code = 0x03U, .subclass = 0x00U,
        .vendor_id = 0x8086U, .device_id = 0x1234U,
        .revision_id = 0x07U,
        .subsystem_vendor_id = 0x1028U,
        .subsystem_device_id = 0x0A5CU
    };
    device_count = 1U;
    assert(gpu_init());
    const struct nexus_gpu_info *info = gpu_primary_info();
    assert(info->present);
    assert(gpu_adapter_count() == 1U);
    assert(gpu_adapter_at(0U) == info);
    assert(gpu_adapter_at(1U) == NULL);
    assert(info->vendor == NEXUS_GPU_VENDOR_INTEL);
    assert(info->vendor_id == 0x8086U);
    assert(info->device_id == 0x1234U);
    assert(info->revision_id == 0x07U);
    assert(info->subsystem_vendor_id == 0x1028U);
    assert(info->subsystem_device_id == 0x0A5CU);
    assert(!gpu_hardware_acceleration_available());
    assert(!info->firmware_framebuffer_active);
    gpu_set_firmware_framebuffer_active(true);
    assert(info->firmware_framebuffer_active);
    assert(strcmp(gpu_render_path_name(),
                  "GPU: FIRMWARE SCANOUT / CPU RASTER") == 0);
}

static void test_nvidia_and_virtio_adapter_names(void) {
    devices[0] = (struct nexus_pci_device){
        .class_code = 0x03U, .subclass = 0x00U,
        .vendor_id = 0x10DEU, .device_id = 0x1CB3U
    };
    devices[1] = (struct nexus_pci_device){
        .class_code = 0x03U, .subclass = 0x00U,
        .bus = 0U, .slot = 4U,
        .vendor_id = 0x1002U, .device_id = 0x67DFU
    };
    device_count = 2U;
    assert(gpu_init());
    assert(gpu_primary_info()->vendor == NEXUS_GPU_VENDOR_NVIDIA);
    assert(gpu_adapter_count() == 2U);
    assert(gpu_adapter_at(1U)->vendor == NEXUS_GPU_VENDOR_AMD);

    devices[0].vendor_id = 0x1AF4U;
    devices[0].device_id = 0x1050U;
    assert(gpu_init());
    assert(gpu_primary_info()->vendor == NEXUS_GPU_VENDOR_VIRTIO);
    assert(!gpu_hardware_acceleration_available());
    device_count = 0U;
    assert(!gpu_init());
    assert(gpu_adapter_count() == 0U);
    assert(gpu_primary_info() == NULL);
    gpu_set_firmware_framebuffer_active(true);
    assert(gpu_render_path_name()[0] == 'G');
}

int main(void) {
    test_intel_adapter_and_cpu_fallback();
    test_nvidia_and_virtio_adapter_names();
    puts("NEXUS GPU DISCOVERY AND FALLBACK TESTS PASSED");
    return 0;
}
