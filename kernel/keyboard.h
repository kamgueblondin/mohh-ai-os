#ifndef KEYBOARD_H
#define KEYBOARD_H

void keyboard_handler_main(); // Renamed to avoid conflict if we have a keyboard_handler stub

// Initializes or updates VGA cursor position and color for keyboard input
void init_vga_kb(int x, int y, char color);

#endif
