#include "interrupts.h"
#include "idt.h"        // Pour idt_set_gate, et les extern isrX, irqX
#include "keyboard.h"   // keyboard_handler_main est appelé par le stub irq1
#include "kernel/timer.h" // Pour timer_tick
#include "kernel/libc.h" // Pour itoa
#include <stdint.h>

// Fonctions/variables globales pour l'affichage (de kernel.c ou vga.c)
extern void print_string(const char* str, char color);
extern char current_color;
extern int vga_x, vga_y;
extern void print_char(char c, int x, int y, char color);


// PIC I/O Ports & Commands (déjà définis dans le fichier original, je les garde)
#define PIC1            0x20    /* IO base address for master PIC */
#define PIC2            0xA0    /* IO base address for slave PIC */
#define PIC1_COMMAND    PIC1
#define PIC1_DATA       (PIC1+1)
#define PIC2_COMMAND    PIC2
#define PIC2_DATA       (PIC2+1)
#define PIC_EOI         0x20    /* End-of-interrupt command code */

#define ICW1_ICW4       0x01    /* Indicates that ICW4 will be present */
#define ICW1_SINGLE     0x02    /* Single (cascade) mode */
#define ICW1_INTERVAL4  0x04    /* Call address interval 4 (8) */
#define ICW1_LEVEL      0x08    /* Level triggered (edge) mode */
#define ICW1_INIT       0x10    /* Initialization - required! */

#define ICW4_8086       0x01    /* 8086/88 (MCS-80/85) mode */
#define ICW4_AUTO       0x02    /* Auto (normal) EOI */
#define ICW4_BUF_SLAVE  0x08    /* Buffered mode/slave */
#define ICW4_BUF_MASTER 0x0C    /* Buffered mode/master */
#define ICW4_SFNM       0x10    /* Special fully nested (not) */

// Helper function to write a byte to an I/O port
void outb(uint16_t port, uint8_t val) {
    asm volatile ( "outb %0, %1" : : "a"(val), "Nd"(port) );
}

// Helper function to read a byte from an I/O port
uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile ( "inb %1, %0"
                   : "=a"(ret)
                   : "Nd"(port) );
    return ret;
}

// Helper function to introduce a small delay for PIC
void io_wait(void) {
    outb(0x80, 0);
}

void pic_remap(int offset1, int offset2) {
    // unsigned char a1, a2; // Variables non utilisées, initialement pour sauvegarder les masques
    // a1 = inb(PIC1_DATA);
    // a2 = inb(PIC2_DATA);
    outb(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4); io_wait();
    outb(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4); io_wait();
    outb(PIC1_DATA, offset1); io_wait();
    outb(PIC2_DATA, offset2); io_wait();
    outb(PIC1_DATA, 4); io_wait();
    outb(PIC2_DATA, 2); io_wait();
    outb(PIC1_DATA, ICW4_8086); io_wait();
    outb(PIC2_DATA, ICW4_8086); io_wait();
    outb(PIC1_DATA, 0x00); // Unmask all for Master
    outb(PIC2_DATA, 0x00); // Unmask all for Slave
}

// Handler C pour les exceptions CPU (ISRs 0-31)
// Appelé par isr_common_stub depuis isr_stubs.s
void fault_handler(void* esp_at_call) {
    // Stack layout from isr_stubs.s:
    // esp_at_call points to where 'call fault_handler' return address would be if it wasn't a pointer.
    // Actual stack arguments start after this.
    // PUSHAD order: EDI, ESI, EBP, ESP_dummy, EBX, EDX, ECX, EAX
    // Stack before `call fault_handler` from `isr_common_stub`:
    // [ESP from stub]   -> ds_val (pushed by stub)
    // [ESP from stub +4]  -> EDI (from PUSHAD)
    // ...
    // [ESP from stub +4+28] -> EAX (from PUSHAD)
    // [ESP from stub +4+32] -> int_num (pushed by macro)
    // [ESP from stub +4+36] -> err_code (pushed by CPU or macro)
    // [ESP from stub +4+40] -> EIP_at_fault (pushed by CPU)
    // [ESP from stub +4+44] -> CS_at_fault (pushed by CPU)
    // [ESP from stub +4+48] -> EFLAGS_at_fault (pushed by CPU)
    // The argument `esp_at_call` is ESP *within* `isr_common_stub` just before `call fault_handler`.
    // So, if isr_common_stub did:
    //   pusha
    //   push ds_val_in_eax
    //   call fault_handler  <-- esp_at_call is effectively [esp+4] from the C perspective if using argument passing
    //                         However, the stub does not push 'esp' to pass to 'fault_handler'.
    //                         The 'esp_at_call' is the C function's view of its own stack frame base,
    //                         and the actual parameters are relative to the original ESP when the stub was entered.
    // Let's re-verify stack layout in isr_stubs.s:
    // isr_common_stub:
    //   pusha              ; Pushes edi,esi,ebp,esp,ebx,edx,ecx,eax (edi lowest addr, eax highest of this block)
    //   mov ax, ds
    //   push eax           ; save the data segment descriptor
    //   mov ax, 0x10       ; load the kernel data segment descriptor
    //   ...
    //   call fault_handler ; C function sees arguments pushed *by the caller* (CPU or macros)
    //                      ; The actual registers are on the stack *before* this call was made.
    //                      ; The parameters int_num, err_code, eip, cs, eflags are pushed by CPU/macros *before* isr_common_stub
    //                      ; Then isr_common_stub pushes dummy error_code, int_num (again, for its own logic)
    //                      ; This is confusing. Let's assume the indices [10], [11], [12] used before were correct
    //                      ; relative to the 'esp' value *after* all items were pushed by CPU and stubs.

    // uint32_t* stack_from_stub_perspective = (uint32_t*)((char*)esp_at_call + 4); // Non utilisé, probablement pour du débogage antérieur

    // Simpler interpretation: The `esp_at_call` is the `esp` right after `push eax` (ds_val) in the stub.
    // So:
    // esp_at_call[0] = ds_val
    // esp_at_call[1] = edi
    // ...
    // esp_at_call[8] = eax_val
    // esp_at_call[9] = int_num (pushed by macro ISR_NOERRCODE/ERRCODE before jmp isr_common_stub)
    // esp_at_call[10] = err_code (pushed by CPU or macro before jmp isr_common_stub)
    // esp_at_call[11] = eip_fault
    // This seems more plausible with the original indexing [10], [11], [12] for int_num, err_code, eip.
    // Let's stick to the original interpretation of stack indices [10], [11], [12] relative to
    // the base of the "interrupt frame" passed implicitly to fault_handler.

    uint32_t int_num  = ((uint32_t*)esp_at_call)[10]; // int_num pushed by our ISR_ macro
    // uint32_t err_code = ((uint32_t*)esp_at_call)[11]; // err_code pushed by CPU or our macro
    // uint32_t eip_fault= ((uint32_t*)esp_at_call)[12]; // eip pushed by CPU

    volatile unsigned short* vga = (unsigned short*)0xB8000;
    char id_char = ' ';

    // Clear a portion of the screen first to make messages visible
    // for (int i = 0; i < 80 * 2; ++i) { // Clear top 2 lines
    //     vga[i] = (unsigned short)' ' | (0x0F << 8); // White on Black
    // }
    // Commented out clear screen to ensure our VGA debug markers are not erased if fault occurs very early.
    // vga_x = 0; vga_y = 0; // Reset internal cursor for print_char if it were used

    if (int_num == 13) { // General Protection Fault
        id_char = 'G'; // For GPF
        vga[3] = (vga[3] & 0x00FF) | (0x5F00); // 4th char, Fond Magenta, Texte BlancBrillant 'G'
        vga[0] = (unsigned short)id_char | (0x0C << 8);
        vga[1] = (unsigned short)'P' | (0x0C << 8);
        vga[2] = (unsigned short)'F' | (0x0C << 8);
         uint32_t err_code_gpf = ((uint32_t*)esp_at_call)[11]; // err_code for GPF
         // Display error code for GPF
        vga[80*1 + 0] = 'E'; vga[80*1 + 1] = 'R'; vga[80*1 + 2] = 'C'; vga[80*1 + 3] = '=';
        for (int i = 0; i < 8; i++) {
            char hexdigit = (err_code_gpf >> ((7-i)*4)) & 0xF;
            if (hexdigit < 10) hexdigit += '0';
            else hexdigit += 'A' - 10;
            vga[80*1 + 4 + i] = (unsigned short)hexdigit | (0x0C << 8);
        }

    } else if (int_num == 14) { // Page Fault
        id_char = 'P';
        uint32_t faulting_address;
        asm volatile("mov %%cr2, %0" : "=r"(faulting_address));

        vga[0] = (unsigned short)id_char | (0x0C << 8); // Red on Black for 'P'
        vga[1] = (unsigned short)'F' | (0x0C << 8);

        // Display faulting_address (CR2)
        // Example: "CR2=0x12345678" at line 1
        vga[80*1 + 0] = 'C'; vga[80*1 + 1] = 'R'; vga[80*1 + 2] = '2'; vga[80*1 + 3] = '='; vga[80*1 + 4] = '0'; vga[80*1 + 5] = 'x';
        for (int i = 0; i < 8; i++) {
            char hexdigit = (faulting_address >> ((7-i)*4)) & 0xF;
            if (hexdigit < 10) hexdigit += '0';
            else hexdigit += 'A' - 10;
            vga[80*1 + 6 + i] = (unsigned short)hexdigit | (0x0C << 8);
        }
         // Display EIP that caused the fault
        uint32_t eip_val = ((uint32_t*)esp_at_call)[12];
        vga[80*0 + 10] = 'E'; vga[80*0 + 11] = 'I'; vga[80*0 + 12] = 'P'; vga[80*0 + 13] = '=';
        for (int i = 0; i < 8; i++) {
            char hexdigit = (eip_val >> ((7-i)*4)) & 0xF;
            if (hexdigit < 10) hexdigit += '0';
            else hexdigit += 'A' - 10;
            vga[80*0 + 14 + i] = (unsigned short)hexdigit | (0x0C << 8);
        }


    } else if (int_num == 8) { // Double Fault
        id_char = 'D';
        vga[0] = (unsigned short)id_char | (0x0C << 8);
        vga[1] = (unsigned short)'F' | (0x0C << 8);
         //uint32_t err_code_df = ((uint32_t*)esp_at_call)[11]; // DF error code is often 0
    } else { // Other exceptions
        id_char = 'E';
        vga[0] = (unsigned short)id_char | (0x0C << 8);
        vga[1] = (unsigned short)('0' + (int_num / 10)) | (0x0C << 8);
        vga[2] = (unsigned short)('0' + (int_num % 10)) | (0x0C << 8);
    }

    asm volatile("cli; hlt");
}

// Handler C pour les IRQs (32-47), sauf clavier (IRQ1)
// Appelé par irq_common_stub depuis isr_stubs.s
void irq_handler_c(void* esp_at_call) {
    uint32_t* stack = (uint32_t*)esp_at_call;
    uint32_t int_num = stack[10]; // Même logique d'offset que pour fault_handler

    if (int_num == 32) { // IRQ0 (Timer)
        timer_handler(); // Appeler le handler principal du timer
    }
    // D'autres IRQs pourraient être gérés ici si nécessaire.
    // Le clavier (IRQ1/INT33) a son propre stub qui appelle keyboard_handler_main.

    // Envoyer EOI (End Of Interrupt)
    if (int_num >= 40) { // IRQ 8-15 (remappé à 40-47) vient de l'esclave
        outb(PIC2_COMMAND, PIC_EOI);
    }
    outb(PIC1_COMMAND, PIC_EOI); // Toujours envoyer au maître
}

// Initialise le PIC et configure les entrées IDT pour les ISRs et IRQs.
void interrupts_init() {
    pic_remap(0x20, 0x28); // IRQs 0-7 à 0x20-0x27 (32-39), IRQs 8-15 à 0x28-0x2F (40-47)

    // Configuration des ISRs (Exceptions CPU)
    idt_set_gate(0, (uint32_t)isr0, 0x08, 0x8E);
    idt_set_gate(1, (uint32_t)isr1, 0x08, 0x8E);
    idt_set_gate(2, (uint32_t)isr2, 0x08, 0x8E);
    idt_set_gate(3, (uint32_t)isr3, 0x08, 0x8E);
    idt_set_gate(4, (uint32_t)isr4, 0x08, 0x8E);
    idt_set_gate(5, (uint32_t)isr5, 0x08, 0x8E);
    idt_set_gate(6, (uint32_t)isr6, 0x08, 0x8E);
    idt_set_gate(7, (uint32_t)isr7, 0x08, 0x8E);
    idt_set_gate(8, (uint32_t)isr8, 0x08, 0x8E);
    idt_set_gate(9, (uint32_t)isr9, 0x08, 0x8E);
    idt_set_gate(10, (uint32_t)isr10, 0x08, 0x8E);
    idt_set_gate(11, (uint32_t)isr11, 0x08, 0x8E);
    idt_set_gate(12, (uint32_t)isr12, 0x08, 0x8E);
    idt_set_gate(13, (uint32_t)isr13, 0x08, 0x8E);
    idt_set_gate(14, (uint32_t)isr14, 0x08, 0x8E); // Page Fault
    idt_set_gate(15, (uint32_t)isr15, 0x08, 0x8E);
    // ... (autres ISRs jusqu'à 31) ...
    idt_set_gate(16, (uint32_t)isr16, 0x08, 0x8E);
    idt_set_gate(17, (uint32_t)isr17, 0x08, 0x8E);
    idt_set_gate(18, (uint32_t)isr18, 0x08, 0x8E);
    idt_set_gate(19, (uint32_t)isr19, 0x08, 0x8E);
    idt_set_gate(20, (uint32_t)isr20, 0x08, 0x8E);
    idt_set_gate(21, (uint32_t)isr21, 0x08, 0x8E);
    idt_set_gate(22, (uint32_t)isr22, 0x08, 0x8E);
    idt_set_gate(23, (uint32_t)isr23, 0x08, 0x8E);
    idt_set_gate(24, (uint32_t)isr24, 0x08, 0x8E);
    idt_set_gate(25, (uint32_t)isr25, 0x08, 0x8E);
    idt_set_gate(26, (uint32_t)isr26, 0x08, 0x8E);
    idt_set_gate(27, (uint32_t)isr27, 0x08, 0x8E);
    idt_set_gate(28, (uint32_t)isr28, 0x08, 0x8E);
    idt_set_gate(29, (uint32_t)isr29, 0x08, 0x8E);
    idt_set_gate(30, (uint32_t)isr30, 0x08, 0x8E);
    idt_set_gate(31, (uint32_t)isr31, 0x08, 0x8E);

    // Configuration des IRQs (Hardware Interrupts)
    idt_set_gate(32, (uint32_t)irq0, 0x08, 0x8E);  // Timer
    idt_set_gate(33, (uint32_t)irq1, 0x08, 0x8E);  // Clavier
    idt_set_gate(34, (uint32_t)irq2, 0x08, 0x8E);  // Cascade
    // ... (autres IRQs jusqu'à 47) ...
    idt_set_gate(35, (uint32_t)irq3, 0x08, 0x8E);
    idt_set_gate(36, (uint32_t)irq4, 0x08, 0x8E);
    idt_set_gate(37, (uint32_t)irq5, 0x08, 0x8E);
    idt_set_gate(38, (uint32_t)irq6, 0x08, 0x8E);
    idt_set_gate(39, (uint32_t)irq7, 0x08, 0x8E);
    idt_set_gate(40, (uint32_t)irq8, 0x08, 0x8E);
    idt_set_gate(41, (uint32_t)irq9, 0x08, 0x8E);
    idt_set_gate(42, (uint32_t)irq10, 0x08, 0x8E);
    idt_set_gate(43, (uint32_t)irq11, 0x08, 0x8E);
    idt_set_gate(44, (uint32_t)irq12, 0x08, 0x8E);
    idt_set_gate(45, (uint32_t)irq13, 0x08, 0x8E);
    idt_set_gate(46, (uint32_t)irq14, 0x08, 0x8E);
    idt_set_gate(47, (uint32_t)irq15, 0x08, 0x8E);

    asm volatile ("sti"); // Activer les interruptions globalement
}
