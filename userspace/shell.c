#include <stddef.h> // Pour NULL

// Fonctions d'enrobage (wrappers) pour les appels système.
// Elles simplifient l'utilisation des appels système en encapsulant l'instruction `int 0x80`.

// Affiche un caractère via l'appel système SYS_PUTC (numéro 1).
// Le caractère à afficher est passé dans EBX.
void putc(char c) { asm volatile("int $0x80" : : "a"(1), "b"(c)); }

// Lit une chaîne de caractères depuis l'entrée standard (clavier) via SYS_GETS (numéro 4).
// `buffer` est le tampon où stocker la chaîne, `size` est la taille maximale du tampon.
// Le noyau devrait gérer la fin de chaîne (caractère nul).
void gets(char* buffer, int size) { asm volatile("int $0x80" : : "a"(4), "b"(buffer), "c"(size)); }

// Exécute un programme via SYS_EXEC (numéro 5).
// `path` est le chemin vers l'exécutable.
// `argv` est un tableau de chaînes de caractères représentant les arguments.
// Retourne le PID du processus enfant ou un code d'erreur négatif.
int exec(const char* path, char* argv[]) {
    int result;
    // La valeur de retour `result` (généralement le statut de sortie de l'enfant ou un code d'erreur de exec)
    // est placée dans EAX par le noyau. Le `"=a"(result)` la récupère.
    asm volatile("int $0x80" : "=a"(result) : "a"(5), "b"(path), "c"(argv));
    return result;
}

// Affiche une chaîne de caractères à la console.
void print(const char* s) {
    for (int i = 0; s[i] != '\0'; i++) { // Parcourt la chaîne jusqu'au caractère nul.
        putc(s[i]); // Affiche chaque caractère.
    }
}

// Fonction simple pour vérifier si une chaîne est vide (ne contient que des espaces blancs ou est NULL).
// Ceci est une simplification ; une gestion d'entrée robuste validerait mieux.
// Retourne 1 si vide, 0 sinon.
int is_empty(const char* str) {
    if (str == NULL) return 1; // Considérer NULL comme vide.
    while (*str) { // Parcourir la chaîne.
        if (*str != ' ' && *str != '\t' && *str != '\n' && *str != '\r') {
            return 0; // Non vide si un caractère non-espace blanc est trouvé.
        }
        str++;
    }
    return 1; // Vide si on arrive ici (tous les caractères étaient des espaces blancs ou la chaîne était initialement vide).
}


// Fonction principale du shell.
void main() {
    print("SHELL MAIN EXECUTED\n"); // Message de débogage immédiat
    print("AI-OS Shell v0.1 - Bienvenue !\n"); // Message de bienvenue.

    char input_buffer[256]; // Tampon pour stocker l'entrée utilisateur.
    char* argv[3];          // Tableau pour les arguments à passer à `exec`.
                            // argv[0] = nom du programme, argv[1] = argument, argv[2] = NULL.

    while(1) { // Boucle principale du shell.
        print("> "); // Afficher l'invite de commande.
        gets(input_buffer, 255); // Lire l'entrée utilisateur. Laisser de la place pour le terminateur nul.

        // Le noyau (via `gets`) devrait s'occuper du terminateur nul.
        // Si `gets` incluait le '\n' final, il faudrait le supprimer ici.
        // Supposons pour l'instant que `gets` fournit une chaîne proprement terminée sans '\n' final superflu.

        // Si la saisie est vide ou ne contient que des espaces, ignorer et reboucler.
        if (input_buffer[0] == '\0' || is_empty(input_buffer)) {
            continue; // Passer à la prochaine itération de la boucle.
        }

        // Préparer les arguments pour le programme `fake_ai.bin`.
        argv[0] = "fake_ai.bin"; // Nom du programme à exécuter.
        argv[1] = input_buffer;  // L'entrée utilisateur devient le premier argument pour `fake_ai.bin`.
        argv[2] = NULL;          // Marqueur de fin pour la liste d'arguments `argv` (convention C standard pour `exec`).

        // Exécuter le programme `fake_ai.bin` et attendre qu'il se termine.
        // L'implémentation actuelle de `SYS_EXEC` dans le noyau est bloquante (attend la fin de l'enfant).
        // On pourrait vérifier la valeur de retour de `exec()` pour gérer les erreurs
        // (par exemple, si `fake_ai.bin` n'est pas trouvé ou ne peut être exécuté).
        exec("fake_ai.bin", argv);

        // Optionnel : remettre à zéro le tampon d'entrée pour éviter des surprises au prochain tour,
        // bien que `gets` devrait l'écraser complètement lors de la prochaine lecture.
        for(int i=0; i < 256; ++i) input_buffer[i] = '\0';
    }
}
