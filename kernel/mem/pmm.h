#ifndef PMM_H
#define PMM_H

#include <stdint.h>
#include <stddef.h> // Pour size_t et NULL

void pmm_init(uint32_t memory_size, uint32_t kernel_end_address, uint32_t multiboot_addr);
void* pmm_alloc_page();
void pmm_free_page(void* page);
uint32_t pmm_get_total_pages();
uint32_t pmm_get_used_pages();

#endif
