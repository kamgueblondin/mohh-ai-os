; Déclare que nous utilisons la syntaxe Intel et le mode 32-bit
bits 32

; Section pour l'en-tête Multiboot (magic numbers et flags)
section .multiboot
align 4
    ; Flags for Multiboot header
    MBALIGN    equ 1<<0  ; Align loaded modules on page boundaries (0x01)
    MEMINFO    equ 1<<1  ; Provide memory map (0x02)
    MULTIBOOT_HEADER_FLAGS: equ MBALIGN | MEMINFO ; Request page alignment and memory map (0x03)

    MULTIBOOT_HEADER_MAGIC: equ 0x1BADB002
    CHECKSUM: equ -(MULTIBOOT_HEADER_MAGIC + MULTIBOOT_HEADER_FLAGS)

    dd MULTIBOOT_HEADER_MAGIC
    dd MULTIBOOT_HEADER_FLAGS
    dd CHECKSUM

; Section pour la pile (stack). Nous allouons 16KB.
section .bss
align 16
stack_bottom:
resb 16384 ; 16 KB
stack_top:

; Section pour le code exécutable
section .text
global _start: ; Point d'entrée global pour le linker

_start:
    ; Attempt to write 'S' to the top-left of VGA memory (0xB8000)
    ; Character 'S' in AL, Attribute Yellow (0E) on Black in AH
    mov word [0xB8000], 0x0E53 ; 0E for attribute, 53 for 'S'

    ; Mettre en place le pointeur de pile (esp) pour qu'il pointe vers le haut de notre pile
    mov esp, stack_top

    ; Nous sommes prêts à sauter dans notre code C.
    ; "extern" déclare que la fonction kmain est définie ailleurs (dans kernel.c)
    extern kmain
    call kmain

    ; Si kmain retourne (ce qui ne devrait pas arriver), on arrête le CPU pour éviter un crash.
    cli ; Désactive les interruptions
    hlt ; Arrête le CPU
