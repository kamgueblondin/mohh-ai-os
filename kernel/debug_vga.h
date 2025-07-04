#ifndef DEBUG_VGA_H
#define DEBUG_VGA_H

// It's better to include stdint.h for uint16_t if used in function signatures,
// but for char, int, it's not strictly needed here.
// However, the color is often a specific type, let's assume char for now.

void debug_putc_at(char c, int x, int y, char color);

#endif // DEBUG_VGA_H
