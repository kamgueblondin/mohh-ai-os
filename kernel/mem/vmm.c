#include "vmm.h"
#include "pmm.h"    // Pour pmm_alloc_page si on alloue dynamiquement les tables de pages
#include <stddef.h> // Pour NULL

// Le répertoire de pages du noyau. Doit être aligné sur 4KB.
// Pour un premier noyau simple, on peut l'allouer statiquement.
// Dans un système plus complexe, il pourrait être alloué dynamiquement.
__attribute__((aligned(4096))) uint32_t kernel_page_directory[1024];

// Une première table de pages pour mapper les premiers 4MB de la mémoire physique.
// Également alignée sur 4KB.
__attribute__((aligned(4096))) uint32_t first_page_table[1024];

// Fonctions assembleur externes pour charger le répertoire de pages et activer le paging.
extern void load_page_directory(uint32_t* page_directory_physical_addr);
extern void enable_paging();

// Flags pour les entrées de Page Directory et Page Table
#define PAGE_PRESENT    0x1
#define PAGE_READ_WRITE 0x2
#define PAGE_USER_SUPERVISOR 0x4 // 0 pour Supervisor, 1 pour User

void vmm_init() {
    // 1. Initialiser la première table de pages (first_page_table)
    //    pour mapper les 4 premiers Mo de la mémoire physique (0x00000000 - 0x003FFFFF)
    //    de manière identitaire (virtuel = physique).
    for (int i = 0; i < 1024; i++) {
        // Adresse physique de la page = i * 4KB (0x1000)
        // Flags: Présent (1), Read/Write (1), Supervisor (0) -> 0b011 = 3
        first_page_table[i] = (i * 0x1000) | (PAGE_PRESENT | PAGE_READ_WRITE);
    }

    // 2. Initialiser le répertoire de pages (kernel_page_directory)
    //    La première entrée du répertoire de pages pointera vers first_page_table.
    //    Cela couvre les adresses virtuelles 0x00000000 à 0x003FFFFF.
    //    Adresse physique de first_page_table. Flags: Présent, Read/Write, Supervisor.
    kernel_page_directory[0] = ((uint32_t)&first_page_table) | (PAGE_PRESENT | PAGE_READ_WRITE);

    // Les autres 1023 entrées du répertoire de pages ne sont pas utilisées pour l'instant.
    // Elles devraient être marquées comme non présentes.
    // Flags: Non Présent (0), Read/Write (1) -> 0b010 = 2 (ou juste 0)
    for (int i = 1; i < 1024; i++) {
        kernel_page_directory[i] = 0; // Ou PAGE_READ_WRITE si on veut permettre la création future sans changer les flags R/W
    }

    // 3. Charger l'adresse physique du répertoire de pages dans CR3.
    //    Note: load_page_directory s'attend à une adresse physique.
    //    Si le paging n'est pas encore actif, l'adresse de &kernel_page_directory est physique.
    load_page_directory((uint32_t*)&kernel_page_directory);

    // 4. Activer le paging (mettre à 1 le bit PG (31) du registre CR0).
    enable_paging();

    // À partir de ce point, le paging est actif !
    // Toutes les adresses sont des adresses virtuelles.
}

// Mappe une page virtuelle à une page physique.
// Pour l'instant, cette fonction est un stub et ne fait rien.
// Une implémentation complète nécessiterait de:
// 1. Trouver l'entrée du Page Directory correspondant à virtual_addr.
// 2. Si l'entrée du PD n'existe pas ou ne pointe pas vers une Page Table valide,
//    allouer une nouvelle Page Table (avec pmm_alloc_page), l'initialiser,
//    et mettre à jour l'entrée du PD.
// 3. Trouver l'entrée de la Page Table correspondant à virtual_addr.
// 4. Mettre l'adresse physical_addr et les flags dans cette entrée de PT.
// 5. Invalider la TLB pour cette adresse virtuelle (asm volatile("invlpg (%0)" ::"r" (virtual_addr) : "memory");)
void vmm_map_page(void* virtual_addr, void* physical_addr) {
    (void)virtual_addr; // Supprimer les avertissements de variable non utilisée
    (void)physical_addr;
    // TODO: Implémenter la logique de mappage.
    // Exemple simplifié pour une adresse dans les 4 premiers Mo (déjà mappés)
    // uint32_t pd_index = (uint32_t)virtual_addr >> 22;
    // uint32_t pt_index = ((uint32_t)virtual_addr >> 12) & 0x03FF;
    //
    // uint32_t* page_table_entry_addr = (uint32_t*)( (kernel_page_directory[pd_index] & ~0xFFF) + (pt_index * 4) );
    // *page_table_entry_addr = ((uint32_t)physical_addr) | PAGE_PRESENT | PAGE_READ_WRITE;
    // asm volatile("invlpg (%0)" ::"r" (virtual_addr) : "memory");
}
