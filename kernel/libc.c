#include "libc.h"
#include <stdint.h> // Ajout redondant pour s'assurer de la définition de uint32_t

int strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

void* memcpy(void* dest, const void* src, size_t n) {
    char* d = dest;
    const char* s = src;
    while (n--) {
        *d++ = *s++;
    }
    return dest;
}

// Function to print a character directly via print_char in kernel.c
// This requires print_char to be globally accessible or passed somehow.
// Assuming print_char updates global vga_x, vga_y, current_color
extern void print_char(char c, int x, int y, char color); // From kernel.c
extern int vga_x; // from kernel.c
extern int vga_y; // from kernel.c

void print_hex_char(unsigned char c, char color) {
    unsigned char nibble;
    nibble = (c >> 4) & 0x0F;
    print_char((nibble < 10) ? (nibble + '0') : (nibble - 10 + 'A'), vga_x, vga_y, color);
    nibble = c & 0x0F;
    print_char((nibble < 10) ? (nibble + '0') : (nibble - 10 + 'A'), vga_x, vga_y, color);
}

void print_hex(uint32_t n, char color) {
    print_string("0x", color);
    print_hex_char((n >> 24) & 0xFF, color);
    print_hex_char((n >> 16) & 0xFF, color);
    print_hex_char((n >> 8) & 0xFF, color);
    print_hex_char(n & 0xFF, color);
}

// Fonction itoa basique (entier vers chaîne ASCII)
// Non réentrante si str est statique, mais ici str est fourni par l'appelant.
char* itoa(uint32_t value, char* str, int base) {
    char* rc;
    char* ptr;
    char* low;
    // Vérifier que la base est valide
    if (base < 2 || base > 36) {
        *str = '\0';
        return str;
    }
    rc = ptr = str;
    // Gérer le cas spécial de la valeur 0
    if (value == 0) {
        *ptr++ = '0';
        *ptr = '\0';
        return rc;
    }
    // Sauvegarder la position de départ pour l'inversion ultérieure
    low = ptr;
    while (value > 0) {
        // Utiliser le reste de la division pour obtenir le chiffre
        unsigned int remainder = value % base;
        *ptr++ = (remainder < 10) ? (remainder + '0') : (remainder - 10 + 'a');
        value /= base;
    }
    // Terminer la chaîne
    *ptr = '\0';
    // Inverser la chaîne, car les chiffres ont été stockés dans l'ordre inverse
    ptr--;
    while (low < ptr) {
        char tmp = *low;
        *low++ = *ptr;
        *ptr-- = tmp;
    }
    return rc;
}

void* memset(void* dest, int val, size_t count) {
    unsigned char* ptr = (unsigned char*)dest;
    while (count-- > 0) {
        *ptr++ = (unsigned char)val;
    }
    return dest;
}
