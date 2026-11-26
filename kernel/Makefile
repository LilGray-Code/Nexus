CC ?= gcc
LD ?= ld
OUT := ../build
DEPS := include

CFLAGS := -std=gnu11 -Os -Wall -Wextra -Werror \
	-ffreestanding -fno-builtin -fno-stack-protector \
	-fno-pic -fno-pie -fno-asynchronous-unwind-tables \
	-fno-unwind-tables -fno-common -m64 -march=x86-64 \
	-mno-red-zone -mno-mmx -mno-sse -mno-sse2 -mgeneral-regs-only \
	-mcmodel=kernel -I$(DEPS) -Isrc -I../installer
NEXUS_TLS ?= 0
ifeq ($(NEXUS_TLS),1)
MBEDTLS_SOURCE ?= ../build/deps/mbedtls-4.1.2
MBEDTLS_BUILD ?= ../build/deps/mbedtls-kernel-build
CFLAGS += -I$(MBEDTLS_BUILD)/generated/include \
	-I$(MBEDTLS_SOURCE)/include \
	-I$(MBEDTLS_SOURCE)/tf-psa-crypto/include \
	-I$(MBEDTLS_BUILD)/tf-psa-crypto/include \
	-I$(MBEDTLS_SOURCE)/tf-psa-crypto/drivers/builtin/include
TLS_OBJECTS := $(OUT)/tls_heap.o $(OUT)/mbedtls_heap.o \
	$(OUT)/mbedtls_random.o $(OUT)/mbedtls_time.o \
	$(OUT)/tls_client.o $(OUT)/net_https.o $(OUT)/tls_libc.o \
	$(OUT)/ca_bundle.o $(OUT)/ca_bundle_api.o
TLS_LIBS := $(MBEDTLS_BUILD)/library/libmbedtls.a \
	$(MBEDTLS_BUILD)/library/libmbedx509.a \
	$(MBEDTLS_BUILD)/tf-psa-crypto/core/libtfpsacrypto.a
TLS_LINK_FLAGS := --start-group $(TLS_LIBS) --end-group $(shell $(CC) -print-libgcc-file-name)
else
TLS_OBJECTS := $(OUT)/net_https_stub.o $(OUT)/ca_bundle_stub.o
TLS_LINK_FLAGS :=
endif
CFLAGS += $(EXTRA_CFLAGS)
LDFLAGS := -nostdlib -static -z max-page-size=0x1000 -T linker/x86_64.ld
C_OBJECTS := $(OUT)/ac97.o $(OUT)/ac97_format.o $(OUT)/main.o $(OUT)/cpu.o $(OUT)/fpu.o $(OUT)/boot_mode.o $(OUT)/boot_payload.o $(OUT)/boot_pack.o $(OUT)/gdt.o $(OUT)/elf64.o $(OUT)/e1000.o $(OUT)/rtl8139.o $(OUT)/netdev.o $(OUT)/net.o $(OUT)/http.o $(OUT)/download.o $(OUT)/app_store.o $(OUT)/package.o $(OUT)/catalog.o $(OUT)/html_text.o $(OUT)/wpa2_psk.o $(OUT)/aes_ccm.o $(OUT)/wpa2_eapol.o $(OUT)/wpa2_ccmp.o $(OUT)/wpa2_handshake.o $(OUT)/sha256.o $(OUT)/random.o $(OUT)/rtc.o $(OUT)/file_list.o $(OUT)/wifi.o $(OUT)/wifi_pci.o $(OUT)/win_driver_inf.o $(OUT)/win_driver_pe.o $(OUT)/win_driver_package.o $(OUT)/win_driver_ndis.o $(OUT)/wifi_80211.o $(OUT)/browser_history.o $(OUT)/browser_url.o $(OUT)/browser_search.o $(OUT)/text_buffer.o $(OUT)/process.o $(OUT)/usermode.o $(OUT)/serial.o $(OUT)/libc.o $(OUT)/memory.o $(OUT)/acpi.o $(OUT)/firmware.o $(OUT)/pci_caps.o $(OUT)/pci.o $(OUT)/driver_manager.o $(OUT)/service_manager.o $(OUT)/gpu.o $(OUT)/virtio_pci.o $(OUT)/virtio_gpu.o $(OUT)/virtio_net.o $(OUT)/blockdev.o $(OUT)/gpt.o $(OUT)/fat32.o $(OUT)/nxfs.o $(OUT)/vfs.o $(OUT)/graphics.o $(OUT)/desktop.o $(OUT)/input.o $(OUT)/usb_hid.o $(OUT)/usb_msc.o $(OUT)/xhci.o $(OUT)/nvme.o $(OUT)/ahci.o $(OUT)/storage.o $(OUT)/ioapic.o $(OUT)/paging.o $(OUT)/interrupts.o $(OUT)/timer.o $(OUT)/scheduler.o $(OUT)/smp.o $(OUT)/partition_plan.o $(OUT)/gpt_image.o $(OUT)/installer_journal.o $(OUT)/gpt_install.o $(OUT)/installer_storage.o $(OUT)/installer_runtime.o $(OUT)/fat32_format.o $(OUT)/fat32_file.o $(OUT)/bios_install.o $(TLS_OBJECTS)
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

$(OUT)/ca_bundle.o: src/ca_bundle.S ../build/deps/ca-bundle/cacert.pem | $(OUT)
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
	$(LD) $(LDFLAGS) -o $@ $(OBJECTS) $(TLS_LINK_FLAGS)

clean:
	rm -f $(OBJECTS) $(OUT)/nexus-kernel.elf
