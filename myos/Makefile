CFLAGS = -m32 -ffreestanding -fno-builtin -fno-tree-loop-distribute-patterns -fno-pie -fno-stack-protector -nostdlib -O2 -Wall -Wextra -Wno-misleading-indentation
all: myos.elf
boot.o: boot.s
	gcc -m32 -c boot.s -o boot.o
kernel.o: kernel.c
	gcc $(CFLAGS) -c kernel.c -o kernel.o
gui.o: gui.c font.h
	gcc $(CFLAGS) -c gui.c -o gui.o
myos.elf: boot.o kernel.o gui.o linker.ld
	gcc -m32 -T linker.ld -o myos.elf -ffreestanding -nostdlib -no-pie -Wl,-z,noexecstack -Wl,--no-warn-rwx-segments boot.o kernel.o gui.o
run: myos.elf
	qemu-system-i386 -kernel myos.elf -vga std -rtc base=localtime
clean:
	rm -f *.o myos.elf
