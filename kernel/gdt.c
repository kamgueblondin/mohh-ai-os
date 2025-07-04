#include "gdt.h"
#include <stddef.h> // Pour NULL

// Définition de la GDT elle-même (5 entrées : Nul, Code Noyau, Données Noyau, Code Utilisateur, Données Utilisateur)
#define GDT_ENTRIES 5
struct gdt_entry gdt[GDT_ENTRIES];
struct gdt_ptr gdtp;

// Fonction pour définir une entrée dans la GDT
static void gdt_set_gate(int32_t num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt[num].base_low    = (base & 0xFFFF);
    gdt[num].base_middle = (base >> 16) & 0xFF;
    gdt[num].base_high   = (base >> 24) & 0xFF;

    gdt[num].limit_low   = (limit & 0xFFFF);
    gdt[num].granularity = (limit >> 16) & 0x0F; // 4 bits supérieurs de la limite

    gdt[num].granularity |= gran & 0xF0; // Attributs de granularité (G, D/B, L, AVL)
    gdt[num].access      = access;       // Drapeaux d'accès (type, S, DPL, P)
}

// Initialise la GDT
void gdt_init() {
    // Configure le pointeur GDT
    gdtp.limit = (sizeof(struct gdt_entry) * GDT_ENTRIES) - 1;
    gdtp.base  = (uint32_t)&gdt;

    // 0: Descripteur Nul (obligatoire)
    gdt_set_gate(0, 0, 0, 0, 0);

    // 1: Segment de Code Noyau (Ring 0)
    // Base=0, Limite=4GB (0xFFFFF en unités de 4KB), Accès=0x9A, Granularité=0xCF
    // Accès 0x9A: P=1, DPL=00, S=1 (Code/Data), Type=1010 (Code, Exécutable, Readable, non-Accessed)
    // Granularité 0xCF: G=1 (limite en pages de 4KB), D/B=1 (segment 32-bit), L=0 (pas 64-bit), AVL=0; + 4 bits hauts de la limite (0xF sur 0xFFFFF)
    gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF);

    // 2: Segment de Données Noyau (Ring 0)
    // Base=0, Limite=4GB, Accès=0x92, Granularité=0xCF
    // Accès 0x92: P=1, DPL=00, S=1, Type=0010 (Data, Read/Write, non-Accessed)
    gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF);

    // 3: Segment de Code Utilisateur (Ring 3)
    // Base=0, Limite=4GB, Accès=0xFA, Granularité=0xCF
    // Accès 0xFA: P=1, DPL=11 (Ring 3), S=1, Type=1010 (Code, Exécutable, Readable)
    gdt_set_gate(3, 0, 0xFFFFFFFF, 0xFA, 0xCF);

    // 4: Segment de Données Utilisateur (Ring 3)
    // Base=0, Limite=4GB, Accès=0xF2, Granularité=0xCF
    // Accès 0xF2: P=1, DPL=11 (Ring 3), S=1, Type=0010 (Data, Read/Write)
    gdt_set_gate(4, 0, 0xFFFFFFFF, 0xF2, 0xCF);

    // Charge la GDT
    gdt_load(&gdtp);
    // Après lgdt, CS pointe toujours vers l'ancien segment.
    // Il faut faire un far jump ou appeler une fonction qui recharge les segments.
    // segments_reload(); // Cette fonction sera appelée depuis gdt_load en assembleur.
}
