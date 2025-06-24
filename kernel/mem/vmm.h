#ifndef VMM_H
#define VMM_H

#include <stdint.h>

// Initialise le Virtual Memory Manager et active le paging.
void vmm_init();

// Mappe une page virtuelle à une page physique avec les flags donnés.
// Pour simplifier, les flags ne sont pas passés ici mais codés en dur dans vmm.c
// Une version complète prendrait des flags.
void vmm_map_page(void* virtual_addr, void* physical_addr);

// (Optionnel) Dé-mappe une page virtuelle.
// void vmm_unmap_page(void* virtual_addr);

#endif
