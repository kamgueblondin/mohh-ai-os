#include "gdt.h"
#include "idt.h"
#include "interrupts.h"
#include "keyboard.h"
#include "mem/pmm.h"
#include "mem/vmm.h"
#include "task/task.h" // Pour create_task et task_t
#include "timer.h"
#include "syscall/syscall.h"
#include "libc.h"
#include <stdint.h>
#include "debug_vga.h"       // Pour debug_putc_at

// Pointeur vers la mémoire vidéo VGA. L'adresse 0xB8000 est standard.
volatile unsigned short* vga_buffer = (unsigned short*)0xB8000;
// Position actuelle du curseur
int vga_x = 0;
int vga_y = 0;
char current_color = 0x1F; // Couleur par défaut

void print_char(char c, int x, int y, char color) {
    if (c == '\n') {
        vga_x = 0;
        vga_y++;
    } else if (c == '\b') {
        if (vga_x > 0) {
            vga_x--;
            vga_buffer[vga_y * 80 + vga_x] = (unsigned short)' ' | (unsigned short)color << 8;
        } else if (vga_y > 0) {
            vga_y--;
            vga_x = 79;
            vga_buffer[vga_y * 80 + vga_x] = (unsigned short)' ' | (unsigned short)color << 8;
        }
    } else {
        vga_buffer[y * 80 + x] = (unsigned short)c | (unsigned short)color << 8;
        vga_x++;
    }

    if (vga_x >= 80) {
        vga_x = 0;
        vga_y++;
    }
    if (vga_y >= 25) {
        for (int i = 0; i < 24 * 80; i++) {
            vga_buffer[i] = vga_buffer[i + 80];
        }
        for (int i = 24 * 80; i < 25 * 80; i++) {
            vga_buffer[i] = (unsigned short)' ' | (unsigned short)color << 8;
        }
        vga_x = 0;
        vga_y = 24;
    }
}

void print_string(const char* str, char color) {
    for (int i = 0; str[i] != '\0'; i++) {
        print_char(str[i], vga_x, vga_y, color);
    }
}

void clear_screen(char color) {
    for (int y = 0; y < 25; y++) {
        for (int x = 0; x < 80; x++) {
            vga_buffer[y * 80 + x] = (unsigned short)' ' | (unsigned short)color << 8;
        }
    }
    vga_x = 0;
    vga_y = 0;
}

void debug_putc_at(char c, int x, int y, char color) {
    if (x >= 0 && x < 80 && y >= 0 && y < 25) {
        vga_buffer[y * 80 + x] = (unsigned short)c | (unsigned short)color << 8;
    }
}

void kmain(uint32_t physical_pd_addr) {
    current_color = 0x1F;
    clear_screen(current_color);

    asm volatile (
        "mov %0, %%eax\n\t"
        "mov %%eax, %%cr3"
        : : "r"(physical_pd_addr) : "eax");

    uint32_t temp_cr0;
    asm volatile("mov %%cr0, %0" : "=r"(temp_cr0));
    if (!(temp_cr0 & 0x80000000)) {
        temp_cr0 |= 0x80000000;
        asm volatile("mov %0, %%cr0" : : "r"(temp_cr0));
    }

    uint32_t kernel_end_addr = 0;
    uint32_t multiboot_addr = 0;

    uint32_t total_memory_bytes = 16 * 1024 * 1024;
    pmm_init(total_memory_bytes, kernel_end_addr, multiboot_addr);

    vmm_init();
    print_string("Gestionnaires PMM et VMM initialises.\n", current_color);

    gdt_init();
    print_string("GDT initialisee.\n", current_color);
    idt_init();
    interrupts_init();
    syscall_init();
    print_string("IDT, PIC et Appels Systeme initialises.\n", current_color);

    // TEST: Forcer une division par zéro pour vérifier fault_handler APRÈS init des interruptions
    volatile int test_x = 5;
    volatile int test_y = 0;
    volatile int test_z = test_x / test_y;
    (void)test_z; // Pour éviter l'avertissement "unused variable"


    tasking_init();
    print_string("Multitache initialise.\n", current_color);

    extern void kernel_worker_task_main();

    print_string("Lancement de la tache worker noyau...\n", current_color);
    task_t* worker_task = create_task(kernel_worker_task_main);
    if (worker_task == NULL) {
        print_string("Echec du lancement de la tache worker. Arret.\n", 0x0C);
        while(1) asm volatile("cli; hlt");
    } else {
        print_string("Tache worker lancee avec PID: ", current_color);
        char pid_str[12];
        itoa(worker_task->id, pid_str, 10);
        print_string(pid_str, current_color);
        print_string(" (DEBUG KMAIN)\n", current_color);
    }

    /* Shell utilisateur commenté
    print_string("Lancement du shell...\n", current_color);
    char* shell_argv[] = {"shell.bin", NULL};
    int shell_pid = create_user_process("shell.bin", shell_argv);
    if (shell_pid < 0) {
        print_string("Echec du lancement de shell.bin. Arret du systeme.\n", 0x0C);
        while(1) asm volatile("cli; hlt");
    } else {
        print_string("shell.bin lance avec PID: ", current_color);
        char pid_str_shell[12];
        itoa(shell_pid, pid_str_shell, 10);
        print_string(pid_str_shell, current_color);
        print_string(" (DEBUG KMAIN)\n", current_color);
    }
    */

    print_string("AVANT TIMER_INIT\n", current_color);
    timer_init(100);
    print_string("APRES TIMER_INIT\n", current_color);
    print_string("Timer systeme active a 100Hz.\n", current_color);

    print_string("Systeme AI-OS operationnel. Passage au mode inactif.\n", current_color);

    while(1) {
        asm volatile("hlt");
    }
}
