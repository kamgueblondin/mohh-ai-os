#include "libc.h"

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
