CC=gcc
CFLAGS=-m32 -ffreestanding -fno-pic -fno-pie -fno-stack-protector -fno-builtin -fno-tree-loop-distribute-patterns \
 -mno-sse -mno-mmx -mfpmath=387 -O2 -Wall -Wextra -Wno-unused-parameter -Wno-misleading-indentation -std=gnu11
OBJS=kernel.o gfx.o wm.o app_term.o app_edit.o app_misc.o

all: NovaClaudeOS.iso

%.o: %.c os.h font.h
	$(CC) $(CFLAGS) -c $< -o $@

# the Multiboot header requests the screen size, so build one boot stub per resolution
boot_1024.o: boot.s
	$(CC) -m32 -c boot.s -o $@ -Wa,--defsym,MB_W=1024 -Wa,--defsym,MB_H=768
boot_1920.o: boot.s
	$(CC) -m32 -c boot.s -o $@ -Wa,--defsym,MB_W=1920 -Wa,--defsym,MB_H=1080
boot_2560.o: boot.s
	$(CC) -m32 -c boot.s -o $@ -Wa,--defsym,MB_W=2560 -Wa,--defsym,MB_H=1440

kernel-%.bin: boot_%.o $(OBJS) linker.ld
	ld -m elf_i386 -T linker.ld -nostdlib -o $@ boot_$*.o $(OBJS)

NovaClaudeOS.iso: kernel-1024.bin kernel-1920.bin kernel-2560.bin grub.cfg
	rm -rf iso && mkdir -p iso/boot/grub
	cp kernel-1024.bin kernel-1920.bin kernel-2560.bin iso/boot/
	cp grub.cfg iso/boot/grub/grub.cfg
	grub-mkrescue -o NovaClaudeOS.iso iso 2>&1 | tail -2
clean:
	rm -rf *.o *.bin iso NovaClaudeOS.iso
