#include "pmm.h"
#include <stddef.h> // Pour NULL si ce n'est pas déjà dans pmm.h

#define PAGE_SIZE 4096
static uint32_t* memory_map_ptr = (uint32_t*)0x10000; // Emplacement du bitmap (doit être géré avec soin)
                                                    // Cette adresse doit être disponible et ne pas écraser le noyau ou d'autres données.
                                                    // Dans une version plus avancée, cette adresse pourrait être déterminée dynamiquement.
static uint32_t total_pages;
static uint32_t used_pages;

// Marque une page comme utilisée dans le bitmap
void pmm_set_page(uint32_t page_num) {
    if (page_num >= total_pages) return; // Dépassement
    memory_map_ptr[page_num / 32] |= (1 << (page_num % 32));
}

// Marque une page comme libre dans le bitmap
void pmm_clear_page(uint32_t page_num) {
    if (page_num >= total_pages) return; // Dépassement
    memory_map_ptr[page_num / 32] &= ~(1 << (page_num % 32));
}

// Vérifie si une page est utilisée
int pmm_is_page_used(uint32_t page_num) {
    if (page_num >= total_pages) return 1; // Considérer comme utilisé si hors limites
    return (memory_map_ptr[page_num / 32] & (1 << (page_num % 32))) != 0;
}

// Initialise le gestionnaire de mémoire physique.
// memory_size: taille totale de la mémoire en octets.
// Pour l'instant, nous ignorerons kernel_end_address et multiboot_addr pour simplifier,
// comme dans la demande initiale qui ne les utilisait pas encore.
void pmm_init(uint32_t memory_size) {
    total_pages = memory_size / PAGE_SIZE;
    used_pages = 0; // On commence avec 0 pages utilisées, puis on marque celles qui le sont.

    // Initialement, marquer toutes les pages comme libres.
    // La taille du bitmap en uint32_t est total_pages / 32.
    uint32_t bitmap_size_in_dwords = total_pages / 32;
    if (total_pages % 32 != 0) { // S'il y a un reste, ajouter un dword de plus
        bitmap_size_in_dwords++;
    }
    for (uint32_t i = 0; i < bitmap_size_in_dwords; i++) {
        memory_map_ptr[i] = 0; // Toutes les pages sont libres
    }

    // Exemple: marquer la première page (contenant le vecteur IVT, BDA) comme utilisée.
    // Et la zone où le bitmap lui-même réside.
    // Et la zone du noyau.
    // Une initialisation plus complète utiliserait les infos de Multiboot.
    // Pour l'instant, nous allons marquer les 4 premiers Mo comme utilisés pour être sûr (noyau, VGA, etc.)
    // Cela simplifie, mais un vrai PMM analyserait la carte mémoire de Multiboot.
    uint32_t pages_for_first_4mb = (4 * 1024 * 1024) / PAGE_SIZE;
    for (uint32_t i = 0; i < pages_for_first_4mb; i++) {
        if (!pmm_is_page_used(i)) {
             pmm_set_page(i);
             used_pages++;
        }
    }
}

// Alloue une page physique.
void* pmm_alloc_page() {
    for (uint32_t i = 0; i < total_pages; i++) {
        if (!pmm_is_page_used(i)) {
            pmm_set_page(i);
            used_pages++;
            return (void*)(i * PAGE_SIZE);
        }
    }
    return NULL; // Plus de pages libres
}

// Libère une page physique.
void pmm_free_page(void* page_addr) {
    if (page_addr == NULL) return;
    uint32_t page_num = (uint32_t)page_addr / PAGE_SIZE;
    if (page_num >= total_pages) return; // Adresse invalide

    if (pmm_is_page_used(page_num)) {
        pmm_clear_page(page_num);
        used_pages--;
    }
}

// Getters (ajoutés pour info, non demandés explicitement mais utiles)
uint32_t pmm_get_total_pages() { return total_pages; }
uint32_t pmm_get_used_pages() { return used_pages; }
