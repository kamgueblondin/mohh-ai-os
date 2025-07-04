#ifndef GDT_H
#define GDT_H

#include <stdint.h>

// Structure d'une entrée de la GDT
struct gdt_entry {
    uint16_t limit_low;    // Les 16 bits inférieurs de la limite
    uint16_t base_low;     // Les 16 bits inférieurs de la base
    uint8_t  base_middle;  // Les 8 bits suivants de la base
    uint8_t  access;       // Drapeaux d'accès (type, S, DPL, P)
    uint8_t  granularity;  // Granularité (G, D/B, L, AVL) et les 4 bits supérieurs de la limite
    uint8_t  base_high;    // Les 8 bits supérieurs de la base
} __attribute__((packed)); // Attribut pour éviter le padding

// Structure du pointeur GDT (pour l'instruction lgdt)
struct gdt_ptr {
    uint16_t limit; // Taille de la GDT - 1
    uint32_t base;  // Adresse de début de la GDT
} __attribute__((packed));

// Fonction pour initialiser la GDT
void gdt_init();

// Fonction externe pour charger la GDT (définie en assembleur)
extern void gdt_load(struct gdt_ptr* gdtp);
extern void segments_reload(); // Pour recharger CS et les autres segments après lgdt

#endif // GDT_H
