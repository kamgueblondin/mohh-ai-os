bits 32

%macro ISR_NOERRCODE 1  ; Macro for ISRs without error codes
global isr%1
isr%1:
    cli                ; Disable interrupts
    ; La section %if %1 == 3 a été supprimée, isr3 utilise maintenant le chemin standard.
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
    ; Test VGA direct pour vérifier l'entrée dans isr_common_stub
    mov edi, 0xB8000
    mov word [edi + 7*2], 0x2F43 ; 8th char: 'C' (0x43) White on Green (0x2F)

stub_loop:
    hlt
    jmp stub_loop

    ; Code original commenté pour ce test:
    ; pusha              ; Pushes edi,esi,ebp,esp,ebx,edx,ecx,eax
    ; mov ax, ds         ; Lower 16-bits of eax = ds.
    ; push eax           ; save the data segment descriptor
    ; mov ax, 0x10       ; load the kernel data segment descriptor
    ; mov ds, ax
    ; mov es, ax
    ; mov fs, ax
    ; mov gs, ax
    ;
    ; call fault_handler ; Call C handler
    ;
    ; pop ebx            ; reload the original data segment descriptor
    ; mov ds, bx
    ; mov es, bx
    ; mov fs, bx
    ; mov gs, bx
    ; popa               ; Pops edi,esi,ebp...
    ; add esp, 8         ; Cleans up the pushed error code and pushed ISR number
    ; iret               ; pops 5 things at once: CS, EIP, EFLAGS, SS, ESP

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
ISR_NOERRCODE 0   ; Divide by zero
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

IRQ 0, 32  ; Timer
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
