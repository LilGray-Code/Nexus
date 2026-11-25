CROSS_COMPILE ?= x86_64-elf-
CC := $(CROSS_COMPILE)gcc
OUT := build
APP := $(OUT)/kinetic_demo.elf
CFLAGS := -std=gnu11 -O2 -Wall -Wextra -Werror \
	-ffreestanding -fno-builtin -fno-stack-protector \
	-fno-pic -fno-pie -fno-asynchronous-unwind-tables \
	-fno-unwind-tables -fno-common -m64 -march=x86-64 \
	-mno-red-zone -mno-mmx -mno-sse -mno-sse2 -mgeneral-regs-only \
	-I../kernel/include
LDFLAGS := -nostdlib -static -no-pie -Wl,-e,_start \
	-Wl,-Ttext-segment=0x400000 -Wl,--build-id=none -Wl,-z,noexecstack

.PHONY: all clean
all: $(APP)

$(OUT):
	mkdir -p $(OUT)

$(APP): examples/kinetic_demo.c ../kernel/include/nexus/app.h | $(OUT)
	$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@

clean:
	rm -rf $(OUT)
