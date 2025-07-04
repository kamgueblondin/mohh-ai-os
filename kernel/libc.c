#include "libc.h"
#include <stdint.h> // Pour uint32_t, size_t (bien que size_t soit souvent dans stddef.h)

// Fonction pour comparer deux chaînes de caractères.
// Retourne 0 si s1 et s2 sont identiques.
// Retourne une valeur négative si s1 < s2.
// Retourne une valeur positive si s1 > s2.
int strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) { // Tant que les caractères sont égaux et qu'on n'est pas à la fin de s1
        s1++;
        s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2; // Retourne la différence des premiers caractères différents
}

// Fonction pour copier 'n' octets de 'src' vers 'dest'.
void* memcpy(void* dest, const void* src, size_t n) {
    char* d = (char*)dest;         // Pointeur de destination, casté en char* pour l'arithmétique d'octet
    const char* s = (const char*)src; // Pointeur source, casté en const char*
    while (n--) {                  // Tant qu'il y a des octets à copier
        *d++ = *s++;               // Copier l'octet et incrémenter les pointeurs
    }
    return dest;                   // Retourner le pointeur de destination original
}

// Fonction pour afficher un caractère directement via print_char de kernel.c
// Nécessite que print_char soit globalement accessible ou passée d'une manière ou d'une autre.
// Suppose que print_char met à jour les variables globales vga_x, vga_y, current_color.
extern void print_char(char c, int x, int y, char color); // Depuis kernel.c
extern int vga_x; // Depuis kernel.c
extern int vga_y; // Depuis kernel.c
// Note : print_string est aussi supposée exister globalement si utilisée par print_hex.
extern void print_string(const char* str, char color); // Depuis kernel.c


// Affiche un octet (unsigned char) en format hexadécimal.
void print_hex_char(unsigned char c, char color) {
    unsigned char nibble; // Un nibble est un demi-octet (4 bits)
    // Afficher le nibble de poids fort (les 4 bits de gauche)
    nibble = (c >> 4) & 0x0F; // Isole le nibble de poids fort
    print_char((nibble < 10) ? (nibble + '0') : (nibble - 10 + 'A'), vga_x, vga_y, color);
    // Afficher le nibble de poids faible (les 4 bits de droite)
    nibble = c & 0x0F; // Isole le nibble de poids faible
    print_char((nibble < 10) ? (nibble + '0') : (nibble - 10 + 'A'), vga_x, vga_y, color);
}

// Affiche un entier non signé de 32 bits en format hexadécimal.
void print_hex(uint32_t n, char color) {
    print_string("0x", color); // Préfixe standard pour l'hexadécimal
    print_hex_char((n >> 24) & 0xFF, color); // Octet le plus significatif
    print_hex_char((n >> 16) & 0xFF, color);
    print_hex_char((n >> 8) & 0xFF, color);
    print_hex_char(n & 0xFF, color);         // Octet le moins significatif
}

// Fonction itoa basique (entier vers chaîne ASCII).
// Convertit un entier `value` en une chaîne de caractères `str` dans la `base` spécifiée.
// Non réentrante si `str` était statique, mais ici `str` est fourni par l'appelant.
char* itoa(uint32_t value, char* str, int base) {
    char* rc;    // Pointeur de retour (début de la chaîne)
    char* ptr;   // Pointeur courant dans la chaîne
    char* low;   // Pointeur vers le début des chiffres (pour inversion)

    // Vérifier que la base est valide (entre 2 et 36 inclus).
    if (base < 2 || base > 36) {
        *str = '\0'; // Chaîne vide si base invalide
        return str;
    }
    rc = ptr = str; // Initialiser les pointeurs au début du buffer fourni

    // Gérer le cas spécial de la valeur 0.
    if (value == 0) {
        *ptr++ = '0'; // Mettre '0'
        *ptr = '\0';  // Terminer la chaîne
        return rc;    // Retourner la chaîne "0"
    }

    // Sauvegarder la position de départ pour l'inversion ultérieure.
    // Les chiffres sont générés de droite à gauche (poids faible d'abord).
    low = ptr;
    while (value > 0) { // Tant qu'il reste des chiffres à convertir
        unsigned int remainder = value % base; // Obtenir le reste (prochain chiffre)
        // Convertir le chiffre en caractère ASCII.
        // Si < 10, c'est '0'-'9'. Sinon, c'est 'a'-'z'.
        *ptr++ = (remainder < 10) ? (remainder + '0') : (remainder - 10 + 'a');
        value /= base; // Diviser la valeur par la base pour le prochain chiffre
    }

    // Terminer la chaîne (caractère nul).
    *ptr = '\0';

    // Inverser la chaîne, car les chiffres ont été stockés dans l'ordre inverse (de droite à gauche).
    ptr--; // Se positionner sur le dernier chiffre écrit
    while (low < ptr) { // Tant que les pointeurs ne se sont pas croisés
        char tmp = *low; // Échanger les caractères
        *low++ = *ptr;
        *ptr-- = tmp;
    }
    return rc; // Retourner le pointeur vers le début de la chaîne convertie
}

// Fonction pour remplir une zone mémoire 'dest' avec 'count' octets de valeur 'val'.
void* memset(void* dest, int val, size_t count) {
    unsigned char* ptr = (unsigned char*)dest; // Caster dest en pointeur sur unsigned char
    while (count-- > 0) { // Tant qu'il y a des octets à remplir
        *ptr++ = (unsigned char)val; // Écrire la valeur et avancer le pointeur
    }
    return dest; // Retourner le pointeur de destination original
}
