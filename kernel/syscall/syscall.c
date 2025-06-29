#include "syscall.h"
#include "kernel/idt.h"        // Pour idt_set_gate et la définition de cpu_state_t (via interrupts.h)
#include "kernel/task/task.h"  // Pour current_task, TASK_TERMINATED, TASK_WAITING_FOR_KEYBOARD, schedule(), cpu_state_t
#include "kernel/keyboard.h"   // Pour keyboard_prepare_for_gets, keyboard_get_chars_read_count
#include <stdint.h>
#include <stddef.h>     // Pour NULL

// Déclarations externes pour les variables et fonctions VGA (probablement de kernel.c ou vga.c)
extern int vga_x;
extern int vga_y;
extern char current_color;
extern void print_char(char c, int x, int y, char color);
// extern void print_string(const char* str, char color); // Si besoin pour debug

// Tâche courante (définie dans task.c)
extern volatile task_t* current_task;

// Handler assembleur pour int 0x80 (défini dans syscall_handler.s ou similaire)
extern void syscall_interrupt_handler_asm();

// Le handler C appelé par le stub assembleur de l'int 0x80
// Le pointeur 'stack_ptr_raw' pointe vers GS sauvegardé sur la pile.
// La structure cpu_state_t de task.h n'est PAS directement mappée ici.
// Nous accédons aux registres par offset.
// PUSHAD (NASM order): EDI, ESI, EBP, ESP_dummy, EBX, EDX, ECX, EAX (EDI is at [ESP_after_pushad])
// Sur la pile: [GS, FS, ES, DS, EDI, ESI, EBP, ESP_d, EBX, EDX, ECX, EAX]
// Index:        0   1   2   3   4    5    6     7      8    9    10   11
#define STACK_IDX_GS  0
#define STACK_IDX_FS  1
#define STACK_IDX_ES  2
#define STACK_IDX_DS  3
#define STACK_IDX_EDI 4
#define STACK_IDX_ESI 5
#define STACK_IDX_EBP 6
#define STACK_IDX_ESP_KERNEL_DUMMY 7
#define STACK_IDX_EBX 8
#define STACK_IDX_EDX 9
#define STACK_IDX_ECX 10
#define STACK_IDX_EAX 11

void syscall_handler(void* stack_ptr_raw) { // Le type cpu_state_t* est trompeur ici.
    // Test d'entrée ultra-basique: change la couleur de fond du premier caractère à l'écran
    // pour prouver que cette fonction C est atteinte.
    // volatile unsigned short* video_mem_test = (unsigned short*)0xB8000;
    // video_mem_test[0] = (video_mem_test[0] & 0x00FF) | (0x4F00); // Fond Rouge, Caractère inchangé

    uint32_t* regs = (uint32_t*)stack_ptr_raw;

    // Déclarations pour print_string/print_hex au cas où elles seraient nécessaires avant le switch
    extern void print_string(const char* str, char color);
    extern void print_hex(uint32_t n, char color);

    // Imprimer un message dès l'entrée pour confirmer l'appel et l'état de current_task
    // Utiliser une couleur distincte, par exemple magenta sur noir (0x05)
    print_string("SYSHNDLR_ENTRY\n", 0x05);
    if (!current_task) {
        print_string("SYSHNDLR_ERR: current_task is NULL\n", 0x05);
    } else {
        print_string("SYSHNDLR_INFO: current_task ID: ", 0x05); print_hex(current_task->id, 0x05); print_string("\n", 0x05);
    }
    if (!regs) {
        print_string("SYSHNDLR_ERR: regs is NULL\n", 0x05);
        // Ne pas essayer d'écrire dans regs[STACK_IDX_EAX] si regs est NULL
        return; // Retour précoce critique
    }


    if (!regs || !current_task) {
        if (regs) regs[STACK_IDX_EAX] = (uint32_t)-1;
        return;
    }

    uint32_t syscall_number = regs[STACK_IDX_EAX];

    switch (syscall_number) {
        case 0: // SYS_EXIT
            current_task->state = TASK_TERMINATED;
            if (current_task->parent &&
                current_task->parent->state == TASK_WAITING_FOR_CHILD &&
                current_task->parent->child_pid_waiting_on == current_task->id) {
                current_task->parent->state = TASK_READY;
                current_task->parent->child_exit_status = (int)regs[STACK_IDX_EBX]; // Status de sortie dans EBX
                current_task->parent->child_pid_waiting_on = 0;
            }
            schedule();
            break;

        case 1: // SYS_PUTC
            print_char((char)regs[STACK_IDX_EBX], vga_x, vga_y, current_color);
            regs[STACK_IDX_EAX] = 0;
            break;

        case 4: // SYS_GETS
            {
                char* user_buf = (char*)regs[STACK_IDX_EBX];
                uint32_t user_buf_size = regs[STACK_IDX_ECX];
                // Utiliser print_string et print_hex qui sont dans libc et kernel.c
                extern void print_string(const char* str, char color);
                extern void print_hex(uint32_t n, char color);

                print_string("DEBUG_SYS_GETS: Entered. Task ID: ", 0x0F); print_hex(current_task->id, 0x0F);
                print_string(", user_buf: ", 0x0F); print_hex((uint32_t)user_buf, 0x0F);
                print_string(", user_buf_size: ", 0x0F); print_hex(user_buf_size, 0x0F); print_string("\n", 0x0F);

                if (user_buf == NULL || user_buf_size == 0) {
                    print_string("DEBUG_SYS_GETS: Invalid buffer or size. Returning -1.\n", 0x0F);
                    regs[STACK_IDX_EAX] = (uint32_t)-1;
                    break;
                }
                keyboard_prepare_for_gets(user_buf, user_buf_size); // current_task est assigné à task_waiting_for_input
                current_task->state = TASK_WAITING_FOR_KEYBOARD;
                print_string("DEBUG_SYS_GETS: Task ID ", 0x0F); print_hex(current_task->id, 0x0F); print_string(" state set to WAITING_FOR_KEYBOARD. Calling schedule().\n", 0x0F);

                schedule();

                // Après schedule(), current_task est la tâche qui vient d'être réveillée ou sélectionnée.
                // Si c'est la tâche qui a appelé SYS_GETS (et qui s'est réveillée), son current_task->syscall_retval est pertinent.
                print_string("DEBUG_SYS_GETS: Returned from schedule(). Current (awakened/switched) Task ID: ", 0x0F); print_hex(current_task->id, 0x0F);
                print_string(". syscall_retval for this task: ", 0x0F); print_hex(current_task->syscall_retval, 0x0F); print_string("\n", 0x0F);

                regs[STACK_IDX_EAX] = current_task->syscall_retval; // regs est toujours la pile de la tâche qui a initié le syscall
                print_string("DEBUG_SYS_GETS: Set EAX (for original calling task) to syscall_retval. Exiting syscall.\n", 0x0F);
            }
            break;

        case 5: // SYS_EXEC
            {
                const char* path = (const char*)regs[STACK_IDX_EBX];
                char** argv = (char**)regs[STACK_IDX_ECX];

                if (path == NULL) {
                    regs[STACK_IDX_EAX] = (uint32_t)-1;
                    break;
                }
                int child_pid = create_user_process(path, argv);

                if (child_pid < 0) {
                    regs[STACK_IDX_EAX] = (uint32_t)-1;
                } else {
                    current_task->state = TASK_WAITING_FOR_CHILD;
                    current_task->child_pid_waiting_on = child_pid;
                    schedule();
                    // Au réveil, le statut de sortie de l'enfant est dans current_task->child_exit_status
                    regs[STACK_IDX_EAX] = (uint32_t)current_task->child_exit_status;
                }
            }
            break;

        default:
            regs[STACK_IDX_EAX] = (uint32_t)-1;
            break;
    }
}

void syscall_init() {
    // Déclarations externes pour print_string/print_hex si elles ne sont pas déjà visibles globalement
    // extern void print_string(const char* str, char color); // Normalement visible via syscall.h -> task.h -> ... -> libc.h
    // extern void print_hex(uint32_t n, char color);       // Idem

    print_string("DEBUG_SYSCALL_INIT: Entered.\n", 0x0D);

    // Enregistre syscall_interrupt_handler_asm pour l'interruption 0x80.
    // Flags 0xEE: Présent, DPL=3 (user mode), Trap Gate 32-bit.
    // Sélecteur 0x08: segment de code du noyau.
    uint32_t handler_address = (uint32_t)syscall_interrupt_handler_asm;
    print_string("DEBUG_SYSCALL_INIT: Address of syscall_interrupt_handler_asm: ", 0x0D);
    print_hex(handler_address, 0x0D);
    print_string("\n", 0x0D);

    idt_set_gate(0x80, handler_address, 0x08, 0xEE);
    print_string("DEBUG_SYSCALL_INIT: idt_set_gate(0x80, ...) called.\n", 0x0D);
    // print_string("Syscall handler registered for int 0x80.\n", 0x0F); // Ancien Debug
}
