#include "idt.h"
#include "interrupts.h"
#include "keyboard.h"
#include "mem/pmm.h"    // NOUVEAU
#include "mem/vmm.h"    // NOUVEAU
#include "fs/initrd.h"  // NOUVEAU
#include <stdint.h>     // Pour uint32_t

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
void kmain(void) {
    // Couleur : texte blanc (0xF) sur fond bleu (0x1) -> 0x1F
    current_color = 0x1F;

    clear_screen(current_color);

    // Initialize VGA for keyboard.c if its separate VGA variables are used
    // This syncs the cursor position and color.
    // init_vga_kb(vga_x, vga_y, current_color); // Defined in keyboard.c

    print_string("Bienvenue dans AI-OS !\nEntrez du texte :\n", current_color);
    // After printing, the vga_x, vga_y in this file are updated.
    // We need to ensure keyboard.c uses these, or we pass them.
    // The current keyboard.c has its own vga_x_kb etc.
    // For them to match, we can call init_vga_kb again.
    // This is still a hack due to duplicated VGA logic.
    init_vga_kb(vga_x, vga_y, current_color);

    // Placeholder pour la structure Multiboot et l'adresse de fin du noyau.
    // Ces valeurs seraient normalement passées par le bootloader (boot.s).
    uint32_t multiboot_magic = 0x2BADB002; // Supposons que nous avons le bon magic
    uint32_t multiboot_addr = 0; // Adresse de la structure Multiboot (supposons 0 si non fournie pour ce test simple)
                                 // Dans un vrai scénario, boot.s mettrait l'adresse de la struct multiboot
                                 // dans un registre (ex: ebx) et kmain la recevrait.
    uint32_t kernel_end_addr = 0; // Placeholder, devrait être la fin du .bss du noyau.
                                  // Un vrai PMM a besoin de ça pour ne pas écraser le noyau avec son bitmap.
                                  // Pour l'instant, pmm_init a été simplifié.

    // Étape 3 : Initialiser la mémoire
    // Taille mémoire arbitraire (ex: 16MB), car nous ne lisons pas encore Multiboot.
    uint32_t total_memory_bytes = 16 * 1024 * 1024;
    // pmm_init(total_memory_bytes, kernel_end_addr, multiboot_addr); // Signature pmm_init modifiée
    pmm_init(total_memory_bytes); // Utilisation de la version simplifiée de pmm_init demandée initialement
    vmm_init(); // Active le paging
    print_string("Gestionnaire de memoire initialise.\n", current_color);

    // Étape 4 : Initialiser l'initrd
    // Adresse arbitraire pour l'initrd. Dans un vrai système, cette info viendrait de Multiboot.
    // QEMU avec `-initrd` charge le fichier, le bootloader (GRUB ou le nôtre) fournirait l'adresse.
    // Assurons-nous que cette adresse est mappée après vmm_init().
    // Les 4 premiers Mo sont mappés en identité. Choisissons une adresse dans cette plage,
    // par exemple 2MB (0x200000), en s'assurant qu'elle n'écrase pas le noyau ou le bitmap PMM (à 0x10000).
    uint32_t initrd_location = 0x200000; // ADRESSE PROVISOIRE POUR L'INITRD

    // Simuler la recherche de l'initrd via Multiboot (sera implémenté plus tard)
    // uint32_t initrd_location = find_initrd_location_from_multiboot(multiboot_addr);
    if (initrd_location != 0) { // Simule la trouvaille d'un initrd (et multiboot_magic ==EXPECTED_MAGIC)
        print_string("Initrd trouve ! Fichiers:\n", current_color);
        initrd_init(initrd_location);
        initrd_list_files();

        // Test de lecture de fichier (optionnel)
        /*
        uint32_t test_file_size = 0;
        char* test_content = initrd_read_file("./test.txt", &test_file_size);
        if (test_content) {
            print_string("Contenu de ./test.txt (taille: ", current_color);
            // char size_str[10]; itoa(test_file_size, size_str, 10); print_string(size_str, current_color);
            print_string("):\n", current_color);
            // Attention: test_content n'est pas NUL-terminé par initrd_read_file tel quel.
            // Il faudrait soit le copier et ajouter NUL, soit l'afficher char par char jusqu'à test_file_size.
            for(uint32_t k=0; k < test_file_size && k < 70; ++k) { // Limiter l'affichage
                 print_char(test_content[k], vga_x, vga_y, current_color);
            }
            print_string("\n", current_color);
        } else {
            print_string("Impossible de lire ./test.txt depuis initrd.\n", current_color);
        }
        */

    } else {
        print_string("Initrd non trouve.\n", current_color);
    }

    // Initialisation des interruptions (après la mémoire pour que les handlers puissent être en mémoire paginée si besoin)
    idt_init();         // Initialise la table des interruptions
    interrupts_init();  // Initialise le PIC et active les interruptions (sti)

    print_string("Systeme AI-OS operationnel. En attente d'interruptions...\n", current_color);

    // Le CPU attendra passivement une interruption au lieu de tourner en boucle
    while(1) {
        asm volatile("hlt");
    }
}

// Définition de strcmp si KERNEL_STRCMP_DEFINED n'est pas activé dans initrd.c
// Il est préférable de l'avoir en un seul endroit, par exemple un util.c
#ifndef KERNEL_STRCMP_DEFINED
#define KERNEL_STRCMP_DEFINED
int strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}
#endif
