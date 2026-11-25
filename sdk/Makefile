CROSS_COMPILE ?= x86_64-elf-
CC := $(CROSS_COMPILE)gcc
OUT := build
APP := $(OUT)/kinetic_demo.elf
GAMEPAD_APP := $(OUT)/gamepad_demo.elf
AUDIO_APP := $(OUT)/audio_demo.elf
CFLAGS := -std=gnu11 -O2 -Wall -Wextra -Werror \
	-ffreestanding -fno-builtin -fno-stack-protector \
	-fno-pic -fno-pie -fno-asynchronous-unwind-tables \
	-fno-unwind-tables -fno-common -m64 -march=x86-64 \
	-mno-red-zone -mno-mmx -msse2 -mfpmath=sse -mno-avx -mno-avx2 -mno-fma \
	-I../kernel/include
LDFLAGS := -nostdlib -static -no-pie -Wl,-e,_start \
	-Wl,-Ttext-segment=0x400000 -Wl,--build-id=none -Wl,-z,noexecstack

.PHONY: all clean
all: $(APP) $(GAMEPAD_APP) $(AUDIO_APP)

$(OUT):
	mkdir -p $(OUT)

$(APP): examples/kinetic_demo.c ../kernel/include/nexus/app.h | $(OUT)
	$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@

$(GAMEPAD_APP): examples/gamepad_demo.c ../kernel/include/nexus/app.h | $(OUT)
	$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@

$(AUDIO_APP): examples/audio_demo.c ../kernel/include/nexus/app.h | $(OUT)
	$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@

clean:
	rm -rf $(OUT)
