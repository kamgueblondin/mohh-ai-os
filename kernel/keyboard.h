#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h> // For uint32_t

// Handler d'interruption principal pour le clavier
void keyboard_handler_main();

// Appelée par le syscall SYS_GETS pour configurer le buffer et la tâche en attente.
// user_buf: Pointeur vers le buffer en espace utilisateur.
// user_buf_size: Taille maximale du buffer utilisateur.
void keyboard_prepare_for_gets(char* user_buf, uint32_t user_buf_size);

// Appelée par le handler syscall après que la tâche ait été réveillée
// pour obtenir le nombre de caractères effectivement lus dans le buffer utilisateur.
uint32_t keyboard_get_chars_read_count();

// La fonction init_vga_kb n'est plus nécessaire ici si on utilise les globales.
// void init_vga_kb(int x, int y, char color);

// keyboard_char_for_gets est maintenant statique ou interne à keyboard.c,
// donc pas besoin de la déclarer ici.
// void keyboard_char_for_gets(char c);

#endif // KEYBOARD_H
