bits 32

%macro ISR_NOERRCODE 1  ; Macro for ISRs without error codes
global isr%1
isr%1:
    cli                ; Disable interrupts
    push byte 0        ; Push a dummy error code
    push byte %1       ; Push the interrupt number
    jmp isr_common_stub
%endmacro

%macro ISR_ERRCODE 1   ; Macro for ISRs with error codes
global isr%1
isr%1:
    cli                ; Disable interrupts
    ; Error code is already on the stack
    push byte %1       ; Push the interrupt number
    jmp isr_common_stub
%endmacro

%macro IRQ 2           ; Macro for IRQs
global irq%1
irq%1:
    cli                ; Disable interrupts
    push byte 0        ; Dummy error code
    push byte %2       ; Interrupt number (IRQ number + 32)
    jmp irq_common_stub
%endmacro

; Common stub for ISRs (CPU exceptions)
isr_common_stub:
    pusha              ; Pushes edi,esi,ebp,esp,ebx,edx,ecx,eax
    mov ax, ds         ; Lower 16-bits of eax = ds.
    push eax           ; save the data segment descriptor
    mov ax, 0x10       ; load the kernel data segment descriptor
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    call fault_handler ; Call C handler

    pop ebx            ; reload the original data segment descriptor
    mov ds, bx
    mov es, bx
    mov fs, bx
    mov gs, bx
    popa               ; Pops edi,esi,ebp...
    add esp, 8         ; Cleans up the pushed error code and pushed ISR number
    iret               ; pops 5 things at once: CS, EIP, EFLAGS, SS, ESP

; Common stub for IRQs (Hardware interrupts)
irq_common_stub:
    pusha
    mov ax, ds
    push eax
    mov ax, 0x10       ; Kernel data segment
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; --- Début du code de débogage ---
    ; Sauvegarder eax et edx temporairement s'ils sont utilisés par la suite avant popa
    push eax
    push edx
    mov edx, 0xB8000   ; Adresse de base de la mémoire VGA
    mov ah, 0x0A       ; Couleur (Vert sur Noir) pour le débogage
    mov al, 'S'        ; Caractère 'S' (pour Stub)
    mov [edx + (0 * 80 + 76) * 2], ax ; Écrire 'S' en (x=76, y=0)
    pop edx
    pop eax
    ; --- Fin du code de débogage ---

    call irq_handler_c ; Call C common IRQ handler

    pop ebx
    mov ds, bx
    mov es, bx
    mov fs, bx
    mov gs, bx
    popa
    add esp, 8         ; Clean up error code and interrupt number
    iret

; ISRs for CPU exceptions
; ISR_NOERRCODE 0   ; Divide by zero (Ancienne version)

; Nouveau stub simplifié pour ISR0 (Divide by Zero)
extern minimal_int0_handler_c
global isr0
isr0:
    cli
    ; Le CPU a déjà poussé EFLAGS, CS, EIP.
    ; Pas de code d'erreur pour l'INT 0.
    ; Nous n'allons pas pousser de numéro d'interruption ou de dummy error code pour ce test.

    ; Sauvegarder les registres caller-saved que minimal_int0_handler_c pourrait utiliser (EAX, ECX, EDX)
    push eax
    push ecx
    push edx

    call minimal_int0_handler_c ; Appeler directement le handler C minimal

    pop edx
    pop ecx
    pop eax

    ; Normalement, on ne ferait pas 'add esp, X' ici car le CPU n'a pas poussé d'error code.
    ; Et nous n'avons pas poussé de numéro d'interruption.
    ; iret va dépiler EIP, CS, EFLAGS.
    iret


ISR_NOERRCODE 1   ; Debug
ISR_NOERRCODE 2   ; Non Maskable Interrupt
ISR_NOERRCODE 3   ; Breakpoint
ISR_NOERRCODE 4   ; Into Detected Overflow
ISR_NOERRCODE 5   ; Out of Bounds
ISR_NOERRCODE 6   ; Invalid Opcode
ISR_NOERRCODE 7   ; No Coprocessor

ISR_ERRCODE   8   ; Double Fault
ISR_NOERRCODE 9   ; Coprocessor Segment Overrun
ISR_ERRCODE   10  ; Bad TSS
ISR_ERRCODE   11  ; Segment Not Present
ISR_ERRCODE   12  ; Stack Fault
ISR_ERRCODE   13  ; General Protection Fault
ISR_ERRCODE   14  ; Page Fault
ISR_NOERRCODE 15  ; Reserved
ISR_NOERRCODE 16  ; Floating Point Exception (x87 FPU)
ISR_ERRCODE   17  ; Alignment Check
ISR_NOERRCODE 18  ; Machine Check
ISR_NOERRCODE 19  ; SIMD Floating-Point Exception
ISR_NOERRCODE 20  ; Virtualization Exception
ISR_ERRCODE   21  ; Control Protection Exception
; ISRs 22-27 are reserved
ISR_NOERRCODE 22
ISR_NOERRCODE 23
ISR_NOERRCODE 24
ISR_NOERRCODE 25
ISR_NOERRCODE 26
ISR_NOERRCODE 27
ISR_NOERRCODE 28  ; Hypervisor Injection Exception
ISR_ERRCODE   29  ; VMM Communication Exception
ISR_ERRCODE   30  ; Security Exception
ISR_NOERRCODE 31  ; Reserved


; IRQ Handlers
; IRQ 0 is Timer (int 32)
; IRQ 1 is Keyboard (int 33)
; ...
; IRQ 15 is Secondary ATA (int 47)

extern timer_handler_minimal_debug ; Nouvelle fonction C pour le débogage de l'IRQ0

; Ancien stub IRQ0 qui utilisait irq_common_stub
; IRQ 0, 32  ; Timer

; Nouveau stub spécifique pour IRQ0
global irq0
irq0:
    cli
    pusha

    ; Debug: Afficher '0' pour IRQ0 stub direct
    push eax
    push edx
    mov edx, 0xB8000   ; Adresse de base de la mémoire VGA
    mov ah, 0x0D       ; Couleur (Magenta sur Noir)
    mov al, '0'
    mov [edx + (0 * 80 + 70) * 2], ax ; Écrire '0' en (x=70, y=0)
    pop edx
    pop eax

    call timer_handler_minimal_debug

    mov al, 0x20    ; Commande EOI
    out 0x20, al    ; Envoyer EOI au PIC Maître (IRQ0 est sur le maître)

    popa
    ; sti ; sti est souvent omis ici car iret restaure EFLAGS (qui inclut IF)
    iret

; IRQ 1 (Keyboard) will have a more specific handler or call keyboard_handler directly
global irq1
irq1:
    cli
    pusha                 ; Sauvegarde tous les registres

    mov ax, ds
    push eax
    mov ax, 0x10          ; Kernel data segment
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    call keyboard_handler_main ; Appelle notre fonction C pour le clavier

    ; Send EOI to Master PIC for IRQ1
    mov al, 0x20
    out 0x20, al          ; Master PIC command port

    pop ebx
    mov ds, bx
    mov es, bx
    mov fs, bx
    mov gs, bx

    popa                  ; Restaure les registres
    iret                  ; Retour d'interruption

IRQ 2, 34  ; Cascade for Slave PIC
IRQ 3, 35  ; COM2
IRQ 4, 36  ; COM1
IRQ 5, 37  ; LPT2
IRQ 6, 38  ; Floppy Disk
IRQ 7, 39  ; LPT1 / Spurious
IRQ 8, 40  ; RTC
IRQ 9, 41  ; Free / Available (often network or sound)
IRQ 10, 42 ; Free / Available
IRQ 11, 43 ; Free / Available
IRQ 12, 44 ; PS/2 Mouse
IRQ 13, 45 ; FPU / Co-processor
IRQ 14, 46 ; Primary ATA Hard Disk
IRQ 15, 47 ; Secondary ATA Hard Disk

; Declare C functions to be called
extern fault_handler      ; For CPU exceptions
extern irq_handler_c      ; For common IRQs (not keyboard)
extern keyboard_handler_main ; For keyboard (IRQ1)
