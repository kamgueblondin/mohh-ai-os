#include "elf.h"
#include "mem/pmm.h" // For pmm_alloc_page
#include "mem/vmm.h" // For vmm_map_user_page
#include <stddef.h>  // For NULL
#include <stdint.h>  // For uint8_t, uint32_t
// #include "kernel/kernel.h" // For print_char, print_string (temporarily for debugging)
// Les fonctions print_string pour le debug dans ce fichier sont commentées.
// Si elles sont décommentées, il faudra déclarer :
// extern void print_string(const char* str); ou similaire.

// Basic memory copy function (memcpy)
// Could be moved to a common string/memory utility header later
static void* memcpy(void* dest, const void* src, size_t count) {
    char* dest_char = (char*)dest;
    const char* src_char = (const char*)src;
    for (size_t i = 0; i < count; i++) {
        dest_char[i] = src_char[i];
    }
    return dest;
}

// Basic memory set function (memset)
// Could be moved to a common string/memory utility header later
static void* memset(void* dest, int val, size_t count) {
    unsigned char* ptr = (unsigned char*)dest;
    while (count-- > 0) {
        *ptr++ = (unsigned char)val;
    }
    return dest;
}


uint32_t elf_load(uint8_t* elf_data) {
    if (elf_data == NULL) {
        // print_string("elf_load: elf_data is NULL\n"); // Temporary debug
        return 0; // No data to load
    }

    Elf32_Ehdr* header = (Elf32_Ehdr*)elf_data;

    // 1. Vérifier le "magic number" et autres informations ELF de base
    if (header->e_ident[EI_MAG0] != ELFMAG0 ||
        header->e_ident[EI_MAG1] != ELFMAG1 ||
        header->e_ident[EI_MAG2] != ELFMAG2 ||
        header->e_ident[EI_MAG3] != ELFMAG3) {
        // print_string("elf_load: Invalid ELF magic number\n"); // Temporary debug
        return 0; // Not an ELF file
    }

    if (header->e_ident[EI_CLASS] != ELFCLASS32) {
        // print_string("elf_load: Not a 32-bit ELF file\n"); // Temporary debug
        return 0; // Not a 32-bit ELF
    }

    if (header->e_ident[EI_DATA] != ELFDATA2LSB) {
        // print_string("elf_load: Not little-endian\n"); // Temporary debug
        return 0; // Not little-endian (for x86)
    }

    if (header->e_type != ET_EXEC) {
        // print_string("elf_load: Not an executable file\n"); // Temporary debug
        return 0; // Not an executable
    }

    if (header->e_machine != EM_386) {
        // print_string("elf_load: Not for i386 architecture\n"); // Temporary debug
        return 0; // Not for i386
    }

    // 2. Parcourir les "Program Headers"
    Elf32_Phdr* phdrs = (Elf32_Phdr*)(elf_data + header->e_phoff);

    for (int i = 0; i < header->e_phnum; i++) {
        Elf32_Phdr* phdr = &phdrs[i];

        if (phdr->p_type == PT_LOAD) {
            // 3. Pour chaque segment de type "LOAD":
            //    a. Allouer de la mémoire physique
            //    b. Mapper cette mémoire physique à l'adresse virtuelle demandée
            //    c. Copier les données du segment

            // print_string("elf_load: Loading segment at vaddr=0x"); // Temporary debug
            // char vaddr_str[9]; int_to_hex_str(phdr->p_vaddr, vaddr_str); print_string(vaddr_str); // Temporary debug
            // print_string(" size=0x"); // Temporary debug
            // char memsz_str[9]; int_to_hex_str(phdr->p_memsz, memsz_str); print_string(memsz_str); // Temporary debug
            // print_string("\n"); // Temporary debug


            // L'adresse virtuelle où le segment doit être chargé
            uint32_t virt_addr = phdr->p_vaddr;
            // La taille du segment en mémoire
            uint32_t mem_size = phdr->p_memsz;
            // La taille du segment dans le fichier
            uint32_t file_size = phdr->p_filesz;
            // L'offset des données du segment dans le fichier ELF
            uint8_t* segment_data_in_elf = elf_data + phdr->p_offset;

            if (mem_size == 0) {
                continue; // Segment vide, rien à faire
            }

            // Allouer et mapper les pages nécessaires pour ce segment
            // Note: p_vaddr et p_offset ne sont pas nécessairement alignés sur PAGE_SIZE
            // Nous devons mapper les pages couvrant [virt_addr, virt_addr + mem_size)

            uint32_t first_page_addr = virt_addr & ~(PAGE_SIZE - 1);
            uint32_t last_addr = virt_addr + mem_size -1; // adresse du dernier byte du segment
            uint32_t last_page_addr = last_addr & ~(PAGE_SIZE - 1);

            for (uint32_t page_v_addr = first_page_addr; page_v_addr <= last_page_addr; page_v_addr += PAGE_SIZE) {
                void* phys_page = pmm_alloc_page();
                if (!phys_page) {
                    // print_string("elf_load: Failed to allocate physical page for vaddr 0x"); // Temporary debug
                    // char page_v_addr_str[9]; int_to_hex_str(page_v_addr, page_v_addr_str); print_string(page_v_addr_str); // Temporary debug
                    // print_string("\n"); // Temporary debug
                    // TODO: Unwind allocations made so far if this happens
                    return 0; // Out of memory
                }
                // IMPORTANT: vmm_map_page doit être capable de créer les tables de pages si elles n'existent pas
                // et de définir les flags USER et WRITABLE pour ces pages.
                // Utiliser vmm_map_user_page pour s'assurer que les flags sont corrects pour l'espace utilisateur.
                vmm_map_user_page((void*)page_v_addr, phys_page);
            }

            // Copier les données du fichier ELF vers la mémoire virtuelle mappée
            // p_filesz peut être plus petit que p_memsz (cas .bss)
            if (file_size > 0) {
                memcpy((void*)virt_addr, segment_data_in_elf, file_size);
            }

            // Si p_memsz > p_filesz, remplir le reste de la section avec des zéros (.bss)
            if (mem_size > file_size) {
                memset((void*)(virt_addr + file_size), 0, mem_size - file_size);
            }
        }
    }

    // 4. Retourner l'adresse du point d'entrée du programme
    // print_string("elf_load: Successfully loaded. Entry point: 0x"); // Temporary debug
    // char entry_str[9]; int_to_hex_str(header->e_entry, entry_str); print_string(entry_str); // Temporary debug
    // print_string("\n"); // Temporary debug

    return header->e_entry;
}
