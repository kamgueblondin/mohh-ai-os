#include <stddef.h> // Pour NULL

// Ce programme doit pouvoir recevoir des arguments (argc, argv)
// L'implémentation de la réception des arguments doit être gérée
// par le noyau lors de la création du processus (via SYS_EXEC).

// Wrappers pour les syscalls putc et exit
void putc(char c) { asm volatile("int $0x80" : : "a"(1), "b"(c)); }
void exit() { asm volatile("int $0x80" : : "a"(0)); }

// Fonctions C basiques qu'il faudra implémenter nous-même
// car nous n'avons pas de libc.
int strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

char* strstr(const char* haystack, const char* needle) {
    if (!*needle) {
        return (char*)haystack; // Aiguille vide, on retourne le haystack
    }
    char* p1 = (char*)haystack;
    while (*p1) {
        char* p1_begin = p1;
        char* p2 = (char*)needle;
        while (*p1 && *p2 && *p1 == *p2) {
            p1++;
            p2++;
        }
        if (!*p2) {
            return p1_begin; // Aiguille trouvée
        }
        p1 = p1_begin + 1; // Avancer dans le haystack
    }
    return NULL; // Non trouvé
}

void main(int argc, char* argv[]) {
    if (argc < 2) {
        // Si aucun argument n'est passé (argv[1] serait la question),
        // on pourrait afficher un message d'erreur ou simplement quitter.
        // Pour l'instant, on quitte silencieusement comme dans l'exemple.
        exit();
    }

    char* prompt = argv[1];

    // Logique de l'IA simulée
    if (strstr(prompt, "bonjour")) {
        char* resp = "Bonjour ! Comment puis-je vous aider aujourd'hui ?\n";
        for (int i = 0; resp[i] != '\0'; i++) putc(resp[i]);
    } else if (strstr(prompt, "heure")) {
        char* resp = "Il est l'heure de developper un OS !\n";
        for (int i = 0; resp[i] != '\0'; i++) putc(resp[i]);
    } else if (strcmp(prompt, "aide") == 0) {
        char* resp = "Commandes simulees : 'bonjour', 'heure', 'aide'.\n";
        for (int i = 0; resp[i] != '\0'; i++) putc(resp[i]);
    } else {
        char* resp = "Desole, je ne comprends pas la question.\n";
        for (int i = 0; resp[i] != '\0'; i++) putc(resp[i]);
    }

    exit();
}
