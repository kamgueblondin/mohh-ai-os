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
    uint32_t* stack = (uint32_t*)esp_at_call;
    uint32_t s8, s9, s10, s11;

    // Lire prudemment les valeurs de la pile
    // Ces indices sont basés sur l'analyse de la pile après `call fault_handler` dans `isr_common_stub`
    // stack[0] = original ds
    // stack[1-8] = edi, esi, ebp, esp_dummy, ebx, edx, ecx, eax (de pusha)
    // stack[9] = interrupt_number (poussé par la macro ISR_NOERRCODE/ISR_ERRCODE)
    // stack[10] = error_code (poussé par CPU ou macro ISR_NOERRCODE/ISR_ERRCODE)
    // stack[11] = EIP original poussé par CPU
    // stack[12] = CS original poussé par CPU
    // stack[13] = EFLAGS original poussé par CPU
    s8 = stack[8];   // EAX de pusha
    s9 = stack[9];   // int_num
    s10 = stack[10]; // err_code
    s11 = stack[11]; // EIP original

    uint32_t int_num_fault = s9; // Utiliser s9 comme numéro d'interruption pour la logique suivante

    // Utiliser les fonctions d'affichage globales après avoir effacé l'écran.
    // S'assurer que current_color est défini, ou utiliser une couleur fixe.
    char fault_color = 0x0F; // Blanc sur Noir pour le débogage
    clear_screen(fault_color); // Efface l'écran et réinitialise vga_x, vga_y

    print_string("S8 (EAX):", fault_color); print_hex(s8, fault_color); print_string("\n", fault_color);
    print_string("S9 (INT):", fault_color); print_hex(s9, fault_color); print_string("\n", fault_color);
    print_string("S10(ERR):", fault_color); print_hex(s10, fault_color); print_string("\n", fault_color);
    print_string("S11(EIP):", fault_color); print_hex(s11, fault_color); print_string("\n", fault_color);

    if (int_num_fault == 14) { // Page Fault
        uint32_t faulting_address;
        asm volatile("mov %%cr2, %0" : "=r"(faulting_address));
        print_string("Page Fault (14) at CR2: ", fault_color);
        print_hex(faulting_address, fault_color);
        print_string("\n", fault_color);
    } else if (int_num_fault == 8) { // Double Fault
        print_string("Double Fault (8)\n", fault_color);
    } else {
        print_string("Exception: ", fault_color);
        char num_str[12];
        itoa(int_num_fault, num_str, 10);
        print_string(num_str, fault_color);
        print_string("\n", fault_color);
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

// Handler C minimal pour le test de l'INT 0
void minimal_int0_handler_c() {
    // Utiliser une couleur différente pour être sûr que c'est ce handler
    debug_putc_at('0', 0, 0, 0x0C); // '0' Rouge à (0,0)
    debug_putc_at('!', 1, 0, 0x0C); // '!' Rouge à (1,0)
    asm volatile("cli; hlt"); // Arrêter ici pour observer
}
