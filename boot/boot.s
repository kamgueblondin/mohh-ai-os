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

; Statically allocated page directory and page tables for initial identity mapping.
; These must be page-aligned (4096 bytes) and are uninitialized (will be zeroed by code).
align 4096
boot_page_directory:
    resb 4096
boot_page_table1: ; Maps 0MB - 4MB
    resb 4096
boot_page_table2: ; Maps 4MB - 8MB
    resb 4096

; Section pour le code exécutable
section .text
global _start: ; Point d'entrée global pour le linker

_start:
    ; Attempt to write 'S' to the top-left of VGA memory (0xB8000)
    mov word [0xB8000], 0x0E53 ; Attribute 0E (Yellow/Black), Char 'S' (0x53)

    ; Set up our own stack
    mov esp, stack_top

    ; Initialize our temporary page directory and page tables to identity map the first 8MB.
    ; Page Directory Entry flags: Present=1, Read/Write=1, Supervisor=0 (0x03)
    ; Page Table Entry flags:   Present=1, Read/Write=1, Supervisor=0 (0x03)

    ; 1. Zero out page directory and page tables
    mov edi, boot_page_directory
    mov ecx, 1024 * 3 ; 3 pages (PD + PT1 + PT2) of 1024 DWORDs each
    xor eax, eax
    rep stosd    ; Zero out all three pages

    ; 2. Map first 4MB using boot_page_table1 (covers 0x00000000 - 0x003FFFFF)
    mov edi, boot_page_table1
    mov ecx, 0          ; Page frame number for this PT (0-1023)
.map_pt1_loop:
    mov eax, ecx        ; eax = virtual page number (0, 1, ..., 1023 for this PT)
    shl eax, 12         ; eax = physical base address of the page (0x0, 0x1000, ...)
    or eax, 0x03        ; Add flags: Present, R/W, Supervisor
    mov [edi + ecx*4], eax ; Set the PTE
    inc ecx
    cmp ecx, 1024
    jl .map_pt1_loop

    ; 3. Map next 4MB using boot_page_table2 (covers 0x00400000 - 0x007FFFFF)
    mov edi, boot_page_table2
    mov ecx, 0          ; Local page frame counter for this PT (0-1023)
    mov esi, 1024       ; Global page frame number offset (starts at page 1024, which is 4MB)
.map_pt2_loop:
    mov eax, esi        ; eax = global page_frame_number (1024, 1025, ...)
    shl eax, 12         ; eax = physical base address of the page (0x400000, 0x401000, ...)
    or eax, 0x03        ; Add flags: Present, R/W, Supervisor
    mov [edi + ecx*4], eax ; Set the PTE
    inc ecx
    inc esi
    cmp ecx, 1024
    jl .map_pt2_loop

    ; 4. Set entries in the page directory
    ; PDE[0] points to boot_page_table1
    mov eax, boot_page_table1
    or eax, 0x03        ; Add flags: Present, R/W, Supervisor
    mov [boot_page_directory], eax

    ; PDE[1] points to boot_page_table2
    mov eax, boot_page_table2
    or eax, 0x03        ; Add flags: Present, R/W, Supervisor
    mov [boot_page_directory + 4], eax ; Offset 4 for PDE[1]

    ; 5. Load our page directory into CR3
    mov eax, boot_page_directory
    mov cr3, eax
    mov ebx, eax ; Save physical address of boot_page_directory in EBX for kmain

    ; 6. Ensure paging is enabled in CR0 (it might already be set by QEMU/SeaBIOS)
    mov eax, cr0
    or eax, 0x80000000  ; Set PG bit (bit 31)
    mov cr0, eax

    jmp short .paging_setup_done ; Flush pipeline
.paging_setup_done:

    ; Now safe to call kmain
    ; Pass physical address of our page directory (in EBX) to kmain as first argument
    push ebx
    extern kmain
    call kmain
    add esp, 4 ; Clean up argument from stack

    ; If kmain returns (it shouldn't), halt.
    cli
    hlt
