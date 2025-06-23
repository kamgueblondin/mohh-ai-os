#ifndef IDT_H
#define IDT_H

#include <stdint.h> // Pour utiliser les types comme uint32_t, etc.

// Structure d'une entrée dans l'IDT (Interrupt Descriptor Table)
struct idt_entry {
    uint16_t base_low;    // 16 bits bas de l'adresse de l'ISR
    uint16_t selector;    // Sélecteur de segment du noyau
    uint8_t  always0;     // Doit toujours être à zéro
    uint8_t  flags;       // Flags de type et d'attributs
    uint16_t base_high;   // 16 bits hauts de l'adresse de l'ISR
} __attribute__((packed)); // Attribut pour ne pas que le compilateur optimise la structure

// Structure du pointeur de l'IDT
struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

// Déclaration des fonctions
void idt_init();
void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags);

#endif
