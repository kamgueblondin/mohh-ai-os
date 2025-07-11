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
ISO_IMAGE = release/mohh-ai-os.iso

# Liste des fichiers objets du noyau
KERNEL_OBJECTS = build/boot.o build/idt_loader.o build/isr_stubs.o build/paging.o build/context_switch.o \
                 build/kernel.o build/idt.o build/interrupts.o build/keyboard.o \
                 build/pmm.o build/vmm.o build/libc.o \
                 build/task.o build/timer.o build/syscall.o build/elf.o build/syscall_handler.o

# Fichiers objets de l'espace utilisateur (convertis à partir des binaires)
USERSPACE_OBJECTS = build/userspace/shell_bin.o build/userspace/fake_ai_bin.o

# Tous les objets à lier
OBJECTS = $(KERNEL_OBJECTS) $(USERSPACE_OBJECTS)

# Cible par défaut : construire l'image de l'OS
all: $(OS_IMAGE)

# Règles pour les objets de l'espace utilisateur copiés
build/userspace/shell_bin.o: userspace/shell_bin.o
	@mkdir -p $(dir $@)
	@cp $< $@

build/userspace/fake_ai_bin.o: userspace/fake_ai_bin.o
	@mkdir -p $(dir $@)
	@cp $< $@

# S'assurer que les binaires de l'espace utilisateur sont construits avant de tenter de les copier
userspace/shell_bin.o: userspace_build
userspace/fake_ai_bin.o: userspace_build


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
build/kernel.o: kernel/kernel.c kernel/idt.h kernel/interrupts.h kernel/keyboard.h kernel/mem/pmm.h kernel/mem/vmm.h
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

# Cible pour exécuter l'OS dans QEMU
run: $(OS_IMAGE)
	# Lancer QEMU avec le noyau, avec affichage graphique, serial multiplexé, verbose debug, no network
	qemu-system-i386 -kernel $(OS_IMAGE) -serial mon:stdio -d int,cpu_reset,guest_errors -no-reboot -no-shutdown -net none

# Cible pour nettoyer le projet
clean:
	rm -rf build my_initrd.tar release iso_root
	$(MAKE) -C userspace clean
	# Nettoyer aussi les objets utilisateur copiés
	rm -f build/userspace/shell_bin.o build/userspace/fake_ai_bin.o


# Cible pour créer l'image ISO bootable
iso: $(OS_IMAGE)
	@echo "Création de l'image ISO bootable..."
	@mkdir -p iso_root/boot/grub
	@cp $(OS_IMAGE) iso_root/boot/os.bin
	@echo 'set timeout=0' > iso_root/boot/grub/grub.cfg
	@echo 'set default=0' >> iso_root/boot/grub/grub.cfg
	@echo '' >> iso_root/boot/grub/grub.cfg
	@echo 'menuentry "mohh-ai-os" {' >> iso_root/boot/grub/grub.cfg
	@echo '  multiboot /boot/os.bin' >> iso_root/boot/grub/grub.cfg
	@echo '}' >> iso_root/boot/grub/grub.cfg
	@mkdir -p $(dir $(ISO_IMAGE))
	grub-mkrescue -o $(ISO_IMAGE) iso_root -- -volid "MOHH_AI_OS"
	@echo "$(ISO_IMAGE) créé."

.PHONY: all run clean userspace_build initrd iso
