#include "timer.h"
#include "interrupts.h"
#include "task/task.h"
#include <stdint.h>
#include "kernel/debug_vga.h" // For debug_putc_at

// extern volatile unsigned short* vga_buffer; // No longer needed

#define PIT_BASE_FREQUENCY 1193182

#define PIT_CHANNEL0_DATA 0x40
#define PIT_COMMAND_REG   0x43

// Debug counter for timer ticks
static uint32_t timer_tick_debug_counter = 0;
static char timer_debug_char_val = 'A';

// Nouvelle fonction de débogage minimaliste pour IRQ0
static char minimal_timer_indicator_char = 'T';
void timer_handler_minimal_debug() {
    // Debug: Afficher un caractère alternant pour montrer que cette fonction est appelée
    debug_putc_at(minimal_timer_indicator_char, 69, 0, 0x0C); // Rouge sur Noir, à (x=69, y=0)
    if (minimal_timer_indicator_char == 'T') minimal_timer_indicator_char = 'M';
    else minimal_timer_indicator_char = 'T';

    schedule(); // Appelons maintenant le scheduler
}


// Ancien timer_handler, toujours présent mais non appelé directement par le nouveau stub IRQ0
// On pourrait le supprimer plus tard si timer_handler_minimal_debug devient le handler officiel.
void timer_handler() {
    // Debug: Display a changing character in the top-right corner
    timer_tick_debug_counter++;
    if ((timer_tick_debug_counter % 100) == 0) { // Update less frequently to be visible
        debug_putc_at(timer_debug_char_val, 79, 0, 0x0F); // White on Black, (x=79, y=0)
        timer_debug_char_val++;
        if (timer_debug_char_val > 'Z') timer_debug_char_val = 'A';
    }
    schedule();
}

void timer_init(uint32_t frequency) {
    if (frequency == 0) {
        return;
    }
    uint16_t divisor = (uint16_t)(PIT_BASE_FREQUENCY / frequency);
    outb(PIT_COMMAND_REG, 0x36);
    uint8_t l = (uint8_t)(divisor & 0xFF);
    outb(PIT_CHANNEL0_DATA, l);
    uint8_t h = (uint8_t)((divisor >> 8) & 0xFF);
    outb(PIT_CHANNEL0_DATA, h);
}
