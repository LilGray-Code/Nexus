CC ?= gcc
LD ?= ld
OUT := ../build
DEPS := ../deps/limine-protocol/include

CFLAGS := -std=gnu11 -O2 -Wall -Wextra -Werror \
	-ffreestanding -fno-builtin -fno-stack-protector \
	-fno-pic -fno-pie -fno-asynchronous-unwind-tables \
	-fno-unwind-tables -fno-common -m64 -march=x86-64 \
	-mno-red-zone -mno-mmx -mno-sse -mno-sse2 \
	-mcmodel=kernel -I$(DEPS)
LDFLAGS := -nostdlib -static -z max-page-size=0x1000 -T linker/x86_64.ld

.PHONY: all clean
all: $(OUT)/nexus-kernel.elf
$(OUT):
	mkdir -p $(OUT)
$(OUT)/main.o: src/main.c | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@
$(OUT)/nexus-kernel.elf: $(OUT)/main.o linker/x86_64.ld
	$(LD) $(LDFLAGS) -o $@ $(OUT)/main.o
clean:
	rm -f $(OUT)/main.o $(OUT)/nexus-kernel.elf
