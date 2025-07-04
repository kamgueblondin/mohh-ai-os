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
    outb(PIC1_DATA, 0x00);
    outb(PIC2_DATA, 0x00);
}

void fault_handler(void* esp_at_call) {
    uint32_t int_num_fault  = ((uint32_t*)esp_at_call)[9]; // Correct index for interrupt number
    // uint32_t err_code_fault = ((uint32_t*)esp_at_call)[10]; // Correct index for error code

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
        uint32_t eip_val = ((uint32_t*)esp_at_call)[11]; // EIP is at [esp_at_call+11] (after int_num and err_code on stack by CPU)
                                                        // This might need adjustment based on actual stack frame from isr_stubs.s
                                                        // If isr_stubs pushes err_code then int_num, then EIP is after that.
                                                        // The CPU pushes EIP, CS, EFLAGS. Then our stub pushes err_code, int_num.
                                                        // So EIP is further down.
                                                        // Let's assume the original indexing for fault_handler was based on a different stack view or was for debug.
                                                        // For now, this EIP might be incorrect. The CR2 is more reliable for PF.
        vga[80*0 + 10] = 'E'; vga[80*0 + 11] = 'I'; vga[80*0 + 12] = 'P'; vga[80*0 + 13] = '=';
        for (int i = 0; i < 8; i++) {
            char hexdigit = '?'; // Placeholder as EIP index might be wrong
            // char hexdigit = (eip_val >> ((7-i)*4)) & 0xF;
            // if (hexdigit < 10) hexdigit += '0';
            // else hexdigit += 'A' - 10;
            vga[80*0 + 14 + i] = (unsigned short)hexdigit | (0x0C << 8);
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
    uint32_t int_num_val_at_10 = stack[10];
    uint32_t int_num_val_at_9 = stack[9];

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

    // Utiliser int_num_val_at_9 qui est le numéro d'interruption correct.
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
    idt_set_gate(14, (uint32_t)isr14, 0x08, 0x8E);
    idt_set_gate(15, (uint32_t)isr15, 0x08, 0x8E);
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

    idt_set_gate(32, (uint32_t)irq0, 0x08, 0x8E);
    idt_set_gate(33, (uint32_t)irq1, 0x08, 0x8E);
    idt_set_gate(34, (uint32_t)irq2, 0x08, 0x8E);
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

    asm volatile ("sti");
}
