#include <stddef.h> // Pour NULL

// Ce programme doit pouvoir recevoir des arguments (argc, argv).
// L'implémentation de la réception des arguments est gérée
// par le noyau lors de la création du processus (via l'appel système SYS_EXEC).

// Fonctions d'enrobage (wrappers) pour les appels système putc et exit.
// Ces fonctions utilisent l'instruction `int 0x80` pour déclencher une interruption logicielle
// et passer le contrôle au noyau pour exécuter l'appel système demandé.
// Le numéro de l'appel système est placé dans EAX, les arguments dans EBX, ECX, etc.
void putc(char c) { asm volatile("int $0x80" : : "a"(1), "b"(c)); } // SYS_PUTC (numéro 1), caractère dans EBX.
void exit() { asm volatile("int $0x80" : : "a"(0)); } // SYS_EXIT (numéro 0).

// Fonctions C basiques que nous devons implémenter nous-mêmes
// car nous n'avons pas de bibliothèque C standard (libc) liée à notre programme utilisateur.

// Compare deux chaînes de caractères s1 et s2.
// Retourne 0 si identiques, une valeur négative si s1 < s2, positive si s1 > s2.
int strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) { // Tant que les caractères sont égaux et qu'on n'est pas à la fin de s1.
        s1++;
        s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2; // Retourne la différence des premiers caractères non concordants.
}

// Cherche la première occurrence de la sous-chaîne `needle` dans `haystack`.
// Retourne un pointeur vers le début de `needle` dans `haystack`, ou NULL si non trouvée.
char* strstr(const char* haystack, const char* needle) {
    if (!*needle) { // Si `needle` est une chaîne vide.
        return (char*)haystack; // Comportement standard : retourner `haystack`.
    }
    char* p1 = (char*)haystack; // Pointeur pour parcourir `haystack`.
    while (*p1) { // Tant qu'on n'est pas à la fin de `haystack`.
        char* p1_begin = p1;      // Sauvegarder la position actuelle dans `haystack`.
        char* p2 = (char*)needle; // Pointeur pour parcourir `needle`.
        // Comparer les caractères de `haystack` (à partir de `p1`) et `needle`.
        while (*p1 && *p2 && *p1 == *p2) {
            p1++;
            p2++;
        }
        if (!*p2) { // Si on a atteint la fin de `needle` (tous les caractères correspondaient).
            return p1_begin; // `needle` a été trouvée, retourner sa position de début dans `haystack`.
        }
        p1 = p1_begin + 1; // Avancer d'un caractère dans `haystack` et réessayer.
    }
    return NULL; // `needle` non trouvée dans `haystack`.
}

// Fonction principale du programme `fake_ai`.
// argc: Nombre d'arguments passés au programme.
// argv: Tableau de chaînes de caractères (les arguments eux-mêmes). argv[0] est généralement le nom du programme.
void main(int argc, char* argv[]) {
    if (argc < 2) {
        // Si aucun argument n'est passé (argv[1] contiendrait la question de l'utilisateur),
        // nous pourrions afficher un message d'erreur ou simplement quitter.
        // Pour l'instant, nous quittons silencieusement, comme dans l'exemple original.
        exit(); // Terminer le programme.
    }

    char* prompt = argv[1]; // Le premier argument (la question) est dans argv[1].

    // Logique de l'"IA" simulée : répond en fonction de mots-clés dans la question.
    if (strstr(prompt, "bonjour")) { // Si la question contient "bonjour".
        char* resp = "Bonjour ! Comment puis-je vous aider aujourd'hui ?\n";
        for (int i = 0; resp[i] != '\0'; i++) putc(resp[i]); // Afficher la réponse caractère par caractère.
    } else if (strstr(prompt, "heure")) { // Si la question contient "heure".
        char* resp = "Il est l'heure de developper un OS !\n";
        for (int i = 0; resp[i] != '\0'; i++) putc(resp[i]);
    } else if (strcmp(prompt, "aide") == 0) { // Si la question est exactement "aide".
        char* resp = "Commandes simulees : 'bonjour', 'heure', 'aide'.\n";
        for (int i = 0; resp[i] != '\0'; i++) putc(resp[i]);
    } else { // Si aucune des conditions précédentes n'est remplie.
        char* resp = "Desole, je ne comprends pas la question.\n";
        for (int i = 0; resp[i] != '\0'; i++) putc(resp[i]);
    }

    exit(); // Terminer le programme après avoir répondu.
}
