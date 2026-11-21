#include <assert.h>
#include <stdint.h>
#include <limine.h>
#include "../src/boot_payload.h"

int main(void) {
    uint8_t kernel[64] = {0x7f,'E','L','F',2,1};
    uint8_t uefi[512] = {'M','Z'};
    uint8_t bios_hdd[512] = {0};
    uint8_t bios_sys[512] = {0};
    bios_hdd[510]=0x55; bios_hdd[511]=0xAA;
    uefi[0x3c]=0x40; uefi[0x40]='P'; uefi[0x41]='E';
    struct limine_file k={.address=kernel,.size=sizeof(kernel)};
    struct limine_file e={.address=uefi,.size=sizeof(uefi),.path="boot():/EFI/BOOT/BOOTX64.EFI"};
    struct limine_file b={.address=bios_hdd,.size=sizeof(bios_hdd),.path="boot():/boot/limine/limine-bios-hdd.bin"};
    struct limine_file bs={.address=bios_sys,.size=sizeof(bios_sys),.path="boot():/boot/limine/limine-bios.sys"};
    struct limine_file *items[3]={&e,&b,&bs};
    struct limine_module_response mr={.module_count=3,.modules=items};
    struct limine_executable_file_response er={.executable_file=&k};
    assert(nexus_boot_payloads_init(&mr,&er));
    const struct nexus_boot_payloads *p=nexus_boot_payloads_get();
    assert(p && p->kernel_elf.data==kernel && p->uefi_loader.data==uefi && p->bios_hdd_image.data==bios_hdd && p->bios_sys.data==bios_sys);
    e.path="wrong/path";
    assert(!nexus_boot_payloads_init(&mr,&er));
    assert(nexus_boot_payloads_get()==0);
    return 0;
}
