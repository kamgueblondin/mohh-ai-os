#include "timer.h"
#include "interrupts.h"
#include "task/task.h"
#include <stdint.h>

#define PIT_BASE_FREQUENCY 1193182

#define PIT_CHANNEL0_DATA 0x40
#define PIT_COMMAND_REG   0x43

void timer_handler() {
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
