CC = gcc
LD = ld
FASM = fasm
QEMU = qemu-system-i386
AR = ar
HOSTCC = gcc
CPPFLAGS = -Ilibc/include
LIBC_SOURCES = $(wildcard libc/*.c)
LIBC_OBJECTS = $(patsubst libc/%.c,build/libc-%.o,$(LIBC_SOURCES))
LIBC_HEADERS = $(wildcard libc/include/*.h)
HOSTFLAGS = -std=c11 -O2 -fno-builtin -Wall -Wextra -Werror
CFLAGS = -m32 -std=c11 -O2 -ffreestanding -fno-pie -fno-pic \
         -fno-stack-protector -fno-builtin -fno-asynchronous-unwind-tables \
         -Wall -Wextra -Werror

.PHONY: all run smoke check clean
all: build/cobalt.img

build:
	mkdir -p $@

build/boot.bin: boot/boot.asm | build
	$(FASM) $< $@

build/entry.o: boot/entry.asm | build
	$(FASM) $< $@

build/kernel.o: kernel/kernel.c games/games.h $(LIBC_HEADERS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

build/games.o: games/games.c games/games.h $(LIBC_HEADERS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

build/libc-%.o: libc/%.c $(LIBC_HEADERS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

build/libc.a: $(LIBC_OBJECTS)
	$(AR) rcs $@ $^

build/kernel.elf: build/entry.o build/kernel.o build/games.o build/libc.a kernel/linker.ld
	$(LD) -m elf_i386 -T kernel/linker.ld -o $@ build/entry.o build/kernel.o build/games.o build/libc.a

build/kernel.bin: build/kernel.elf
	objcopy -O binary $< $@

build/kernel-size.inc: build/kernel.bin
	@bytes=$$(wc -c < $<); sectors=$$(( (bytes + 511) / 512 )); \
	 test $$sectors -le 127 || { echo 'Kernel exceeds 65024-byte loader limit'; exit 1; }; \
	 printf 'KERNEL_SECTORS = %s\n' $$sectors > $@

build/loader.bin: boot/loader.asm build/kernel-size.inc
	INCLUDE="$(CURDIR)/build/" $(FASM) $< $@

build/cobalt.img: build/boot.bin build/loader.bin build/kernel.bin
	dd if=/dev/zero of=$@ bs=512 count=2880 status=none
	dd if=build/boot.bin of=$@ conv=notrunc status=none
	dd if=build/loader.bin of=$@ bs=512 seek=1 conv=notrunc status=none
	dd if=build/kernel.bin of=$@ bs=512 seek=9 conv=notrunc status=none

run: build/cobalt.img
	$(QEMU) -drive file=$<,format=raw,if=floppy -boot a -m 32 -vga std \
	 -debugcon file:build/debug.log -no-reboot

smoke: build/cobalt.img
	python3 scripts/smoke.py

build/libc-test: tests/libc_test.c $(LIBC_SOURCES) $(LIBC_HEADERS) | build
	$(HOSTCC) $(CPPFLAGS) $(HOSTFLAGS) tests/libc_test.c $(LIBC_SOURCES) -o $@

build/games-test: tests/games_test.c games/games.c games/games.h $(LIBC_SOURCES) $(LIBC_HEADERS) | build
	$(HOSTCC) $(CPPFLAGS) $(HOSTFLAGS) tests/games_test.c $(LIBC_SOURCES) -o $@

check: build/libc-test build/games-test build/cobalt.img
	./build/libc-test
	./build/games-test
	python3 scripts/smoke.py

clean:
	rm -rf build
