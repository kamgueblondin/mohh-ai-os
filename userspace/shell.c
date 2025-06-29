#include <stddef.h> // Pour NULL

// Wrappers pour les syscalls
void putc(char c) { asm volatile("int $0x80" : : "a"(1), "b"(c)); }
// SYS_GETS (syscall n°4)
void gets(char* buffer, int size) { asm volatile("int $0x80" : : "a"(4), "b"(buffer), "c"(size)); }
// SYS_EXEC (syscall n°5)
int exec(const char* path, char* argv[]) {
    int result;
    // La valeur de retour 'result' contiendra le PID du processus créé ou un code d'erreur.
    // Pour l'instant, on ne l'utilise pas mais c'est une bonne pratique.
    asm volatile("int $0x80" : "=a"(result) : "a"(5), "b"(path), "c"(argv));
    return result;
}

void print(const char* s) {
    for (int i = 0; s[i] != '\0'; i++) putc(s[i]);
}

// Fonction simple pour vérifier si une chaîne est vide (ne contient que des espaces ou est nulle)
// Ceci est une simplification, une vraie gestion d'input validerait mieux.
int is_empty(const char* str) {
    if (str == NULL) return 1;
    while (*str) {
        if (*str != ' ' && *str != '\t' && *str != '\n' && *str != '\r') {
            return 0; // Non vide si on trouve un caractère non-espace blanc
        }
        str++;
    }
    return 1; // Vide si on arrive ici
}


void main() {
    print("AI-OS Shell v0.1 - Bienvenue !\n");

    char input_buffer[256];
    char* argv[3]; // Nom du programme, argument1, et NULL pour terminer la liste

    while(1) {
        print("> ");
        gets(input_buffer, 255); // Laisse de la place pour le null terminator

        // input_buffer[strlen(input_buffer)-1] = '\0'; // gets devrait gérer le null terminator,
                                                      // mais si le noyau met un \n, il faut le retirer.
                                                      // On va supposer que gets s'en occupe pour l'instant.
                                                      // Si la saisie est vide ou ne contient que des espaces, on reboucle.
        if (input_buffer[0] == '\0' || is_empty(input_buffer)) {
            continue;
        }

        // Prépare les arguments pour le programme fake_ai
        argv[0] = "fake_ai.bin"; // Nom du programme à exécuter
        argv[1] = input_buffer;  // La question de l'utilisateur
        argv[2] = NULL;          // Marqueur de fin pour la liste d'arguments argv
                                 // C'est une convention C standard pour exec.

        // Exécute le programme et attend qu'il se termine
        // (un exec simple et bloquant pour commencer)
        // On pourrait vérifier la valeur de retour de exec() pour gérer les erreurs
        // (par exemple, si fake_ai.bin n'est pas trouvé).
        exec("fake_ai.bin", argv);

        // Remettre à zéro le buffer d'input pour éviter des surprises au prochain tour.
        // Bien que gets devrait l'écraser.
        for(int i=0; i<256; ++i) input_buffer[i] = '\0';
    }
}
