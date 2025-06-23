#ifndef INTERRUPTS_H
#define INTERRUPTS_H

#include <stdint.h>

// IO Port helper function
void outb(uint16_t port, uint8_t val);
uint8_t inb(uint16_t port);
void io_wait(void);


void pic_remap(int offset1, int offset2);
void interrupts_init();

// ISR and IRQ handlers (stubs to be defined in assembly)
// CPU Exceptions (first 32 interrupts)
extern void isr0();
extern void isr1();
extern void isr2();
extern void isr3();
extern void isr4();
extern void isr5();
extern void isr6();
extern void isr7();
extern void isr8();
extern void isr9();
extern void isr10();
extern void isr11();
extern void isr12();
extern void isr13();
extern void isr14();
extern void isr15();
extern void isr16();
extern void isr17();
extern void isr18();
extern void isr19();
extern void isr20();
extern void isr21();
extern void isr22();
extern void isr23();
extern void isr24();
extern void isr25();
extern void isr26();
extern void isr27();
extern void isr28();
extern void isr29();
extern void isr30();
extern void isr31();

// PIC IRQs (remapped to 32-47)
extern void irq0();  // Timer
extern void irq1();  // Keyboard
extern void irq2();
extern void irq3();
extern void irq4();
extern void irq5();
extern void irq6();
extern void irq7();
extern void irq8();  // RTC
extern void irq9();
extern void irq10();
extern void irq11();
extern void irq12(); // Mouse
extern void irq13(); // FPU
extern void irq14(); // Primary ATA
extern void irq15(); // Secondary ATA


#endif
