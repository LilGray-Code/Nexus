CC ?= gcc
LD ?= ld
AR ?= ar
STRIP ?= strip
OUT ?= ../build/userland

CFLAGS := -std=c11 -Os -Wall -Wextra -Werror \
	-ffreestanding -fno-builtin -fno-stack-protector \
	-fno-pic -fno-pie -fno-asynchronous-unwind-tables \
	-fno-unwind-tables -fno-common -ffunction-sections -fdata-sections \
	-m64 -march=x86-64 -mno-red-zone -mno-mmx -mno-sse -mno-sse2 \
	-mgeneral-regs-only -mcmodel=small -Iinclude
ASFLAGS := -m64 -fno-pic -fno-pie

.PHONY: all clean inspect test
all: $(OUT)/libnexus.a $(OUT)/HELLO.ELF

$(OUT):
	mkdir -p $(OUT)

$(OUT)/crt0.o: src/crt0.S | $(OUT)
	$(CC) $(ASFLAGS) -c $< -o $@

$(OUT)/runtime.o: src/runtime.c include/nexus/runtime.h include/nexus/syscall.h | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/heap.o: src/heap.c include/nexus/runtime.h | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/libnexus.a: $(OUT)/runtime.o $(OUT)/heap.o
	$(AR) rcs $@ $^

$(OUT)/hello.o: src/hello.c include/nexus/syscall.h include/nexus/runtime.h | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/HELLO.ELF: $(OUT)/crt0.o $(OUT)/hello.o $(OUT)/libnexus.a linker.ld
	$(LD) -m elf_x86_64 -static -nostdlib -z max-page-size=0x100 \
		-s --gc-sections -T linker.ld -o $@ \
		$(OUT)/crt0.o $(OUT)/hello.o $(OUT)/libnexus.a
	$(STRIP) --strip-section-headers $@
	@test -s $@

inspect: $(OUT)/libnexus.a $(OUT)/HELLO.ELF
	file $(OUT)/libnexus.a $(OUT)/HELLO.ELF
	readelf -h $(OUT)/HELLO.ELF
	readelf -l $(OUT)/HELLO.ELF

test:
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror -pedantic \
		-DNEXUS_USERLAND_HOST_TEST -Iinclude \
		src/runtime.c src/heap.c tests/runtime_test.c -o /tmp/nexus-runtime-test
	/tmp/nexus-runtime-test

clean:
	rm -rf $(OUT) /tmp/nexus-runtime-test
