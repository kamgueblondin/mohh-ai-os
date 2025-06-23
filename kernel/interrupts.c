#include "interrupts.h"
#include "idt.h"
#include "keyboard.h" // For keyboard_handler_main declaration, though irq1 stub calls it directly
#include <stdint.h>

// PIC I/O Ports
#define PIC1            0x20    /* IO base address for master PIC */
#define PIC2            0xA0    /* IO base address for slave PIC */
#define PIC1_COMMAND    PIC1
#define PIC1_DATA       (PIC1+1)
#define PIC2_COMMAND    PIC2
#define PIC2_DATA       (PIC2+1)

// PIC Commands
#define PIC_EOI         0x20    /* End-of-interrupt command code */

// PIC Initialization Control Words
#define ICW1_ICW4       0x01    /* Indicates that ICW4 will be present */
#define ICW1_SINGLE     0x02    /* Single (cascade) mode */
#define ICW1_INTERVAL4  0x04    /* Call address interval 4 (8) */
#define ICW1_LEVEL      0x08    /* Level triggered (edge) mode */
#define ICW1_INIT       0x10    /* Initialization - required! */

#define ICW4_8086       0x01    /* 8086/88 (MCS-80/85) mode */
#define ICW4_AUTO       0x02    /* Auto (normal) EOI */
#define ICW4_BUF_SLAVE  0x08    /* Buffered mode/slave */
#define ICW4_BUF_MASTER 0x0C    /* Buffered mode/master */
#define ICW4_SFNM       0x10    /* Special fully nested (not) */

// Helper function to write a byte to an I/O port
void outb(uint16_t port, uint8_t val) {
    asm volatile ( "outb %0, %1" : : "a"(val), "Nd"(port) );
}

// Helper function to read a byte from an I/O port
uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile ( "inb %1, %0"
                   : "=a"(ret)
                   : "Nd"(port) );
    return ret;
}

// Helper function to introduce a small delay for PIC
void io_wait(void) {
    // Port 0x80 is typically unused and safe for a short delay
    outb(0x80, 0);
}

// Remaps the PIC controllers.
// offset1: vector offset for master PIC (vectors offset1..offset1+7)
// offset2: vector offset for slave PIC (vectors offset2..offset2+7)
void pic_remap(int offset1, int offset2) {
    unsigned char a1, a2;

    a1 = inb(PIC1_DATA); // save masks
    a2 = inb(PIC2_DATA);

    // starts the initialization sequence (in cascade mode)
    outb(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();
    outb(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();

    outb(PIC1_DATA, offset1); // ICW2: Master PIC vector offset
    io_wait();
    outb(PIC2_DATA, offset2); // ICW2: Slave PIC vector offset
    io_wait();

    // ICW3: tell Master PIC that there is a slave PIC at IRQ2 (0000 0100)
    outb(PIC1_DATA, 4);
    io_wait();
    // ICW3: tell Slave PIC its cascade identity (0000 0010)
    outb(PIC2_DATA, 2);
    io_wait();

    outb(PIC1_DATA, ICW4_8086); // ICW4: have the PICs use 8086 mode
    io_wait();
    outb(PIC2_DATA, ICW4_8086);
    io_wait();

    // Restore saved masks (or initialize to 0 to unmask all)
    // For now, let's unmask all interrupts on both PICs after remapping.
    // The specific IRQs can be masked/unmasked later if needed.
    outb(PIC1_DATA, 0x00); // Unmask all for Master
    outb(PIC2_DATA, 0x00); // Unmask all for Slave
    // outb(PIC1_DATA, a1); // To restore original masks
    // outb(PIC2_DATA, a2);
}


// C handler for CPU exceptions (ISRs 0-31)
// This function will be called from the assembly ISR stubs.
// For now, it just prints a message. A more robust handler would print registers, error codes etc.
void fault_handler(struct idt_entry* r) { // The 'r' here is conceptual for what assembly pushes
                                        // Actual parameters depend on how 'call fault_handler' is set up
                                        // For now, let's assume the assembly pushes interrupt number and error code
                                        // and we'll need to fix the signature later if we want to use them.
    // A simple placeholder. In a real scenario, you'd print interrupt number, error code, registers, etc.
    // For now, we can't use print_string directly without vga context.
    // This part needs a proper screen print function accessible globally.
    // For this step, we'll leave it empty as screen printing from here is complex.
    // Consider having a global VGA write function if needed for debugging.

    // Example of how it might look if we had a global print function:
    // char* exception_messages[] = { ... };
    // print_global(exception_messages[r->int_no]); // r->int_no would be passed by assembly
    // if (r->err_code) print_global_hex(r->err_code);
    // For now, just hang:
    asm volatile("cli; hlt");
}


// C handler for IRQs (32-47), except for keyboard (IRQ1) which has its own path.
// This function will be called from the assembly IRQ stubs.
void irq_handler_c(struct idt_entry* r) { // Similar to fault_handler, 'r' is conceptual
    // Send EOI (End of Interrupt) signal to PICs.
    // If the IRQ came from the Master PIC (IRQ 0-7, which are int 32-39 after remapping)
    // send EOI only to Master.
    // If IRQ came from Slave PIC (IRQ 8-15, int 40-47 after remapping)
    // send EOI to both Slave and Master.

    // This function would receive the interrupt number.
    // For now, we assume the assembly stub that calls this *might* pass it.
    // However, our current stubs don't pass the interrupt number to this C function directly.
    // The EOI logic is better handled in the assembly stubs for IRQs if they are distinct,
    // or if this C function knew the IRQ number.

    // For simplicity, the EOI for IRQ1 is in its assembly stub.
    // For other IRQs, if they call this common handler, EOI needs to be managed here
    // or in their respective assembly stubs.
    // The current irq_common_stub in isr_stubs.s doesn't distinguish master/slave EOI.
    // This needs refinement.

    // Placeholder: if we knew the IRQ number (e.g., passed in 'r' or a global)
    // uint8_t irq_number = r->int_no - 32; // Convert IDT vector to IRQ number
    // if (irq_number >= 8) { // IRQ from slave
    //    outb(PIC2_COMMAND, PIC_EOI);
    // }
    // outb(PIC1_COMMAND, PIC_EOI);

    // For now, this common C handler does nothing beyond what the asm stub does.
    // The EOI for non-keyboard IRQs should be added to their asm stubs or here if info is passed.
}


// Initializes PICs and IDT entries for IRQs
void interrupts_init() {
    // Remap PIC to avoid conflicts with CPU exceptions.
    // IRQ 0-7  -> INT 0x20-0x27 (32-39)
    // IRQ 8-15 -> INT 0x28-0x2F (40-47)
    pic_remap(0x20, 0x28);

    // Setup IDT gates for all ISRs (0-31) - CPU exceptions
    // These point to the assembly stubs defined in isr_stubs.s
    // Selector 0x08 is the kernel code segment. Flags 0x8E for 32-bit interrupt gate.
    idt_set_gate(0, (uint32_t)isr0, 0x08, 0x8E);
    idt_set_gate(1, (uint32_t)isr1, 0x08, 0x8E);
    idt_set_gate(2, (uint32_t)isr2, 0x08, 0x8E);
    idt_set_gate(3, (uint32_t)isr3, 0x08, 0x8E);
    idt_set_gate(4, (uint32_t)isr4, 0x08, 0x8E);
    idt_set_gate(5, (uint32_t)isr5, 0x08, 0x8E);
    idt_set_gate(6, (uint32_t)isr6, 0x08, 0x8E);
    idt_set_gate(7, (uint32_t)isr7, 0x08, 0x8E);
    idt_set_gate(8, (uint32_t)isr8, 0x08, 0x8E);
    idt_set_gate(9, (uint32_t)isr9, 0x08, 0x8E);
    idt_set_gate(10, (uint32_t)isr10, 0x08, 0x8E);
    idt_set_gate(11, (uint32_t)isr11, 0x08, 0x8E);
    idt_set_gate(12, (uint32_t)isr12, 0x08, 0x8E);
    idt_set_gate(13, (uint32_t)isr13, 0x08, 0x8E);
    idt_set_gate(14, (uint32_t)isr14, 0x08, 0x8E);
    idt_set_gate(15, (uint32_t)isr15, 0x08, 0x8E);
    idt_set_gate(16, (uint32_t)isr16, 0x08, 0x8E);
    idt_set_gate(17, (uint32_t)isr17, 0x08, 0x8E);
    idt_set_gate(18, (uint32_t)isr18, 0x08, 0x8E);
    idt_set_gate(19, (uint32_t)isr19, 0x08, 0x8E);
    idt_set_gate(20, (uint32_t)isr20, 0x08, 0x8E);
    idt_set_gate(21, (uint32_t)isr21, 0x08, 0x8E);
    idt_set_gate(22, (uint32_t)isr22, 0x08, 0x8E);
    idt_set_gate(23, (uint32_t)isr23, 0x08, 0x8E);
    idt_set_gate(24, (uint32_t)isr24, 0x08, 0x8E);
    idt_set_gate(25, (uint32_t)isr25, 0x08, 0x8E);
    idt_set_gate(26, (uint32_t)isr26, 0x08, 0x8E);
    idt_set_gate(27, (uint32_t)isr27, 0x08, 0x8E);
    idt_set_gate(28, (uint32_t)isr28, 0x08, 0x8E);
    idt_set_gate(29, (uint32_t)isr29, 0x08, 0x8E);
    idt_set_gate(30, (uint32_t)isr30, 0x08, 0x8E);
    idt_set_gate(31, (uint32_t)isr31, 0x08, 0x8E);

    // Setup IDT gates for PIC IRQs (32-47)
    idt_set_gate(32, (uint32_t)irq0, 0x08, 0x8E);  // IRQ0  (Timer) -> INT 32
    idt_set_gate(33, (uint32_t)irq1, 0x08, 0x8E);  // IRQ1  (Keyboard) -> INT 33
    idt_set_gate(34, (uint32_t)irq2, 0x08, 0x8E);  // IRQ2  (Cascade) -> INT 34
    idt_set_gate(35, (uint32_t)irq3, 0x08, 0x8E);  // IRQ3  (COM2) -> INT 35
    idt_set_gate(36, (uint32_t)irq4, 0x08, 0x8E);  // IRQ4  (COM1) -> INT 36
    idt_set_gate(37, (uint32_t)irq5, 0x08, 0x8E);  // IRQ5  (LPT2) -> INT 37
    idt_set_gate(38, (uint32_t)irq6, 0x08, 0x8E);  // IRQ6  (Floppy) -> INT 38
    idt_set_gate(39, (uint32_t)irq7, 0x08, 0x8E);  // IRQ7  (LPT1/Spurious) -> INT 39
    idt_set_gate(40, (uint32_t)irq8, 0x08, 0x8E);  // IRQ8  (RTC) -> INT 40
    idt_set_gate(41, (uint32_t)irq9, 0x08, 0x8E);  // IRQ9  (Free) -> INT 41
    idt_set_gate(42, (uint32_t)irq10, 0x08, 0x8E); // IRQ10 (Free) -> INT 42
    idt_set_gate(43, (uint32_t)irq11, 0x08, 0x8E); // IRQ11 (Free) -> INT 43
    idt_set_gate(44, (uint32_t)irq12, 0x08, 0x8E); // IRQ12 (Mouse) -> INT 44
    idt_set_gate(45, (uint32_t)irq13, 0x08, 0x8E); // IRQ13 (FPU) -> INT 45
    idt_set_gate(46, (uint32_t)irq14, 0x08, 0x8E); // IRQ14 (ATA1) -> INT 46
    idt_set_gate(47, (uint32_t)irq15, 0x08, 0x8E); // IRQ15 (ATA2) -> INT 47

    // Enable interrupts on the CPU
    asm volatile ("sti");
}
