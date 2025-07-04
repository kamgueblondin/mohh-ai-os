#include "interrupts.h"
#include "idt.h"
#include "keyboard.h"
#include "kernel/timer.h"
#include "kernel/libc.h"
#include <stdint.h>
#include "kernel/debug_vga.h" // Pour debug_putc_at

// Fonctions/variables globales pour l'affichage (de kernel.c ou vga.c)
extern void print_string(const char* str, char color);
extern char current_color;
extern int vga_x, vga_y;
extern void print_char(char c, int x, int y, char color);


// PIC I/O Ports & Commands
#define PIC1            0x20
#define PIC2            0xA0
#define PIC1_COMMAND    PIC1
#define PIC1_DATA       (PIC1+1)
#define PIC2_COMMAND    PIC2
#define PIC2_DATA       (PIC2+1)
#define PIC_EOI         0x20

#define ICW1_ICW4       0x01
#define ICW1_SINGLE     0x02
#define ICW1_INTERVAL4  0x04
#define ICW1_LEVEL      0x08
#define ICW1_INIT       0x10

#define ICW4_8086       0x01
#define ICW4_AUTO       0x02
#define ICW4_BUF_SLAVE  0x08
#define ICW4_BUF_MASTER 0x0C
#define ICW4_SFNM       0x10

void outb(uint16_t port, uint8_t val) {
    asm volatile ( "outb %0, %1" : : "a"(val), "Nd"(port) );
}

uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile ( "inb %1, %0"
                   : "=a"(ret)
                   : "Nd"(port) );
    return ret;
}

void io_wait(void) {
    outb(0x80, 0);
}

void pic_remap(int offset1, int offset2) {
    outb(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4); io_wait();
    outb(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4); io_wait();
    outb(PIC1_DATA, offset1); io_wait();
    outb(PIC2_DATA, offset2); io_wait();
    outb(PIC1_DATA, 4); io_wait();
    outb(PIC2_DATA, 2); io_wait();
    outb(PIC1_DATA, ICW4_8086); io_wait();
    outb(PIC2_DATA, ICW4_8086); io_wait();

    // Masquer les IRQs.
    // Bit à 0 = démasqué (autorisé). Bit à 1 = masqué (désactivé).
    // PIC1 (Maître): IRQ 0-7
    //   IRQ0 (timer)   -> bit 0
    //   IRQ1 (clavier) -> bit 1
    // Pour démasquer IRQ0 et IRQ1, et masquer 2-7: 1111 1100 = 0xFC
    outb(PIC1_DATA, 0xFC);
    // PIC2 (Esclave): IRQ 8-15. Nous les masquons toutes pour l'instant.
    outb(PIC2_DATA, 0xFF);
}

void fault_handler(void* esp_at_call) {
    uint32_t int_num_fault  = ((uint32_t*)esp_at_call)[9];
    // uint32_t err_code_fault = ((uint32_t*)esp_at_call)[10];

    volatile unsigned short* vga = (unsigned short*)0xB8000;
    char id_char = ' ';

    for (int i = 0; i < 80 * 2; ++i) {
        vga[i] = (unsigned short)' ' | (0x0F << 8);
    }
    vga_x = 0; vga_y = 0;

    if (int_num_fault == 14) {
        id_char = 'P';
        uint32_t faulting_address;
        asm volatile("mov %%cr2, %0" : "=r"(faulting_address));

        vga[0] = (unsigned short)id_char | (0x0C << 8);
        vga[1] = (unsigned short)'F' | (0x0C << 8);

        vga[80*1 + 0] = 'C'; vga[80*1 + 1] = 'R'; vga[80*1 + 2] = '2'; vga[80*1 + 3] = '='; vga[80*1 + 4] = '0'; vga[80*1 + 5] = 'x';
        for (int i = 0; i < 8; i++) {
            char hexdigit = (faulting_address >> ((7-i)*4)) & 0xF;
            if (hexdigit < 10) hexdigit += '0';
            else hexdigit += 'A' - 10;
            vga[80*1 + 6 + i] = (unsigned short)hexdigit | (0x0C << 8);
        }
        // EIP display in fault_handler is complex due to stack variations, focusing on CR2 for Page Fault
        vga[80*0 + 10] = 'E'; vga[80*0 + 11] = 'I'; vga[80*0 + 12] = 'P'; vga[80*0 + 13] = '=';
        for (int i = 0; i < 8; i++) {
            vga[80*0 + 14 + i] = (unsigned short)'?' | (0x0C << 8);
        }
    } else if (int_num_fault == 8) {
        id_char = 'D';
        vga[0] = (unsigned short)id_char | (0x0C << 8);
        vga[1] = (unsigned short)'F' | (0x0C << 8);
    } else {
        id_char = 'E';
        vga[0] = (unsigned short)id_char | (0x0C << 8);
        vga[1] = (unsigned short)((int_num_fault / 10) % 10 + '0') | (0x0C << 8);
        vga[2] = (unsigned short)(int_num_fault % 10 + '0') | (0x0C << 8);
    }
    asm volatile("cli; hlt");
}

static char irq0_debug_indicator = '+';

void irq_handler_c(void* esp_at_call) {
    debug_putc_at('C', 75, 0, 0x0E);

    uint32_t* stack = (uint32_t*)esp_at_call;
    uint32_t int_num_val_at_10 = stack[10]; // Should be dummy error code (0 for IRQs)
    uint32_t int_num_val_at_9 = stack[9];   // Should be interrupt number (e.g., 32 for IRQ0)

    char tens_9 = ((int_num_val_at_9 / 10) % 10) + '0';
    char units_9 = (int_num_val_at_9 % 10) + '0';
    debug_putc_at(tens_9, 73, 0, 0x0F);
    debug_putc_at(units_9, 74, 0, 0x0F);

    char tens_10 = ((int_num_val_at_10 / 10) % 10) + '0';
    char units_10 = (int_num_val_at_10 % 10) + '0';
    debug_putc_at(tens_10, 71, 0, 0x0C);
    debug_putc_at(units_10, 72, 0, 0x0C);

    if (int_num_val_at_9 == 32) {
        debug_putc_at(irq0_debug_indicator, 77, 0, 0x0B);
        if (irq0_debug_indicator == '+') irq0_debug_indicator = '*';
        else irq0_debug_indicator = '+';

        timer_handler();
    }

    if (int_num_val_at_9 >= 32 && int_num_val_at_9 <= 47) {
        if (int_num_val_at_9 >= 40) {
            outb(PIC2_COMMAND, PIC_EOI);
        }
        outb(PIC1_COMMAND, PIC_EOI);
    }
}

void interrupts_init() {
    pic_remap(0x20, 0x28);

    idt_set_gate(0, (uint32_t)isr0, 0x08, 0x8E);
    idt_set_gate(1, (uint32_t)isr1, 0x08, 0x8E);
    // ... (all other ISR gates) ...
    idt_set_gate(11, (uint32_t)isr11, 0x08, 0x8E);
    idt_set_gate(13, (uint32_t)isr13, 0x08, 0x8E);
    idt_set_gate(14, (uint32_t)isr14, 0x08, 0x8E);
    // ... (all other ISR gates) ...
    idt_set_gate(31, (uint32_t)isr31, 0x08, 0x8E);

    idt_set_gate(32, (uint32_t)irq0, 0x08, 0x8E);  // IRQ0  - Timer
    idt_set_gate(33, (uint32_t)irq1, 0x08, 0x8E);  // IRQ1  - Keyboard
    // For brevity, assume other IRQ gates are set up similarly or are less critical for now
    idt_set_gate(34, (uint32_t)irq2, 0x08, 0x8E);
    idt_set_gate(39, (uint32_t)irq7, 0x08, 0x8E);  // IRQ7
    idt_set_gate(47, (uint32_t)irq15, 0x08, 0x8E);

    asm volatile ("sti");
}
