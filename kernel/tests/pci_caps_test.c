#include "pci_caps.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
struct config_image { uint32_t dwords[1024]; bool fail_reads; };
static bool read_config(void *context, uint16_t offset, uint32_t *value) {
    struct config_image *image = context;
    if (image == NULL || value == NULL || (offset & 3U) != 0U ||
        offset > 0xFFCU || image->fail_reads) return false;
    *value = image->dwords[offset / 4U]; return true;
}
static uint32_t std_header(uint8_t id, uint8_t next) {
    return (uint32_t)id | ((uint32_t)next << 8);
}
static uint32_t ext_header(uint16_t id, uint8_t version, uint16_t next) {
    return (uint32_t)id | ((uint32_t)(version & 0x0FU) << 16) |
           ((uint32_t)(next & 0x0FFFU) << 20);
}
static void test_capabilities(void) {
    struct config_image image = {0};
    image.dwords[0x04U/4U] = 1U << 20;
    image.dwords[0x34U/4U] = 0x40U;
    image.dwords[0x40U/4U] = std_header(0x01U,0x48U);
    image.dwords[0x48U/4U] = std_header(0x05U,0x50U);
    image.dwords[0x50U/4U] = std_header(0x10U,0x58U);
    image.dwords[0x58U/4U] = std_header(0x11U,0U);
    image.dwords[0x100U/4U] = ext_header(0x0001U,1U,0x120U);
    image.dwords[0x120U/4U] = ext_header(0x000DU,1U,0x140U);
    image.dwords[0x140U/4U] = ext_header(0x0010U,1U,0x160U);
    image.dwords[0x160U/4U] = ext_header(0x001BU,1U,0U);
    struct nexus_pci_capability_summary s;
    assert(nexus_pci_scan_capabilities(read_config,&image,0U,true,&s));
    assert(s.standard_valid && s.extended_valid && s.extended_supported);
    assert(s.standard_count==4U && s.extended_count==4U);
    assert((s.standard_flags & (NEXUS_PCI_CAP_POWER_MANAGEMENT|NEXUS_PCI_CAP_MSI|
       NEXUS_PCI_CAP_PCIE|NEXUS_PCI_CAP_MSIX)) ==
       (NEXUS_PCI_CAP_POWER_MANAGEMENT|NEXUS_PCI_CAP_MSI|
        NEXUS_PCI_CAP_PCIE|NEXUS_PCI_CAP_MSIX));
    assert((s.extended_flags & (NEXUS_PCIE_EXT_AER|NEXUS_PCIE_EXT_ACS|
       NEXUS_PCIE_EXT_SRIOV|NEXUS_PCIE_EXT_PASID)) ==
       (NEXUS_PCIE_EXT_AER|NEXUS_PCIE_EXT_ACS|
        NEXUS_PCIE_EXT_SRIOV|NEXUS_PCIE_EXT_PASID));
}
static void test_absent_and_cardbus(void) {
    struct config_image image={0}; struct nexus_pci_capability_summary s;
    assert(nexus_pci_scan_capabilities(read_config,&image,0U,false,&s));
    assert(s.standard_valid && s.standard_count==0U);
    assert(!s.extended_supported && !s.extended_valid);
    memset(&image,0,sizeof(image));
    image.dwords[0x04U/4U]=1U<<20;
    image.dwords[0x14U/4U]=0x40U;
    image.dwords[0x40U/4U]=std_header(0x07U,0U);
    assert(nexus_pci_scan_capabilities(read_config,&image,2U,false,&s));
    assert(s.standard_count==1U && (s.standard_flags & NEXUS_PCI_CAP_PCIX));
}
static void test_malformed_cycles_and_read_failure(void) {
    struct nexus_pci_capability_summary s; struct config_image image={0};
    image.dwords[0x04U/4U]=1U<<20; image.dwords[0x34U/4U]=0x41U;
    assert(!nexus_pci_scan_capabilities(read_config,&image,0U,false,&s));
    assert(!s.standard_valid);
    memset(&image,0,sizeof(image)); image.dwords[0x04U/4U]=1U<<20;
    image.dwords[0x34U/4U]=0x40U; image.dwords[0x40U/4U]=std_header(5U,0x40U);
    assert(!nexus_pci_scan_capabilities(read_config,&image,0U,false,&s));
    assert(!s.standard_valid && s.standard_count==1U);
    memset(&image,0,sizeof(image));
    image.dwords[0x100U/4U]=ext_header(1U,1U,0x100U);
    assert(!nexus_pci_scan_capabilities(read_config,&image,0U,true,&s));
    assert(!s.extended_valid && s.extended_count==1U);
    image.fail_reads=true;
    assert(!nexus_pci_scan_capabilities(read_config,&image,0U,true,&s));
    assert(!s.standard_valid && !s.extended_valid);
}
int main(void) {
    test_capabilities(); test_absent_and_cardbus();
    test_malformed_cycles_and_read_failure();
    puts("NEXUS PCI CAPABILITY TESTS PASSED"); return 0;
}
