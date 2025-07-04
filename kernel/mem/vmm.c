#include "vmm.h"
#include "pmm.h"    // Pour pmm_alloc_page si nous allouons dynamiquement les tables de pages.
#include <stddef.h> // Pour NULL.
#include <stdint.h> // Pour uint32_t.

// Le répertoire de pages du noyau (Kernel Page Directory).
// Doit être aligné sur une frontière de 4KB (PAGE_SIZE).
// Pour un premier noyau simple, nous pouvons l'allouer statiquement.
// Dans un système plus complexe, il pourrait être alloué dynamiquement depuis la mémoire physique.
__attribute__((aligned(4096))) uint32_t kernel_page_directory[1024];

// Une première table de pages (Page Table) pour mapper les 4 premiers Mo de la mémoire physique.
// Également alignée sur 4KB.
__attribute__((aligned(4096))) uint32_t first_page_table[1024];

// Une deuxième table de pages pour mapper les 4 Mo suivants (de 4Mo à 8Mo).
__attribute__((aligned(4096))) uint32_t second_page_table[1024];


// Fonctions assembleur externes (définies dans paging.s ou similaire)
// pour charger le répertoire de pages dans CR3 et activer la pagination.
extern void load_page_directory(uint32_t* page_directory_physical_addr);
extern void enable_paging();

// Drapeaux (flags) pour les entrées du Répertoire de Pages (PDE) et de la Table de Pages (PTE).
#define PAGE_PRESENT         0x1 // Bit 0: Page présente en mémoire.
#define PAGE_READ_WRITE      0x2 // Bit 1: Lecture/Écriture (1) ou Lecture Seule (0).
#define PAGE_USER_SUPERVISOR 0x4 // Bit 2: Utilisateur (1) ou Superviseur (0).
                                 // Si 0 (Superviseur), seul le code en ring 0-2 peut y accéder.
                                 // Si 1 (Utilisateur), le code en ring 3 peut aussi y accéder (si les autres permissions le permettent).

// Initialise le gestionnaire de mémoire virtuelle (VMM).
void vmm_init() {
    // 1. Initialiser la première table de pages (first_page_table).
    //    Cette table va mapper les 4 premiers Mo de la mémoire physique (0x00000000 - 0x003FFFFF)
    //    de manière identitaire (adresse virtuelle = adresse physique).
    for (int i = 0; i < 1024; i++) {
        // Adresse physique de la page = i * 4KB (0x1000).
        // Drapeaux : Présent (1), Lecture/Écriture (1), Superviseur (0) -> 0b00000011 = 0x3.
        // Le bit User/Supervisor est à 0 pour le noyau.
        first_page_table[i] = (i * 0x1000) | (PAGE_PRESENT | PAGE_READ_WRITE);
    }

    // 2. Initialiser le répertoire de pages du noyau (kernel_page_directory).
    //    La première entrée (index 0) du répertoire de pages pointera vers `first_page_table`.
    //    Cela couvre les adresses virtuelles de 0x00000000 à 0x003FFFFF (les 4 premiers Mo).
    //    L'adresse stockée dans la PDE doit être l'adresse physique de la table de pages.
    //    Drapeaux : Présent, Lecture/Écriture, Superviseur.
    kernel_page_directory[0] = ((uint32_t)(uintptr_t)&first_page_table) | (PAGE_PRESENT | PAGE_READ_WRITE);

    // 2b. Initialiser la deuxième table de pages (second_page_table).
    //     Elle va mapper les 4 Mo suivants de la mémoire physique (0x00400000 - 0x007FFFFF)
    //     de manière identitaire. Utile si le noyau ou des données initiales dépassent 4Mo.
    for (int i = 0; i < 1024; i++) {
        // Adresse physique de la page = 0x400000 + (i * 0x1000).
        second_page_table[i] = (0x400000 + (i * 0x1000)) | (PAGE_PRESENT | PAGE_READ_WRITE);
    }
    // La deuxième entrée (index 1) du répertoire de pages pointera vers `second_page_table`.
    // Cela couvre les adresses virtuelles de 0x00400000 à 0x007FFFFF.
    kernel_page_directory[1] = ((uint32_t)(uintptr_t)&second_page_table) | (PAGE_PRESENT | PAGE_READ_WRITE);


    // Les autres 1022 entrées du répertoire de pages ne sont pas utilisées pour l'instant.
    // Elles devraient être marquées comme non présentes (bit 0 à 0).
    for (int i = 2; i < 1024; i++) { // Commence à l'index 2 maintenant.
        kernel_page_directory[i] = 0; // Une entrée à 0 signifie non présente.
    }

    // 3. Charger l'adresse physique du répertoire de pages dans le registre CR3.
    //    Note : `load_page_directory` s'attend à une adresse physique.
    //    Si la pagination n'est pas encore active, l'adresse de `&kernel_page_directory` (obtenue avec un cast)
    //    est effectivement une adresse physique car le segment de code s'exécute avec un mappage identité implicite.
    load_page_directory((uint32_t*)(uintptr_t)&kernel_page_directory);

    // 4. Activer la pagination.
    //    Cela se fait en mettant à 1 le bit PG (bit 31) du registre CR0.
    enable_paging();

    // À partir de ce point, la pagination est active !
    // Toutes les adresses utilisées par le CPU (sauf celles spécifiquement gérées par les segments,
    // ce qui est rare en mode protégé 32 bits une fois la pagination active) sont des adresses virtuelles.
}

// Mappe une page virtuelle à une page physique avec les drapeaux donnés.
// Pour l'instant, cette fonction opère sur le `kernel_page_directory` global.
// virtual_addr: L'adresse virtuelle à mapper.
// physical_addr: L'adresse physique vers laquelle mapper.
// flags: Les drapeaux pour la PTE (ex: PAGE_PRESENT | PAGE_READ_WRITE | PAGE_USER_SUPERVISOR).
void vmm_map_page(void* virtual_addr, void* physical_addr, uint32_t flags) {
    uint32_t virt_addr_val = (uint32_t)(uintptr_t)virtual_addr;
    uint32_t pd_index = virt_addr_val >> 22;             // Index dans le Répertoire de Pages (les 10 bits de poids fort).
    uint32_t pt_index = (virt_addr_val >> 12) & 0x03FF;  // Index dans la Table de Pages (les 10 bits du milieu).

    // Vérifier si l'entrée du Répertoire de Pages (PDE) pour cette table de pages existe.
    uint32_t pde = kernel_page_directory[pd_index];
    uint32_t* page_table_virt_addr; // Adresse virtuelle de la table de pages.

    if (!(pde & PAGE_PRESENT)) {
        // La table de pages n'existe pas, il faut l'allouer.
        void* new_pt_phys_addr_ptr = pmm_alloc_page(); // Allouer une page physique pour la nouvelle table.
        if (!new_pt_phys_addr_ptr) {
            // Échec de l'allocation de la page physique pour la table de pages. C'est une erreur critique.
            // Le système ne peut pas continuer car la mémoire demandée ne peut pas être mappée.
            // Pour l'instant, affichons un message d'erreur direct sur l'écran VGA et arrêtons le système.
            // Une vraie gestion d'erreur pourrait tenter de libérer de la mémoire ou déclencher un "kernel panic" plus formel.
            // TODO: Implémenter une fonction kpanic() standard.
            volatile unsigned short* vga = (unsigned short*)0xB8000; // Adresse de la mémoire vidéo VGA.
            const char* error_msg = "VMM PMM PT ALLOC FAIL";
            for(int k=0; error_msg[k] != '\0'; ++k) vga[80*0+k] = (unsigned short)(error_msg[k] | (0x0C << 8)); // Rouge sur noir.
            asm volatile("cli; hlt"); // Désactiver les interruptions et arrêter le CPU.
            return; // Ne devrait jamais être atteint.
        }
        uint32_t new_pt_phys_addr = (uint32_t)(uintptr_t)new_pt_phys_addr_ptr;

        // Initialiser la nouvelle table de pages à zéro (toutes les entrées non présentes).
        // L'adresse `new_pt_phys_addr` est physique. Pour y écrire, nous avons besoin d'une adresse virtuelle.
        // Simplification : Puisque les 4-8 premiers Mo sont mappés 1:1 (virtuel=physique) par `first_page_table` et `second_page_table`,
        // nous pouvons supposer que si `new_pt_phys_addr` est dans cette plage, nous pouvons y accéder directement.
        // ATTENTION : C'est une simplification majeure. Si `pmm_alloc_page` retourne une adresse > 8Mo,
        // cette approche directe ne fonctionnera pas sans un mappage temporaire ou un accès physique spécial.
        // Une solution robuste nécessiterait une zone de mappage temporaire ou s'assurer que PMM alloue dans la zone mappée.
        page_table_virt_addr = (uint32_t*)new_pt_phys_addr; // Traiter l'adresse physique comme virtuelle (simplification).

        for (int i = 0; i < 1024; i++) {
            page_table_virt_addr[i] = 0; // Marquer toutes les entrées de la nouvelle table comme non présentes.
                                         // Ou initialiser avec PAGE_READ_WRITE pour simplifier la future application des drapeaux.
        }

        // Mettre à jour l'entrée du Répertoire de Pages (PDE).
        // Le drapeau PAGE_USER_SUPERVISOR pour la PDE doit permettre l'accès utilisateur si les PTE le permettent aussi.
        kernel_page_directory[pd_index] = new_pt_phys_addr | PAGE_PRESENT | PAGE_READ_WRITE | PAGE_USER_SUPERVISOR;
    } else {
        // La table de pages existe déjà. Récupérer son adresse (virtuelle, en supposant un mappage 1:1 pour le noyau).
        // L'adresse dans la PDE est physique (bits 31-12).
        // Encore une fois, simplification majeure pour l'accès direct.
        page_table_virt_addr = (uint32_t*)((uintptr_t)pde & ~0xFFF); // Masquer les drapeaux (bits 0-11) pour obtenir l'adresse physique de la PT,
                                                              // puis la traiter comme virtuelle (simplification).
    }

    // Mettre à jour l'entrée de la Table de Pages (PTE).
    // L'adresse physique de la page est stockée dans les bits 31-12. Les drapeaux sont dans les bits 0-11.
    page_table_virt_addr[pt_index] = ((uint32_t)(uintptr_t)physical_addr & ~0xFFF) | flags;

    // Invalider l'entrée du TLB (Translation Lookaside Buffer) pour cette adresse virtuelle,
    // car le mappage a peut-être changé. L'instruction `invlpg` fait cela.
    asm volatile("invlpg (%0)" ::"r" (virtual_addr) : "memory");
}

// Fonction d'enrobage (wrapper) pour `vmm_map_page` qui utilise les drapeaux par défaut pour le noyau (Superviseur, Lecture/Écriture).
void vmm_map_kernel_page(void* virtual_addr, void* physical_addr) {
    vmm_map_page(virtual_addr, physical_addr, PAGE_PRESENT | PAGE_READ_WRITE); // PAGE_USER_SUPERVISOR = 0 par défaut pour le noyau
}

// Fonction d'enrobage (wrapper) pour `vmm_map_page` qui utilise les drapeaux par défaut pour l'espace utilisateur (Utilisateur, Lecture/Écriture).
void vmm_map_user_page(void* virtual_addr, void* physical_addr) {
    vmm_map_page(virtual_addr, physical_addr, PAGE_PRESENT | PAGE_READ_WRITE | PAGE_USER_SUPERVISOR);
}
