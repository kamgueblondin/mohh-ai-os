// Ce programme n'a accès à AUCUNE fonction du noyau directement.
// Il ne peut communiquer que via les appels système.

// Fonction "wrapper" pour l'appel système putc
void putc(char c) {
    // Syscall 1 = putc
    asm volatile("int $0x80" : : "a"(1), "b"(c));
}

// Fonction "wrapper" pour l'appel système exit
void exit() {
    // Syscall 0 = exit
    asm volatile("int $0x80" : : "a"(0));
}

void main() {
    char* msg = "Bonjour depuis l'espace utilisateur !\n";
    for (int i = 0; msg[i] != '\0'; i++) {
        putc(msg[i]);
    }
    exit();
}
