#ifndef KEYBOARD_H
#define KEYBOARD_H

// Fonction pour initialiser/synchroniser les variables VGA du clavier
// avec celles du kernel principal. (Déclaration ajoutée)
void init_vga_kb(int x, int y, char color);

void keyboard_handler_main(); // Renamed to avoid conflict if we have a keyboard_handler stub

#endif
