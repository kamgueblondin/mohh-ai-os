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
OBJECTS = build/boot.o build/idt_loader.o build/isr_stubs.o build/paging.o build/context_switch.o \
          build/kernel.o build/idt.o build/interrupts.o build/keyboard.o \
          build/pmm.o build/vmm.o build/initrd.o build/libc.o \
          build/task.o build/timer.o build/syscall.o build/elf.o build/syscall_handler.o

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

build/context_switch.o: boot/context_switch.s
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) $< -o $@

build/syscall_handler.o: kernel/syscall_handler.s
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) $< -o $@

# Règles pour les nouveaux fichiers C du noyau
build/task.o: kernel/task/task.c kernel/task/task.h kernel/mem/pmm.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c kernel/task/task.c -o $@

build/timer.o: kernel/timer.c kernel/timer.h kernel/interrupts.h kernel/task/task.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c kernel/timer.c -o $@

build/syscall.o: kernel/syscall/syscall.c kernel/syscall/syscall.h kernel/task/task.h kernel/keyboard.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c kernel/syscall/syscall.c -o $@

build/elf.o: kernel/elf.c kernel/elf.h kernel/mem/vmm.h kernel/mem/pmm.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c kernel/elf.c -o $@

# Cible pour construire les programmes de l'espace utilisateur
userspace_build:
	$(MAKE) -C userspace

# Cible pour créer l'initrd
initrd: userspace_build
	@echo "Creation de my_initrd.tar avec shell.bin et fake_ai.bin..."
	tar -cf my_initrd.tar -C userspace shell.bin fake_ai.bin
	@echo "my_initrd.tar cree."

# Cible pour exécuter l'OS dans QEMU
run: $(OS_IMAGE) initrd
	# Lancer QEMU avec le noyau ET l'initrd
	qemu-system-i386 -kernel $(OS_IMAGE) -initrd my_initrd.tar -nographic -monitor none -serial stdio

# Cible pour nettoyer le projet
clean:
	rm -rf build my_initrd.tar
	$(MAKE) -C userspace clean

.PHONY: all run clean userspace_build initrd
