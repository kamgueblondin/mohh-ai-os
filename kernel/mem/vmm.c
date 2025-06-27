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

// Mappe une page virtuelle à une page physique avec les flags donnés.
// Pour l'instant, cela fonctionne sur le kernel_page_directory global.
void vmm_map_page(void* virtual_addr, void* physical_addr, uint32_t flags) {
    uint32_t virt_addr_val = (uint32_t)virtual_addr;
    uint32_t pd_index = virt_addr_val >> 22; // Index dans le Page Directory
    uint32_t pt_index = (virt_addr_val >> 12) & 0x03FF; // Index dans la Page Table

    // Vérifier si l'entrée du Page Directory pour cette table de pages existe
    uint32_t pde = kernel_page_directory[pd_index];
    uint32_t* page_table_virt_addr;

    if (!(pde & PAGE_PRESENT)) {
        // La table de pages n'existe pas, il faut l'allouer
        void* new_pt_phys_addr = pmm_alloc_page();
        if (!new_pt_phys_addr) {
            // Échec de l'allocation de la table de pages. C'est critique.
            // On ne peut pas continuer car on ne peut pas mapper la mémoire demandée.
            // Pour l'instant, on va imprimer un message et boucler indéfiniment.
            // Une vraie gestion d'erreur pourrait tenter de libérer de la mémoire ou afficher un "kernel panic".
            // extern void print_string(const char* str, char color); // Assurez-vous que c'est disponible
            // print_string("VMM Error: Failed to allocate physical page for new page table. System Halted.\n", 0x0C);
            // TODO: Need a kpanic function here.
            volatile unsigned short* vga = (unsigned short*)0xB8000;
            const char* error_msg = "VMM PMM PT ALLOC FAIL";
            for(int k=0; error_msg[k] != '\0'; ++k) vga[k] = (vga[k] & 0xFF00) | error_msg[k];
            asm volatile("cli; hlt");
            return; // Ne devrait pas être atteint
        }
        // Initialiser la nouvelle table de pages à zéro (toutes les entrées non présentes)
        // L'adresse physique est retournée par pmm_alloc_page.
        // Pour y accéder (écrire), nous avons besoin d'une adresse virtuelle.
        // Pour l'instant, on suppose un mappage identité pour le noyau ou un accès direct.
        // Si le noyau est mappé haut (par exemple), il faut une astuce pour accéder à new_pt_phys_addr.
        // Supposons que new_pt_phys_addr est accessible directement pour l'écriture.
        // Ou, si le paging est déjà actif et que les adresses physiques ne sont pas directement mappées,
        // il faudrait temporairement mapper cette page physique pour l'initialiser.
        // Pour la simplicité (et parce que les 4 premiers Mo sont mappés 1:1),
        // on suppose qu'on peut écrire à l'adresse physique si elle est < 4MB.
        // Une solution plus robuste serait d'avoir une zone de mappage temporaire.
        page_table_virt_addr = (uint32_t*)new_pt_phys_addr; // ATTENTION: Ceci est une simplification.
                                                            // Si new_pt_phys_addr > VMM_KERNEL_MAX_PHYS_MAPPED_ADDR,
                                                            // il faut le mapper temporairement.
                                                            // Pour l'instant, on suppose que pmm_alloc_page() retourne des adresses < 4MB
                                                            // ou que le noyau a un moyen d'accéder à toute la RAM physique.

        for (int i = 0; i < 1024; i++) {
            page_table_virt_addr[i] = 0; // Marquer toutes les entrées comme non présentes initialement.
                                         // Ou avec PAGE_READ_WRITE pour simplifier les flags futurs.
        }

        // Mettre à jour l'entrée du Page Directory
        // PAGE_USER_SUPERVISOR pour la PDE doit permettre l'accès utilisateur si les PTE le permettent.
        kernel_page_directory[pd_index] = (uint32_t)new_pt_phys_addr | PAGE_PRESENT | PAGE_READ_WRITE | PAGE_USER_SUPERVISOR;
    } else {
        // La table de pages existe déjà. Récupérer son adresse virtuelle.
        // L'adresse dans la PDE est physique.
        // Encore une fois, simplification pour l'accès.
        page_table_virt_addr = (uint32_t*)(pde & ~0xFFF); // Masquer les flags pour obtenir l'adresse physique de la PT
                                                          // et la traiter comme virtuelle (simplification)
    }

    // Mettre à jour l'entrée de la Page Table
    page_table_virt_addr[pt_index] = ((uint32_t)physical_addr & ~0xFFF) | flags;

    // Invalider l'entrée TLB pour cette adresse virtuelle
    // car le mappage a peut-être changé.
    asm volatile("invlpg (%0)" ::"r" (virtual_addr) : "memory");
}

// Wrapper pour vmm_map_page qui utilise les flags par défaut pour le noyau (Kernel R/W)
void vmm_map_kernel_page(void* virtual_addr, void* physical_addr) {
    vmm_map_page(virtual_addr, physical_addr, PAGE_PRESENT | PAGE_READ_WRITE);
}

// Wrapper pour vmm_map_page qui utilise les flags par défaut pour l'utilisateur (User R/W)
void vmm_map_user_page(void* virtual_addr, void* physical_addr) {
    vmm_map_page(virtual_addr, physical_addr, PAGE_PRESENT | PAGE_READ_WRITE | PAGE_USER_SUPERVISOR);
}
