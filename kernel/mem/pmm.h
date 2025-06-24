#ifndef PMM_H
#define PMM_H

#include <stdint.h>
#include <stddef.h> // Pour size_t et NULL

void pmm_init(uint32_t memory_size); // Signature simplifiée
void* pmm_alloc_page();
void pmm_free_page(void* page);
uint32_t pmm_get_total_pages(); // Ces fonctions nécessitent une implémentation si utilisées
uint32_t pmm_get_used_pages();  // Ces fonctions nécessitent une implémentation si utilisées

#endif
