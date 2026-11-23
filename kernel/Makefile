CC ?= gcc
LD ?= ld
OUT := ../build
DEPS := include

CFLAGS := -std=gnu11 -O2 -Wall -Wextra -Werror \
	-ffreestanding -fno-builtin -fno-stack-protector \
	-fno-pic -fno-pie -fno-asynchronous-unwind-tables \
	-fno-unwind-tables -fno-common -m64 -march=x86-64 \
	-mno-red-zone -mno-mmx -mno-sse -mno-sse2 -mgeneral-regs-only \
	-mcmodel=kernel -I$(DEPS) -Isrc -I../installer
CFLAGS += $(EXTRA_CFLAGS)
LDFLAGS := -nostdlib -static -z max-page-size=0x1000 -T linker/x86_64.ld
C_OBJECTS := $(OUT)/main.o $(OUT)/boot_payload.o $(OUT)/gdt.o $(OUT)/elf64.o $(OUT)/elf64_dynamic.o $(OUT)/e1000.o $(OUT)/netdev.o $(OUT)/wifi.o $(OUT)/wifi_80211.o $(OUT)/net.o $(OUT)/browser_url.o $(OUT)/browser_html.o $(OUT)/calculator.o $(OUT)/sha256.o $(OUT)/process.o $(OUT)/usermode.o $(OUT)/serial.o $(OUT)/memory.o $(OUT)/acpi.o $(OUT)/pci.o $(OUT)/pci_match.o $(OUT)/pci_bar.o $(OUT)/blockdev.o $(OUT)/gpt.o $(OUT)/fat32.o $(OUT)/nxfs.o $(OUT)/vfs.o $(OUT)/graphics.o $(OUT)/desktop.o $(OUT)/window_manager.o $(OUT)/input.o $(OUT)/usb_hid.o $(OUT)/usb_hid_gamepad.o $(OUT)/usb_desc.o $(OUT)/usb_xhci.o $(OUT)/xhci.o $(OUT)/nvme.o $(OUT)/ahci.o $(OUT)/storage.o $(OUT)/ioapic.o $(OUT)/paging.o $(OUT)/interrupts.o $(OUT)/timer.o $(OUT)/scheduler.o $(OUT)/partition_plan.o $(OUT)/gpt_image.o $(OUT)/installer_journal.o $(OUT)/gpt_install.o $(OUT)/installer_storage.o $(OUT)/installer_runtime.o $(OUT)/fat32_format.o $(OUT)/fat32_file.o $(OUT)/bios_install.o
OBJECTS := $(C_OBJECTS) $(OUT)/isr.o $(OUT)/context.o $(OUT)/gdt_asm.o $(OUT)/usermode_asm.o

.PHONY: all clean
all: $(OUT)/nexus-kernel.elf

$(OUT):
	mkdir -p $(OUT)

$(OUT)/%.o: src/%.c | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/partition_plan.o: ../installer/partition_plan.c ../installer/partition_plan.h | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/gpt_image.o: ../installer/gpt_image.c ../installer/gpt_image.h | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/fat32_format.o: ../installer/fat32_format.c ../installer/fat32_format.h | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/fat32_file.o: ../installer/fat32_file.c ../installer/fat32_file.h ../installer/fat32_format.h | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/gpt_install.o: ../installer/gpt_install.c ../installer/gpt_install.h | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/installer_journal.o: ../installer/installer_journal.c ../installer/installer_journal.h | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/bios_install.o: ../installer/bios_install.c ../installer/bios_install.h | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/isr.o: src/isr.S | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/context.o: src/context.S | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/gdt_asm.o: src/gdt.S | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/usermode_asm.o: src/usermode.S | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/nexus-kernel.elf: $(OBJECTS) linker/x86_64.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJECTS)

clean:
	rm -f $(OBJECTS) $(OUT)/nexus-kernel.elf
