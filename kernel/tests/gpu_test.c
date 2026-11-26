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

bool pci_decode_bar(const struct nexus_pci_device *device, uint8_t index,
                    struct nexus_pci_bar *bar) {
    if (device == NULL || bar == NULL || index >= NEXUS_PCI_BAR_COUNT)
        return false;
    *bar = (struct nexus_pci_bar){0};
    for (uint8_t i = 0U; i < index; ++i) {
        const uint32_t previous = device->bars[i];
        if (previous != 0U && (previous & 1U) == 0U &&
            ((previous >> 1) & 3U) == 2U) {
            if ((uint8_t)(i + 1U) == index) return false;
            ++i;
        }
    }
    const uint32_t raw = device->bars[index];
    if (raw == 0U || raw == UINT32_MAX) return false;
    bar->present = true;
    if ((raw & 1U) != 0U) {
        bar->io_space = true;
        bar->base = (uint64_t)(raw & ~3U);
        bar->assigned = bar->base != 0U;
        return true;
    }
    const uint32_t memory_type = (raw >> 1) & 3U;
    if (memory_type == 3U) return false;
    bar->prefetchable = (raw & 8U) != 0U;
    if (memory_type == 2U) {
        if (index + 1U >= NEXUS_PCI_BAR_COUNT) return false;
        bar->is_64bit = true;
        bar->base = ((uint64_t)device->bars[index + 1U] << 32) |
                    (uint64_t)(raw & ~0xFU);
    } else {
        bar->base = (uint64_t)(raw & ~0xFU);
    }
    bar->assigned = bar->base != 0U;
    return true;
}

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

static void test_intel_arc_generation_and_bar_inventory(void) {
    devices[0] = (struct nexus_pci_device){
        .class_code = 0x03U, .subclass = 0x00U,
        .vendor_id = 0x8086U, .device_id = 0x56A0U,
        .bars = { 0x8000000CU, 0x00000012U, 0U, 0U, 0U, 0U }
    };
    device_count = 1U;
    assert(gpu_init());
    const struct nexus_gpu_info *info = gpu_primary_info();
    assert(info->family == NEXUS_GPU_FAMILY_INTEL_ARC_ALCHEMIST);
    assert(strcmp(info->family_name, "Intel Arc Alchemist (Xe-HPG)") == 0);
    assert(info->bar_count == 1U);
    assert(info->memory_bar_count == 1U);
    assert(info->has_64bit_memory_bar);
    assert(info->has_prefetchable_memory_bar);
    assert(info->bars[0].present && info->bars[0].assigned);
    assert(info->bars[0].is_64bit && info->bars[0].prefetchable);
    assert(info->bars[0].base == 0x0000001280000000ULL);
    assert(!info->hardware_2d_acceleration);
    assert(!info->hardware_3d_acceleration);

    devices[0].device_id = 0xE20BU; /* Arc B580 / Battlemage. */
    assert(gpu_init());
    assert(gpu_primary_info()->family ==
           NEXUS_GPU_FAMILY_INTEL_ARC_BATTLEMAGE);

    devices[0].device_id = 0xB080U; /* Panther Lake / Xe3. */
    assert(gpu_init());
    assert(gpu_primary_info()->family == NEXUS_GPU_FAMILY_INTEL_XE3);

    devices[0].device_id = 0x1234U;
    assert(gpu_init());
    assert(gpu_primary_info()->family == NEXUS_GPU_FAMILY_INTEL_GENERIC);
}

static void test_same_bdf_on_different_pci_segments(void) {
    devices[0] = (struct nexus_pci_device){
        .segment_group = 0U, .class_code = 0x03U, .subclass = 0x80U,
        .bus = 0U, .slot = 2U, .function = 0U,
        .vendor_id = 0x10DEU, .device_id = 0x2000U
    };
    devices[1] = (struct nexus_pci_device){
        .segment_group = 1U, .class_code = 0x03U, .subclass = 0x00U,
        .bus = 0U, .slot = 2U, .function = 0U,
        .vendor_id = 0x8086U, .device_id = 0x3000U,
        .standard_capability_flags = NEXUS_PCI_CAP_PCIE | NEXUS_PCI_CAP_MSIX,
        .extended_capabilities_valid = true,
        .extended_capability_flags = NEXUS_PCIE_EXT_AER
    };
    device_count = 2U;
    assert(gpu_init());
    assert(gpu_adapter_count() == 2U);
    assert(gpu_primary_info()->segment_group == 1U);
    assert(gpu_primary_info()->vendor == NEXUS_GPU_VENDOR_INTEL);
    assert(gpu_primary_info()->pcie_capabilities_valid);
    assert((gpu_primary_info()->standard_capability_flags &
            NEXUS_PCI_CAP_MSIX) != 0U);
    assert((gpu_primary_info()->extended_capability_flags &
            NEXUS_PCIE_EXT_AER) != 0U);
}

int main(void) {
    test_intel_adapter_and_cpu_fallback();
    test_same_bdf_on_different_pci_segments();
    test_intel_arc_generation_and_bar_inventory();
    test_nvidia_and_virtio_adapter_names();
    puts("NEXUS GPU DISCOVERY AND FALLBACK TESTS PASSED");
    return 0;
}
