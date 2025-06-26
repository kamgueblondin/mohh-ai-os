CC = gcc
LD = ld
CFLAGS = -m32 -ffreestanding -nostdlib -fno-pie -I.
LDFLAGS = -m elf_i386 -Ttext=0x40000000 --oformat binary

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
