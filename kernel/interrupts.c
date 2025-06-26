#include "interrupts.h"
#include "idt.h"        // Pour idt_set_gate, et les extern isrX, irqX
#include "keyboard.h"   // keyboard_handler_main est appelé par le stub irq1
#include "timer.h"      // timer_tick est appelé par irq_handler_c pour l'IRQ0
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
    unsigned char a1, a2;
    a1 = inb(PIC1_DATA);
    a2 = inb(PIC2_DATA);
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
// esp_at_call est la valeur de ESP juste avant l'instruction CALL fault_handler dans isr_common_stub
void fault_handler(void* esp_at_call) {
    // Sur la pile à partir de esp_at_call:
    // stack[0] = adresse de retour de call fault_handler (vers isr_common_stub)
    // stack[1] = ds_val (poussé par 'push eax' où eax contenait ds)
    // stack[2] = edi (début de PUSHAD)
    // ...
    // stack[9] = eax (fin de PUSHAD)
    // stack[10] = int_num (poussé par la macro ISR_*)
    // stack[11] = err_code (poussé par CPU ou macro ISR_*)
    // stack[12] = eip_at_fault (poussé par CPU)
    // stack[13] = cs_at_fault
    // stack[14] = eflags_at_fault
    uint32_t* stack = (uint32_t*)esp_at_call;
    uint32_t int_num  = stack[10];
    uint32_t err_code = stack[11];
    uint32_t eip_fault= stack[12];

    char buffer[12];
    char color_err = 0x0C; // Rouge sur fond noir

    if (int_num == 14) { // Page Fault
        uint32_t faulting_address;
        asm volatile("mov %%cr2, %0" : "=r"(faulting_address));

        print_string("\n!! PAGE FAULT (14) !!\n", color_err);
        print_string("Adresse fautive: 0x", color_err);
        itoa(faulting_address, buffer, 16); print_string(buffer, color_err);

        print_string("\nCode d'erreur: 0x", color_err);
        itoa(err_code, buffer, 16); print_string(buffer, color_err);
        print_string(" (", color_err);
        if (!(err_code & 0x1)) print_string("P", color_err); else print_string("p", color_err); // Present
        if (err_code & 0x2) print_string("W", color_err); else print_string("r", color_err);    // Write/Read
        if (err_code & 0x4) print_string("U", color_err); else print_string("s", color_err);    // User/Supervisor
        if (err_code & 0x8) print_string("R", color_err);                                 // Reserved
        if (err_code & 0x10) print_string("I", color_err);                                // Instruction Fetch
        print_string(")\n", color_err);

        print_string("EIP: 0x", color_err);
        itoa(eip_fault, buffer, 16); print_string(buffer, color_err);
        print_string("\n", color_err);
    } else {
        print_string("\n!! EXCEPTION CPU ", color_err);
        itoa(int_num, buffer, 10); print_string(buffer, color_err);
        print_string(" !!\n", color_err);
        print_string("EIP: 0x", color_err);
        itoa(eip_fault, buffer, 16); print_string(buffer, color_err);
        print_string("\nCode d'erreur: 0x", color_err);
        itoa(err_code, buffer, 16); print_string(buffer, color_err);
        print_string("\n", color_err);
    }

    print_string("Systeme arrete.\n", color_err);
    asm volatile("cli; hlt");
}

// Handler C pour les IRQs (32-47), sauf clavier (IRQ1)
// Appelé par irq_common_stub depuis isr_stubs.s
void irq_handler_c(void* esp_at_call) {
    uint32_t* stack = (uint32_t*)esp_at_call;
    uint32_t int_num = stack[10]; // Même logique d'offset que pour fault_handler

    if (int_num == 32) { // IRQ0 (Timer)
        timer_tick();
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
