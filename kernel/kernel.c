#include "idt.h"
#include "interrupts.h"
#include "keyboard.h"
#include "mem/pmm.h"
#include "mem/vmm.h"
// #include "fs/initrd.h" // Plus utilisé, les applications sont embarquées
#include "task/task.h"
#include "timer.h"
#include "syscall/syscall.h" // Pour syscall_init()
#include "libc.h"            // Pour strcmp (si besoin ici, sinon implicite via d'autres .h)
#include <stdint.h>

// Pointeur vers la mémoire vidéo VGA. L'adresse 0xB8000 est standard.
volatile unsigned short* vga_buffer = (unsigned short*)0xB8000;
// Position actuelle du curseur
int vga_x = 0;
int vga_y = 0;
char current_color = 0x1F; // Couleur par défaut

// Fonction pour afficher un caractère à une position donnée avec une couleur donnée
void print_char(char c, int x, int y, char color) {
    if (c == '\n') { // Si c'est un retour à la ligne
        vga_x = 0;   // Retour au début de la ligne
        vga_y++;     // Ligne suivante
    } else if (c == '\b') { // Si c'est un retour arrière (backspace)
        if (vga_x > 0) { // S'il y a des caractères sur la ligne actuelle
            vga_x--;     // Reculer d'une position
            vga_buffer[vga_y * 80 + vga_x] = (unsigned short)' ' | (unsigned short)color << 8; // Effacer le caractère
        } else if (vga_y > 0) { // Si au début de la ligne mais pas la première ligne
            vga_y--;            // Monter d'une ligne
            vga_x = 79;         // Aller à la fin de la ligne précédente
            vga_buffer[vga_y * 80 + vga_x] = (unsigned short)' ' | (unsigned short)color << 8; // Effacer (potentiellement)
        }
    } else { // Pour tout autre caractère
        vga_buffer[y * 80 + x] = (unsigned short)c | (unsigned short)color << 8; // Afficher le caractère
        vga_x++; // Avancer le curseur
    }

    if (vga_x >= 80) { // Si le curseur dépasse la largeur de l'écran
        vga_x = 0;   // Retour au début de la ligne
        vga_y++;     // Ligne suivante
    }
    if (vga_y >= 25) { // Si le curseur dépasse la hauteur de l'écran (défilement basique)
        // Défilement basique (scrolling)
        for (int i = 0; i < 24 * 80; i++) { // Déplacer chaque ligne d'une position vers le haut
            vga_buffer[i] = vga_buffer[i + 80];
        }
        for (int i = 24 * 80; i < 25 * 80; i++) { // Effacer la dernière ligne
            vga_buffer[i] = (unsigned short)' ' | (unsigned short)color << 8;
        }
        vga_x = 0; // Curseur au début de la ligne défilée (qui est maintenant la dernière ligne)
        vga_y = 24; // Se positionner sur la dernière ligne (24 car indexé à partir de 0)
    }
}

// Fonction pour afficher une chaîne de caractères
void print_string(const char* str, char color) {
    for (int i = 0; str[i] != '\0'; i++) { // Parcourir la chaîne jusqu'au caractère nul
        print_char(str[i], vga_x, vga_y, color); // Afficher chaque caractère
    }
}

// Fonction pour effacer l'écran
void clear_screen(char color) {
    for (int y = 0; y < 25; y++) { // Parcourir chaque ligne
        for (int x = 0; x < 80; x++) { // Parcourir chaque colonne
            vga_buffer[y * 80 + x] = (unsigned short)' ' | (unsigned short)color << 8; // Mettre un espace avec la couleur de fond
        }
    }
    vga_x = 0; // Réinitialiser la position du curseur
    vga_y = 0;
}

// La fonction principale de notre noyau
// physical_pd_addr est l'adresse physique de boot_page_directory depuis boot.s
void kmain(uint32_t physical_pd_addr) {
    current_color = 0x1F; // Texte blanc sur fond bleu
    clear_screen(current_color);

    // Réaffirmer notre répertoire de pages depuis _start, au cas où SMM ou autre chose aurait changé CR3.
    // S'assurer également que la pagination est activée.
    asm volatile (
        "mov %0, %%eax\n\t"
        "mov %%eax, %%cr3"
        : : "r"(physical_pd_addr) : "eax");

    uint32_t temp_cr0;
    asm volatile("mov %%cr0, %0" : "=r"(temp_cr0));
    if (!(temp_cr0 & 0x80000000)) { // Si le bit PG (Paging) n'est pas activé
        temp_cr0 |= 0x80000000;      // Activer le bit PG
        asm volatile("mov %0, %%cr0" : : "r"(temp_cr0));
    }

    // Espace réservé pour la structure Multiboot et l'adresse de fin du noyau.
    // uint32_t multiboot_magic = 0x2BADB002; // Non utilisé actuellement
    uint32_t multiboot_addr = 0; // Non utilisé par pmm_init dans sa forme actuelle
    uint32_t kernel_end_addr = 0; // TODO: Obtenir cette valeur depuis le script du linker, non utilisé par pmm_init actuellement

    // Initialiser la mémoire physique et virtuelle
    uint32_t total_memory_bytes = 16 * 1024 * 1024; // Supposition pour l'instant (16MiB)
    pmm_init(total_memory_bytes, kernel_end_addr, multiboot_addr);

    vmm_init(); // Active la pagination
    print_string("Gestionnaires PMM et VMM initialises.\n", current_color);

    // L'initialisation de l'initrd n'est plus nécessaire ici car les applications
    // sont directement intégrées dans l'image du noyau.
    print_string("Applications utilisateur embarquees.\n", current_color);

    // Initialiser les interruptions et les appels système
    idt_init();         // Initialise la table des descripteurs d'interruptions (IDT)
    interrupts_init();  // Configure le PIC, active les IRQ de base
    syscall_init();     // Enregistre le gestionnaire pour l'interruption 0x80 (appels système)
    print_string("IDT, PIC et Appels Systeme initialises.\n", current_color);

    // Initialiser le multitâche
    tasking_init(); // Crée la tâche noyau initiale (tâche inactive ou "idle task")
    print_string("Multitache initialise.\n", current_color);

    // Lancer le shell utilisateur
    print_string("Lancement du shell...\n", current_color);
    char* shell_argv[] = {"shell.bin", NULL}; // Arguments (argv) pour le shell
    int shell_pid = create_user_process("shell.bin", shell_argv); // Tenter de créer le processus shell

    if (shell_pid < 0) { // Si la création du processus a échoué
        print_string("Echec du lancement de shell.bin. Arret du systeme.\n", 0x0C); // Rouge sur Noir
        while(1) asm volatile("cli; hlt"); // Arrêter le système
    } else {
        print_string("shell.bin lance avec PID: ", current_color);
        print_hex((uint32_t)shell_pid, current_color); // Affichage temporaire en héxadécimal
        print_string("\n", current_color);
    }

    // Initialiser et démarrer le timer système pour permettre le préemption (scheduling)
    timer_init(100); // Configurer le timer pour une fréquence de 100 Hz
    print_string("Timer systeme active a 100Hz.\n", current_color);

    // Activer les interruptions globalement (si ce n'est pas déjà fait dans timer_init ou interrupts_init)
    // `interrupts_init` devrait déjà avoir exécuté `sti`.
    // `timer_init` démarre les IRQ0 qui déclencheront le scheduler.

    print_string("Systeme AI-OS operationnel. Passage au mode inactif.\n", current_color);

    // La tâche noyau initiale (kmain) devient la tâche "inactive" (idle).
    // Elle ne fait rien d'autre qu'attendre les interruptions (qui peuvent déclencher le scheduler).
    while(1) {
        asm volatile("hlt"); // Met le CPU en état de basse consommation jusqu'à la prochaine interruption
    }
}

// La définition de strcmp est maintenant dans kernel/libc.c
// et devrait être liée automatiquement.
