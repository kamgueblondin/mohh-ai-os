// Pointeur vers la mémoire vidéo VGA. L'adresse 0xB8000 est standard.
volatile unsigned short* vga_buffer = (unsigned short*)0xB8000;
// Position actuelle du curseur
int vga_x = 0;
int vga_y = 0;

// Fonction pour afficher un caractère à une position donnée avec une couleur donnée
void print_char(char c, int x, int y, char color) {
    vga_buffer[y * 80 + x] = (unsigned short)c | (unsigned short)color << 8;
}

// Fonction pour afficher une chaîne de caractères
void print_string(const char* str, char color) {
    for (int i = 0; str[i] != '\0'; i++) {
        if (str[i] == '\n') { // Gérer le retour à la ligne
            vga_x = 0;
            vga_y++;
        } else {
            print_char(str[i], vga_x, vga_y, color);
            vga_x++;
            if (vga_x >= 80) {
                vga_x = 0;
                vga_y++;
            }
        }
    }
}

// La fonction principale de notre noyau
void kmain(void) {
    // Couleur : texte blanc (0xF) sur fond bleu (0x1)
    char color = 0x1F;

    // Effacer l'écran en remplissant de caractères "espace"
    for (int y = 0; y < 25; y++) {
        for (int x = 0; x < 80; x++) {
            print_char(' ', x, y, color);
        }
    }

    // Afficher notre message de bienvenue
    vga_x = 25; // Centrer un peu le texte
    vga_y = 12;
    print_string("Bienvenue dans AI-OS !", color);

    // Le noyau ne doit jamais se terminer
    while(1) {}
}
