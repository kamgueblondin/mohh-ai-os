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
static char timer_debug_char_val = 'A'; // Renamed to avoid conflict if timer_debug_char was a global

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
