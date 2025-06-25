CC = gcc
LD = ld
CFLAGS = -m32 -ffreestanding -nostdlib -fno-pie -I.

# Compile le programme en un exécutable ELF 32-bit simple
test_program: test_program.c
	$(CC) $(CFLAGS) -c $< -o test_program.o
	$(LD) -m elf_i386 -o $@ -Ttext=0x40000000 test_program.o
