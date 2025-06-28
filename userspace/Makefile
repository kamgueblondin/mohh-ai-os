CC = gcc
LD = ld
CFLAGS = -m32 -ffreestanding -nostdlib -fno-pie -I.
# Output ELF files and specify 'main' as the entry point.
# Using a common base address for simple user programs.
LDFLAGS = -m elf_i386 -Ttext=0x00010000 --entry=main

# Cibles par défaut
all: shell.bin fake_ai.bin

# Compilation et liaison pour shell.c
shell.o: shell.c
	$(CC) $(CFLAGS) -c $< -o $@

shell.bin: shell.o
	$(LD) $(LDFLAGS) -o $@ $<

# Compilation et liaison pour fake_ai.c
fake_ai.o: fake_ai.c
	$(CC) $(CFLAGS) -c $< -o $@

fake_ai.bin: fake_ai.o
	$(LD) $(LDFLAGS) -o $@ $<

# Nettoyage
clean:
	rm -f *.o shell.bin fake_ai.bin

.PHONY: all clean
