#include "pmm.h"
#include <stddef.h> // Pour NULL si ce n'est pas déjà dans pmm.h ou un autre include.
#include <stdint.h> // Pour uint32_t.

#define PAGE_SIZE 4096 // Taille d'une page en octets (4KiB).
// Pointeur vers le bitmap de la mémoire. Chaque bit représente une page.
// 0x10000 (64KiB) est un emplacement arbitraire. Il faut s'assurer que cette zone est disponible.
// Dans un système plus robuste, cet emplacement serait déterminé dynamiquement ou réservé par le linker.
static uint32_t* memory_map_ptr = (uint32_t*)0x10000;
static uint32_t total_pages;  // Nombre total de pages physiques gérables.
static uint32_t used_pages;   // Nombre de pages actuellement allouées.

// Marque une page comme utilisée (allouée) dans le bitmap.
// page_num: Le numéro de la page à marquer.
void pmm_set_page(uint32_t page_num) {
    if (page_num >= total_pages) return; // Vérification des limites.
    // Trouve le bon dword dans le bitmap et met le bit correspondant à 1.
    memory_map_ptr[page_num / 32] |= (1 << (page_num % 32));
}

// Marque une page comme libre dans le bitmap.
// page_num: Le numéro de la page à libérer.
void pmm_clear_page(uint32_t page_num) {
    if (page_num >= total_pages) return; // Vérification des limites.
    // Trouve le bon dword et met le bit correspondant à 0.
    memory_map_ptr[page_num / 32] &= ~(1 << (page_num % 32));
}

// Vérifie si une page est marquée comme utilisée (allouée).
// page_num: Le numéro de la page à vérifier.
// Retourne: 1 si la page est utilisée, 0 sinon.
int pmm_is_page_used(uint32_t page_num) {
    if (page_num >= total_pages) return 1; // Hors limites, considérer comme "utilisée" pour éviter des erreurs.
    return (memory_map_ptr[page_num / 32] & (1 << (page_num % 32))) != 0;
}

// Initialise le gestionnaire de mémoire physique (PMM).
// memory_size: Taille totale de la mémoire physique disponible en octets.
// kernel_end_address: Adresse de la fin du noyau chargé en mémoire. (Non utilisé pour l'instant)
// multiboot_addr: Adresse de la structure d'information Multiboot. (Non utilisé pour l'instant)
// Pour l'instant, kernel_end_address et multiboot_addr sont ignorés pour simplifier.
// Une implémentation complète les utiliserait pour marquer précisément les zones réservées.
void pmm_init(uint32_t memory_size, uint32_t kernel_end_address __attribute__((unused)), uint32_t multiboot_addr __attribute__((unused))) {
    // Les paramètres kernel_end_address et multiboot_addr sont marqués comme non utilisés
    // pour éviter les avertissements du compilateur. Ils seront utiles pour une initialisation
    // plus précise du PMM à l'avenir, en utilisant la carte mémoire fournie par Multiboot
    // pour identifier les zones utilisables et réservées.

    total_pages = memory_size / PAGE_SIZE; // Calculer le nombre total de pages.
    used_pages = 0; // Initialiser le compteur de pages utilisées.

    // Initialiser le bitmap : marquer toutes les pages comme libres au début.
    // La taille du bitmap en nombre de dwords (uint32_t) est total_pages / 32.
    uint32_t bitmap_size_in_dwords = total_pages / 32;
    if (total_pages % 32 != 0) { // S'il y a un reste, ajouter un dword pour couvrir les pages restantes.
        bitmap_size_in_dwords++;
    }
    for (uint32_t i = 0; i < bitmap_size_in_dwords; i++) {
        memory_map_ptr[i] = 0; // Mettre tous les bits à 0 (toutes les pages sont libres).
    }

    // Marquer les zones initiales comme utilisées.
    // Ceci inclut typiquement :
    // - La table des vecteurs d'interruption (IVT) et la zone de données du BIOS (BDA) (première page).
    // - La zone où le bitmap lui-même est stocké.
    // - La zone occupée par le code et les données du noyau.
    // Une initialisation plus complète utiliserait les informations de Multiboot pour cela.
    // Pour l'instant, de manière simplifiée, marquons les premiers 4 Mo comme utilisés
    // pour couvrir le noyau, la mémoire VGA, le bitmap PMM lui-même, etc.
    // C'est une simplification grossière ; un vrai PMM analyserait la carte mémoire de Multiboot.
    uint32_t pages_for_first_4mb = (4 * 1024 * 1024) / PAGE_SIZE;
    for (uint32_t i = 0; i < pages_for_first_4mb; i++) {
        if (i < total_pages && !pmm_is_page_used(i)) { // S'assurer de ne pas dépasser total_pages
             pmm_set_page(i);
             used_pages++;
        }
    }
}

// Alloue une page physique.
// Cherche la première page libre dans le bitmap, la marque comme utilisée, et retourne son adresse.
// Retourne: Un pointeur vers le début de la page allouée, ou NULL si aucune page n'est libre.
void* pmm_alloc_page() {
    for (uint32_t i = 0; i < total_pages; i++) { // Parcourir toutes les pages.
        if (!pmm_is_page_used(i)) { // Si la page i n'est pas utilisée.
            pmm_set_page(i); // Marquer la page comme utilisée.
            used_pages++;    // Incrémenter le compteur de pages utilisées.
            return (void*)(i * PAGE_SIZE); // Retourner l'adresse physique de la page.
        }
    }
    return NULL; // Aucune page libre trouvée.
}

// Libère une page physique précédemment allouée.
// page_addr: Pointeur vers le début de la page à libérer.
void pmm_free_page(void* page_addr) {
    if (page_addr == NULL) return; // Ne rien faire si l'adresse est NULL.
    uint32_t page_num = (uint32_t)((uintptr_t)page_addr / PAGE_SIZE); // Calculer le numéro de la page à partir de son adresse.
                                                                // Utilisation de uintptr_t pour la conversion de pointeur en entier.
    if (page_num >= total_pages) return; // Adresse invalide (hors des limites gérées).

    if (pmm_is_page_used(page_num)) { // Si la page était effectivement utilisée.
        pmm_clear_page(page_num); // Marquer la page comme libre.
        used_pages--;             // Décrémenter le compteur de pages utilisées.
    }
}

// Fonctions d'accès (getters) pour obtenir des informations sur l'état du PMM.
// (Ajoutées pour information, non demandées explicitement mais utiles pour le débogage ou les statistiques).
uint32_t pmm_get_total_pages() { return total_pages; }
uint32_t pmm_get_used_pages() { return used_pages; }
