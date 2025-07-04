#include "timer.h"
#include "interrupts.h"
#include "task/task.h"
#include <stdint.h>

// For direct VGA buffer access for debugging
extern volatile unsigned short* vga_buffer;

#define PIT_BASE_FREQUENCY 1193182

#define PIT_CHANNEL0_DATA 0x40
#define PIT_COMMAND_REG   0x43

// Debug counter for timer ticks
static uint32_t timer_tick_debug_counter = 0;
static char timer_debug_char = 'A';

void timer_handler() {
    // Debug: Display a changing character in the top-right corner
    // This is a very raw way to show timer is ticking, avoid complex print functions in ISR
    if (vga_buffer) { // Check if vga_buffer is valid (it should be by the time timer runs)
        // Display a single character that changes, e.g., at position (0, 79)
        // Or a counter that increments and wraps around
        timer_tick_debug_counter++;
        unsigned short val_to_write;
        if ((timer_tick_debug_counter % 100) == 0) { // Update less frequently to be visible
            val_to_write = (unsigned short)(timer_debug_char) | (0x0F << 8); // White on Black
            vga_buffer[0 * 80 + 79] = val_to_write;
            timer_debug_char++;
            if (timer_debug_char > 'Z') timer_debug_char = 'A';
        }
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
