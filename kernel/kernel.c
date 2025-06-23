#include "idt.h"        // NOUVEAU
#include "interrupts.h" // NOUVEAU
#include "keyboard.h"   // For init_vga_kb, if we decide to call it from kmain

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


    // Initialisation
    idt_init();         // Initialise la table des interruptions
    interrupts_init();  // Initialise le PIC et active les interruptions (sti)

    // Le CPU attendra passivement une interruption au lieu de tourner en boucle
    while(1) {
        asm volatile("hlt");
    }
}
