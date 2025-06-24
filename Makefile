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
OBJECTS = build/boot.o build/idt_loader.o build/isr_stubs.o build/paging.o \
          build/kernel.o build/idt.o build/interrupts.o build/keyboard.o \
          build/pmm.o build/vmm.o build/initrd.o

# Cible par défaut : construire l'image de l'OS
all: $(OS_IMAGE)

# Règle pour lier les fichiers objets et créer l'image finale
$(OS_IMAGE): $(OBJECTS)
	@mkdir -p $(dir $@)
	$(LD) -m elf_i386 -T linker.ld -o $@ $(OBJECTS) -nostdlib # Ensure no standard libraries are linked

# Règles de compilation pour les fichiers .c
# Règle générique pour les fichiers .c directement dans kernel/
build/%.o: kernel/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Dépendances spécifiques et règles pour les fichiers .c
# (La règle générique ci-dessus pourrait les gérer si les .h sont au même niveau ou via -I.)
# Mais pour être explicite :
build/kernel.o: kernel/kernel.c kernel/idt.h kernel/interrupts.h kernel/keyboard.h kernel/mem/pmm.h kernel/mem/vmm.h fs/initrd.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c kernel/kernel.c -o $@

build/idt.o: kernel/idt.c kernel/idt.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c kernel/idt.c -o $@

build/interrupts.o: kernel/interrupts.c kernel/interrupts.h kernel/idt.h kernel/keyboard.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c kernel/interrupts.c -o $@

build/keyboard.o: kernel/keyboard.c kernel/keyboard.h kernel/interrupts.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c kernel/keyboard.c -o $@

# Règles pour les fichiers dans kernel/mem/
build/pmm.o: kernel/mem/pmm.c kernel/mem/pmm.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c kernel/mem/pmm.c -o $@

build/vmm.o: kernel/mem/vmm.c kernel/mem/vmm.h kernel/mem/pmm.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c kernel/mem/vmm.c -o $@

# Règle pour les fichiers dans fs/
build/initrd.o: fs/initrd.c fs/initrd.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c fs/initrd.c -o $@

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

build/paging.o: boot/paging.s
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) $< -o $@

# Cible pour exécuter l'OS dans QEMU
run: $(OS_IMAGE)
	# Créer l'initrd s'il n'existe pas
	@if [ ! -f my_initrd.tar ]; then \
		echo "Creation de my_initrd.tar..."; \
		mkdir -p initrd_content; \
		echo "Ceci est un test depuis l'initrd !" > initrd_content/test.txt; \
		echo "Un autre fichier." > initrd_content/hello.txt; \
		tar -cf my_initrd.tar -C initrd_content .; \
		echo "my_initrd.tar cree."; \
	fi
	# Lancer QEMU avec le noyau ET l'initrd
	qemu-system-i386 -kernel $(OS_IMAGE) -initrd my_initrd.tar -display curses

# Cible pour nettoyer le projet
clean:
	rm -rf build my_initrd.tar initrd_content

.PHONY: all run clean
