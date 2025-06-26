#ifndef VMM_H
#define VMM_H

#include <stdint.h>

// Initialise le Virtual Memory Manager et active le paging.
void vmm_init();

// Flags pour les entrées de Page Directory et Page Table
#define PAGE_PRESENT         0x1     // Bit 0: Page présente en mémoire
#define PAGE_READ_WRITE      0x2     // Bit 1: Lecture/écriture (0 = lecture seule)
#define PAGE_USER_SUPERVISOR 0x4     // Bit 2: User/Supervisor (0 = supervisor, 1 = user)
// D'autres flags comme Accessed, Dirty, etc., peuvent être ajoutés ici.

// Mappe une page virtuelle à une page physique avec les flags spécifiés.
// `virtual_addr` et `physical_addr` doivent être alignés sur une page (4KB).
// `flags` est une combinaison des macros PAGE_* ci-dessus.
void vmm_map_page(void* virtual_addr, void* physical_addr, uint32_t flags);

// Raccourci pour mapper une page pour le noyau (Present, Read/Write, Supervisor).
void vmm_map_kernel_page(void* virtual_addr, void* physical_addr);

// Raccourci pour mapper une page pour l'espace utilisateur (Present, Read/Write, User).
void vmm_map_user_page(void* virtual_addr, void* physical_addr);

// (Optionnel) Dé-mappe une page virtuelle.
// void vmm_unmap_page(void* virtual_addr);

#endif // VMM_H
