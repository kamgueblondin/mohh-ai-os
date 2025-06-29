#include "idt.h"
#include <stdint.h> // For uint32_t etc.

// Déclaration de notre table IDT (256 entrées)
struct idt_entry idt[256];
struct idt_ptr idtp;

// Fonction externe (définie dans idt_loader.s)
extern void idt_load(struct idt_ptr* idtp); // Corrected to pass a pointer

// Initialise une entrée de l'IDT
void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags) {
    idt[num].base_low = (base & 0xFFFF);
    idt[num].base_high = (base >> 16) & 0xFFFF;
    idt[num].selector = sel;
    idt[num].always0 = 0;
    idt[num].flags = flags;
}

// Initialise l'IDT entière
void idt_init() {
    extern void print_string(const char* str, char color); // Pour le débogage
    extern void print_hex(uint32_t n, char color); // Pour le débogage

    print_string("DEBUG_IDT_INIT: Entered.\n", 0x0D); // Magenta clair

    // Configure le pointeur de l'IDT
    idtp.limit = (sizeof(struct idt_entry) * 256) - 1;
    idtp.base = (uint32_t)&idt;
    print_string("DEBUG_IDT_INIT: idtp.limit=", 0x0D); print_hex(idtp.limit, 0x0D);
    print_string(", idtp.base=", 0x0D); print_hex(idtp.base, 0x0D); print_string("\n", 0x0D);

    // Remplit l'IDT avec des ISR nulles (par défaut)
    for (int i = 0; i < 256; i++) {
        idt_set_gate(i, 0, 0, 0); // Initialisation à des handlers nuls
    }
    print_string("DEBUG_IDT_INIT: IDT zeroed.\n", 0x0D);

    // Charge la nouvelle IDT
    print_string("DEBUG_IDT_INIT: Calling idt_load(&idtp).\n", 0x0D);
    idt_load(&idtp); // Pass the address of idtp
    print_string("DEBUG_IDT_INIT: Returned from idt_load(). IDT should be active.\n", 0x0D);
}
