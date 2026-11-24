CC ?= gcc
LD ?= ld
STRIP ?= strip
OUT ?= ../build/userland

CFLAGS := -std=c11 -Os -Wall -Wextra -Werror \
	-ffreestanding -fno-builtin -fno-stack-protector \
	-fno-pic -fno-pie -fno-asynchronous-unwind-tables \
	-fno-unwind-tables -fno-common -m64 -march=x86-64 \
	-mno-red-zone -mno-mmx -mno-sse -mno-sse2 \
	-mgeneral-regs-only -mcmodel=small -Iinclude
ASFLAGS := -m64 -fno-pic -fno-pie

.PHONY: all clean inspect
all: $(OUT)/HELLO.ELF

$(OUT):
	mkdir -p $(OUT)

$(OUT)/crt0.o: src/crt0.S | $(OUT)
	$(CC) $(ASFLAGS) -c $< -o $@

$(OUT)/hello.o: src/hello.c include/nexus/syscall.h | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/HELLO.ELF: $(OUT)/crt0.o $(OUT)/hello.o linker.ld
	$(LD) -m elf_x86_64 -static -nostdlib -z max-page-size=0x100 \
		-s -T linker.ld -o $@ $(OUT)/crt0.o $(OUT)/hello.o
	$(STRIP) --strip-section-headers $@
	@test -s $@

inspect: $(OUT)/HELLO.ELF
	file $(OUT)/HELLO.ELF
	readelf -h $(OUT)/HELLO.ELF
	readelf -l $(OUT)/HELLO.ELF

clean:
	rm -rf $(OUT)
