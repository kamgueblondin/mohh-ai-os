# Outils de compilation
CC = gcc
AS = nasm
LD = ld

# Options de compilation
# -m32 : Compiler en 32-bit
# -ffreestanding : Ne pas utiliser la bibliothèque standard C
# -nostdlib : Ne pas lier avec la bibliothèque standard C
# -fno-pie : Produire du code indépendant de la position
CFLAGS = -m32 -ffreestanding -nostdlib -fno-pie -Wall -Wextra -I.
ASFLAGS = -f elf32

# Nom du fichier final de notre OS
OS_IMAGE = build/ai_os.bin
# ISO_IMAGE = build/ai_os.iso # ISO not used yet

# Liste des fichiers objets
OBJECTS = build/boot.o build/idt_loader.o build/isr_stubs.o build/kernel.o build/idt.o build/interrupts.o build/keyboard.o

# Cible par défaut : construire l'image de l'OS
all: $(OS_IMAGE)

# Règle pour lier les fichiers objets et créer l'image finale
$(OS_IMAGE): $(OBJECTS)
	@mkdir -p $(dir $@)
	$(LD) -m elf_i386 -T linker.ld -o $@ $(OBJECTS) -nostdlib # Ensure no standard libraries are linked

# Règles de compilation pour les fichiers .c
# Common rule for .c files:
# $< is the first prerequisite (the .c file)
# $@ is the target (the .o file)
build/%.o: kernel/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Specific dependencies for .c files if they include other project headers
build/kernel.o: kernel/kernel.c kernel/idt.h kernel/interrupts.h kernel/keyboard.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

build/idt.o: kernel/idt.c kernel/idt.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

build/interrupts.o: kernel/interrupts.c kernel/interrupts.h kernel/idt.h kernel/keyboard.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

build/keyboard.o: kernel/keyboard.c kernel/keyboard.h kernel/interrupts.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Règles de compilation pour les fichiers assembleur .s
build/boot.o: boot/boot.s
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) $< -o $@

build/idt_loader.o: boot/idt_loader.s
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) $< -o $@

build/isr_stubs.o: boot/isr_stubs.s
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) $< -o $@

# Cible pour exécuter l'OS dans QEMU
run: $(OS_IMAGE)
	qemu-system-i386 -kernel $(OS_IMAGE) -nographic

# Cible pour nettoyer le projet
clean:
	rm -rf build

.PHONY: all run clean
