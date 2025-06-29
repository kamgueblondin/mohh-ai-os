CC = gcc
LD = ld
CFLAGS = -m32 -ffreestanding -nostdlib -fno-pie -I.
# Output ELF files and specify 'main' as the entry point.
# Using a common base address for simple user programs.
LDFLAGS = -m elf_i386 -Ttext=0x00010000 --entry=main

# Cibles par défaut
all: shell.bin fake_ai.bin shell_bin.o fake_ai_bin.o

# Compilation et liaison pour shell.c
shell.o: shell.c
	$(CC) $(CFLAGS) -c $< -o $@

shell.bin: shell.o
	$(LD) $(LDFLAGS) -o $@ $<
	@echo "Taille de shell.bin :"
	@ls -l $@

shell_bin.o: shell.bin
	objcopy -I binary -O elf32-i386 -B i386 --rename-section .data=.data.shell_bin shell.bin shell_bin.o

# Compilation et liaison pour fake_ai.c
fake_ai.o: fake_ai.c
	$(CC) $(CFLAGS) -c $< -o $@

fake_ai.bin: fake_ai.o
	$(LD) $(LDFLAGS) -o $@ $<
	@echo "Taille de fake_ai.bin :"
	@ls -l $@

fake_ai_bin.o: fake_ai.bin
	objcopy -I binary -O elf32-i386 -B i386 --rename-section .data=.data.fake_ai_bin fake_ai.bin fake_ai_bin.o

# Nettoyage
clean:
	rm -f *.o shell.bin fake_ai.bin shell_bin.o fake_ai_bin.o

.PHONY: all clean
