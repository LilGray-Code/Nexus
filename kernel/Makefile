CC ?= gcc
LD ?= ld
OUT := ../build
DEPS := ../deps/limine-protocol/include

CFLAGS := -std=gnu11 -O2 -Wall -Wextra -Werror \
	-ffreestanding -fno-builtin -fno-stack-protector \
	-fno-pic -fno-pie -fno-asynchronous-unwind-tables \
	-fno-unwind-tables -fno-common -m64 -march=x86-64 \
	-mno-red-zone -mno-mmx -mno-sse -mno-sse2 -mgeneral-regs-only \
	-mcmodel=kernel -I$(DEPS) -Isrc
CFLAGS += $(EXTRA_CFLAGS)
LDFLAGS := -nostdlib -static -z max-page-size=0x1000 -T linker/x86_64.ld
C_OBJECTS := $(OUT)/main.o $(OUT)/serial.o $(OUT)/memory.o $(OUT)/acpi.o $(OUT)/pci.o $(OUT)/blockdev.o $(OUT)/graphics.o $(OUT)/desktop.o $(OUT)/input.o $(OUT)/nvme.o $(OUT)/storage.o $(OUT)/ioapic.o $(OUT)/paging.o $(OUT)/interrupts.o $(OUT)/timer.o $(OUT)/scheduler.o
OBJECTS := $(C_OBJECTS) $(OUT)/isr.o $(OUT)/context.o

.PHONY: all clean
all: $(OUT)/nexus-kernel.elf

$(OUT):
	mkdir -p $(OUT)

$(OUT)/%.o: src/%.c | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/isr.o: src/isr.S | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/context.o: src/context.S | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/nexus-kernel.elf: $(OBJECTS) linker/x86_64.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJECTS)

clean:
	rm -f $(OBJECTS) $(OUT)/nexus-kernel.elf
