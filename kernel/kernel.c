#include "idt.h"
#include "interrupts.h"
#include "keyboard.h"
#include "mem/pmm.h"
#include "mem/vmm.h"
#include "fs/initrd.h"
#include "task/task.h"
#include "timer.h"
#include "syscall/syscall.h" // Pour syscall_init()
#include "libc.h"            // Pour strcmp (si besoin ici, sinon implicite via autres .h)
#include <stdint.h>

// Pointeur vers la mémoire vidéo VGA. L'adresse 0xB8000 est standard.
volatile unsigned short* vga_buffer = (unsigned short*)0xB8000;
// Position actuelle du curseur
int vga_x = 0;
int vga_y = 0;
char current_color = 0x1F; // Default color

// Fonction pour afficher un caractère à une position donnée avec une couleur donnée
void print_char(char c, int x, int y, char color) {
    if (c == '\n') {
        vga_x = 0;
        vga_y++;
    } else if (c == '\b') {
        if (vga_x > 0) {
            vga_x--;
            vga_buffer[vga_y * 80 + vga_x] = (unsigned short)' ' | (unsigned short)color << 8;
        } else if (vga_y > 0) {
            vga_y--;
            vga_x = 79;
            vga_buffer[vga_y * 80 + vga_x] = (unsigned short)' ' | (unsigned short)color << 8;
        }
    } else {
        vga_buffer[y * 80 + x] = (unsigned short)c | (unsigned short)color << 8;
        vga_x++;
    }

    if (vga_x >= 80) {
        vga_x = 0;
        vga_y++;
    }
    if (vga_y >= 25) {
        // Basic scroll
        for (int i = 0; i < 24 * 80; i++) {
            vga_buffer[i] = vga_buffer[i + 80];
        }
        for (int i = 24 * 80; i < 25 * 80; i++) {
            vga_buffer[i] = (unsigned short)' ' | (unsigned short)color << 8;
        }
        vga_x = 0; // Cursor to start of the scrolled line (which is now the last line)
        vga_y = 24;
    }
}

// Fonction pour afficher une chaîne de caractères
void print_string(const char* str, char color) {
    for (int i = 0; str[i] != '\0'; i++) {
        print_char(str[i], vga_x, vga_y, color);
    }
}

void clear_screen(char color) {
    for (int y = 0; y < 25; y++) {
        for (int x = 0; x < 80; x++) {
            vga_buffer[y * 80 + x] = (unsigned short)' ' | (unsigned short)color << 8;
        }
    }
    vga_x = 0;
    vga_y = 0;
}

// La fonction principale de notre noyau
// physical_pd_addr is the physical address of boot_page_directory from boot.s
void kmain(uint32_t physical_pd_addr) {
    current_color = 0x1F; // Texte blanc sur fond bleu
    clear_screen(current_color);
    // EARLY PRINT FOR DEBUGGING
    print_string("KMAIN_CALLED_EARLY_DEBUG\n", 0x0A); // Green on Black for distinction

    // Re-assert our page directory from _start, in case SMM or something changed CR3.
    // Also ensure paging is enabled.
    asm volatile (
        "mov %0, %%eax\n\t"
        "mov %%eax, %%cr3"
        : : "r"(physical_pd_addr) : "eax");

    uint32_t temp_cr0;
    asm volatile("mov %%cr0, %0" : "=r"(temp_cr0));
    if (!(temp_cr0 & 0x80000000)) { // If PG bit is not set
        temp_cr0 |= 0x80000000;      // Set PG bit
        asm volatile("mov %0, %%cr0" : : "r"(temp_cr0));
    }
    // Add a print statement to confirm. Need itoa for physical_pd_addr.
    // For now, just a generic message.
    // print_string("CR3 re-asserted in kmain.\n", 0x0B); // Cyan on Black
    // More detailed printing:
    print_string("kmain: physical_pd_addr (intended CR3 from boot.s) = ", 0x0B);
    print_hex(physical_pd_addr, 0x0B);
    print_string("\n", 0x0B);

    uint32_t current_cr3_val;
    asm volatile("mov %%cr3, %0" : "=r"(current_cr3_val));
    print_string("kmain: CR3 read back after set = ", 0x0B);
    print_hex(current_cr3_val, 0x0B);
    print_string("\n", 0x0B);

    // Placeholder pour la structure Multiboot et l'adresse de fin du noyau.
    // uint32_t multiboot_magic = 0x2BADB002; // Non utilisé actuellement
    uint32_t multiboot_addr = 0; // Non utilisé par pmm_init dans sa forme actuelle
    uint32_t kernel_end_addr = 0; // TODO: Obtenir cette valeur depuis le linker script, non utilisé par pmm_init actuellement

    // Debug CR3 before PMM
    asm volatile("mov %%cr3, %0" : "=r"(current_cr3_val));
    print_string("kmain: CR3 before pmm_init = ", 0x0B);
    print_hex(current_cr3_val, 0x0B);
    print_string("\n", 0x0B);

    // Initialiser la mémoire physique et virtuelle
    uint32_t total_memory_bytes = 16 * 1024 * 1024; // Supposition pour l'instant
    pmm_init(total_memory_bytes, kernel_end_addr, multiboot_addr);

    // Debug CR3 before VMM
    asm volatile("mov %%cr3, %0" : "=r"(current_cr3_val));
    print_string("kmain: CR3 before vmm_init = ", 0x0B);
    print_hex(current_cr3_val, 0x0B);
    print_string("\n", 0x0B);

    vmm_init(); // Active le paging
    print_string("Gestionnaires PMM et VMM initialises.\n", current_color);

    // Initialiser l'initrd
    // Idéalement, keyboard.c utilise les globales vga_x, vga_y, current_color directement.
    // init_vga_kb(vga_x, vga_y, current_color); // Supprimé car keyboard.c a été modifié

    // Placeholder pour la structure Multiboot et l'adresse de fin du noyau.
    // uint32_t multiboot_magic = 0x2BADB002; // Non utilisé actuellement
    uint32_t multiboot_addr = 0; // Non utilisé par pmm_init dans sa forme actuelle
    uint32_t kernel_end_addr = 0; // TODO: Obtenir cette valeur depuis le linker script, non utilisé par pmm_init actuellement

    // Initialiser la mémoire physique et virtuelle
    uint32_t total_memory_bytes = 16 * 1024 * 1024; // Supposition pour l'instant
    pmm_init(total_memory_bytes, kernel_end_addr, multiboot_addr);
    vmm_init(); // Active le paging
    print_string("Gestionnaires PMM et VMM initialises.\n", current_color);

    // Initialiser l'initrd
    // TODO: Obtenir initrd_location depuis Multiboot
    uint32_t initrd_location = 0x200000; // Adresse codée en dur pour l'instant
    if (initrd_location != 0) {
        initrd_init(initrd_location);
        print_string("Initrd initialise. Contenu:\n", current_color);
        initrd_list_files(); // Optionnel: lister les fichiers pour le debug
    } else {
        print_string("Initrd non trouve. Arret.\n", 0x0C);
        while(1) asm volatile("cli; hlt");
    }

    // Initialiser les interruptions et les appels système
    idt_init();
    interrupts_init();  // Configure le PIC, active les IRQ de base
    syscall_init();     // Enregistre le handler pour int 0x80
    print_string("IDT, PIC et Syscalls initialises.\n", current_color);

    // Initialiser le multitâche
    tasking_init(); // Crée la tâche noyau initiale (idle task)
    print_string("Multitache initialise.\n", current_color);

    // Lancer le shell utilisateur
    print_string("Lancement du shell...\n", current_color);
    char* shell_argv[] = {"shell.bin", NULL}; // argv pour le shell
    int shell_pid = create_user_process("shell.bin", shell_argv);

    if (shell_pid < 0) {
        print_string("Echec du lancement de shell.bin. Arret.\n", 0x0C);
        while(1) asm volatile("cli; hlt");
    } else {
        print_string("shell.bin lance avec PID: ", current_color);
        // TODO: Fonction itoa pour afficher shell_pid
        print_string("[PID affichage non implemente]\n", current_color);
    }

    // Initialiser et démarrer le timer système pour permettre le scheduling
    timer_init(100); // 100 Hz
    print_string("Timer systeme active a 100Hz.\n", current_color);

    // Activer les interruptions globalement (si ce n'est pas déjà fait dans timer_init ou interrupts_init)
    // `interrupts_init` devrait déjà avoir fait `sti`.
    // `timer_init` démarre les IRQ0 qui déclencheront le scheduler.

    print_string("Systeme AI-OS operationnel. Passage au mode idle.\n", current_color);

    // La tâche noyau initiale (kmain) devient la tâche "idle".
    // Elle ne fait rien d'autre qu'attendre les interruptions.
    while(1) {
        asm volatile("hlt");
    }
}

// La définition de strcmp est maintenant dans kernel/libc.c
// et devrait être liée automatiquement.
