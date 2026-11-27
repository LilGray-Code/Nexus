CROSS_COMPILE ?= x86_64-elf-
CC := $(CROSS_COMPILE)gcc
OUT := build
APP := $(OUT)/kinetic_demo.elf
GAMEPAD_APP := $(OUT)/gamepad_demo.elf
AUDIO_APP := $(OUT)/audio_demo.elf
PIE_APP := $(OUT)/pie_demo.elf
ARCADE_APP := $(OUT)/breakout_game.elf
PIE_CFLAGS = $(filter-out -fno-pic -fno-pie,$(CFLAGS)) -fPIE
PIE_LDFLAGS := -nostdlib -static-pie -Wl,-e,_start -Wl,--build-id=none -Wl,-z,noexecstack -Wl,-z,max-page-size=0x1000
CFLAGS := -std=gnu11 -O2 -Wall -Wextra -Werror \
	-ffreestanding -fno-builtin -fno-stack-protector \
	-fno-pic -fno-pie -fno-asynchronous-unwind-tables \
	-fno-unwind-tables -fno-common -m64 -march=x86-64 \
	-mno-red-zone -mno-mmx -msse2 -mfpmath=sse -mno-avx -mno-avx2 -mno-fma \
	-I../kernel/include
LDFLAGS := -nostdlib -static -no-pie -Wl,-e,_start \
	-Wl,-Ttext-segment=0x400000 -Wl,--build-id=none -Wl,-z,noexecstack

.PHONY: all clean inspect
all: $(APP) $(GAMEPAD_APP) $(AUDIO_APP) $(PIE_APP) $(ARCADE_APP)

$(OUT):
	mkdir -p $(OUT)

$(APP): examples/kinetic_demo.c ../kernel/include/nexus/app.h | $(OUT)
	$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@

$(GAMEPAD_APP): examples/gamepad_demo.c ../kernel/include/nexus/app.h | $(OUT)
	$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@

$(AUDIO_APP): examples/audio_demo.c ../kernel/include/nexus/app.h | $(OUT)
	$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@

$(PIE_APP): examples/pie_demo.c ../kernel/include/nexus/app.h | $(OUT)
	$(CC) $(PIE_CFLAGS) $(PIE_LDFLAGS) $< -o $@

$(ARCADE_APP): examples/breakout_game.c ../kernel/include/nexus/app.h | $(OUT)
	$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@

inspect: all
	file $(APP) $(GAMEPAD_APP) $(AUDIO_APP) $(PIE_APP) $(ARCADE_APP)
	readelf -h $(APP)
	readelf -lW $(APP)
	readelf -h $(PIE_APP)
	readelf -dW $(PIE_APP)
	objdump -d $(APP) | head -n 80

clean:
	rm -rf $(OUT)
