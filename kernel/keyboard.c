#include "keyboard.h"
#include "interrupts.h" // For inb/outb if not defined elsewhere, and for vga stuff
#include <stdint.h>

// These should be in kernel.c or a vga.h, but for now, declare them here
// Pointeur vers la mémoire vidéo VGA. L'adresse 0xB8000 est standard.
volatile unsigned short* vga_buffer_kb = (unsigned short*)0xB8000; // Renamed to avoid conflict
// Position actuelle du curseur
int vga_x_kb = 0; // Renamed
int vga_y_kb = 0; // Renamed
char default_color_kb = 0x1F; // Renamed

// Forward declaration for print_char_kb, as it might be used by keyboard_handler_main
void print_char_kb(char c, int x, int y, char color);
void print_string_kb(const char* str, char color); // For potential future use
void clear_screen_kb(char color); // For potential future use


// Fonction pour afficher un caractère à une position donnée avec une couleur donnée
// This is a duplicate of print_char in kernel.c. Ideally, this should be shared.
void print_char_kb(char c, int x, int y, char color) {
    if (c == '\n') {
        vga_x_kb = 0;
        vga_y_kb++;
    } else if (c == '\b') { // Handle backspace
        if (vga_x_kb > 0) {
            vga_x_kb--;
            vga_buffer_kb[vga_y_kb * 80 + vga_x_kb] = (unsigned short)' ' | (unsigned short)color << 8;
        } else if (vga_y_kb > 0) { // Backspace at start of line
            vga_y_kb--;
            vga_x_kb = 79; // Move to end of previous line
             vga_buffer_kb[vga_y_kb * 80 + vga_x_kb] = (unsigned short)' ' | (unsigned short)color << 8;
        }
    }
    else {
        vga_buffer_kb[y * 80 + x] = (unsigned short)c | (unsigned short)color << 8;
        vga_x_kb++;
    }

    if (vga_x_kb >= 80) {
        vga_x_kb = 0;
        vga_y_kb++;
    }
    // Simple scroll if needed
    if (vga_y_kb >= 25) {
        // For now, just reset to top. Proper scrolling is more complex.
        // Ideally, copy lines up and clear the last line.
        // clear_screen_kb(color); // This would clear everything
        vga_y_kb = 24; // Keep cursor on last line
        // A basic scroll would involve memmoving the buffer up one line
        // and clearing the last line. For now, we'll just let it overwrite.
        // Or, simply reset cursor to prevent overflow and let user manage screen for now.
        // For simplicity in this step, let's just wrap around for now.
        // This is not ideal but avoids complex scrolling logic for this specific step.
        // A better approach would be to implement scrolling in vga.c
        for (int i = 0; i < 24 * 80; i++) {
            vga_buffer_kb[i] = vga_buffer_kb[i + 80];
        }
        for (int i = 24 * 80; i < 25 * 80; i++) {
            vga_buffer_kb[i] = (unsigned short)' ' | (unsigned short)color << 8;
        }
        vga_x_kb = 0;

    }
}


// Table de correspondance simple Scancode -> ASCII (pour un clavier US QWERTY)
// Source: OSDev Wiki Scancode Set 1
const char scancode_map[] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8', /* 9 */
  '9', '0', '-', '=', '\b', /* Backspace */
  '\t',         /* Tab */
  'q', 'w', 'e', 'r',   /* 19 */
  't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', /* Enter key */
    0,          /* 29   - Control */
  'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', /* 39 */
 '\'', '`',   0,        /* Left shift */
 '\\', 'z', 'x', 'c', 'v', 'b', 'n',            /* 49 */
  'm', ',', '.', '/',   0,              /* Right shift */
  '*',
    0,  /* Alt */
  ' ',  /* Space bar */
    0,  /* Caps lock */
    0,  /* 59 - F1 key ... > */
    0,   0,   0,   0,   0,   0,   0,   0,
    0,  /* < ... F10 */
    0,  /* 69 - Num lock*/
    0,  /* Scroll Lock */
    0,  /* Home key */
    0,  /* Up Arrow */
    0,  /* Page Up */
  '-',
    0,  /* Left Arrow */
    0,
    0,  /* Right Arrow */
  '+',
    0,  /* 79 - End key*/
    0,  /* Down Arrow */
    0,  /* Page Down */
    0,  /* Insert Key */
    0,  /* Delete Key */
    0,   0,   0,
    0,  /* F11 Key */
    0,  /* F12 Key */
    0,  /* All other keys are undefined */
};

// Le handler appelé par l'ISR (renamed to keyboard_handler_main)
void keyboard_handler_main() {
    unsigned char scancode = inb(0x60); // Lit le scancode depuis le port du clavier

    // On ne gère que les pressions de touche (pas les relâchements pour l'instant)
    // Scancodes > 0x80 are key releases for Set 1
    if (scancode < 0x80) { // Only handle key press events
        if (scancode < sizeof(scancode_map) && scancode_map[scancode] != 0) {
            char c = scancode_map[scancode];

            // Affiche le caractère à la position actuelle du curseur
            // Using the renamed vga variables from this file
            print_char_kb(c, vga_x_kb, vga_y_kb, default_color_kb);
            // print_char_kb will update vga_x_kb and vga_y_kb internally
        }
    }
    // Important: Send EOI to PIC
    // If IRQ is from slave PIC (IRQ 8-15), send EOI to slave first
    // For keyboard (IRQ 1), it's on the master PIC.
    // This will be handled by the assembly stub or a common C handler.
    // For now, the EOI is conceptualized to be sent by the assembly stub.
}

// Helper to initialize VGA variables if needed from kernel.c
// This is a temporary solution. VGA control should be centralized.
void init_vga_kb(int x, int y, char color) {
    vga_x_kb = x;
    vga_y_kb = y;
    default_color_kb = color;
}

// These functions are also duplicates and should be centralized
void print_string_kb(const char* str, char color) {
    for (int i = 0; str[i] != '\0'; i++) {
        print_char_kb(str[i], vga_x_kb, vga_y_kb, color);
    }
}

void clear_screen_kb(char color) {
    for (int y = 0; y < 25; y++) {
        for (int x = 0; x < 80; x++) {
            // Use print_char_kb to ensure consistent cursor handling, though direct buffer write is faster for clear
             vga_buffer_kb[y * 80 + x] = (unsigned short)' ' | (unsigned short)color << 8;
        }
    }
    vga_x_kb = 0;
    vga_y_kb = 0;
}
