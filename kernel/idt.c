#include "idt.h"
#include <stdint.h> // Pour uint32_t etc.

// Déclaration de notre table IDT (256 entrées)
struct idt_entry idt[256];
struct idt_ptr idtp;

// Fonction externe (définie dans idt_loader.s)
extern void idt_load(struct idt_ptr* idtp); // Corrigé pour passer un pointeur

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
    // Configure le pointeur de l'IDT
    idtp.limit = (sizeof(struct idt_entry) * 256) - 1;
    idtp.base = (uint32_t)&idt;

    // Remplit l'IDT avec des ISR nulles (par défaut)
    // Ceci remplace le memset commenté
    for (int i = 0; i < 256; i++) {
        idt_set_gate(i, 0, 0, 0);
    }

    // Charge la nouvelle IDT
    idt_load(&idtp); // Passe l'adresse de idtp
}
