#include "task.h"
#include "kernel/mem/pmm.h"
#include "kernel/mem/vmm.h"
#include "kernel/elf.h"
#include "kernel/libc.h"
#include <stddef.h>
#include <stdint.h>
#include "kernel/debug_vga.h" // Pour debug_putc_at

// Sélecteurs de segment GDT pour l'espace utilisateur
const uint16_t USER_CODE_SELECTOR = 0x18 | 3;
const uint16_t USER_DATA_SELECTOR = 0x20 | 3;

// Fonctions externes
extern void context_switch(cpu_state_t* old_state, cpu_state_t* new_state);
extern uint32_t read_eip();

// Variables globales pour la gestion des tâches
volatile task_t* current_task = NULL;
volatile task_t* task_queue_head = NULL;
volatile uint32_t next_task_id = 1;

// Constantes pour les piles
#define KERNEL_TASK_STACK_SIZE PAGE_SIZE
#define USER_STACK_VIRTUAL_TOP         0xC0000000
#define USER_STACK_NUM_PAGES           4
#define USER_STACK_SIZE_BYTES          (USER_STACK_NUM_PAGES * PAGE_SIZE)
#define USER_STACK_VIRTUAL_BOTTOM      (USER_STACK_VIRTUAL_TOP - USER_STACK_SIZE_BYTES)

// Fonctions VGA externes pour le débogage via print_string (principalement pour les messages commentés)
extern void print_string(const char* str, char color);
extern char current_color;

void tasking_init() {
    asm volatile("cli");
    current_task = (task_t*)pmm_alloc_page();
    if (!current_task) {
        return;
    }
    current_task->id = next_task_id++;
    current_task->state = TASK_RUNNING;
    current_task->cpu_state.eip = 0;
    current_task->cpu_state.esp = 0;
    current_task->cpu_state.ebp = 0;
    current_task->cpu_state.eflags = 0x00000202;
    current_task->next = (struct task*)current_task;
    current_task->parent = NULL;
    current_task->child_pid_waiting_on = 0;
    task_queue_head = current_task;
    asm volatile("sti");
}

task_t* create_task(void (*entry_point)()) {
    asm volatile("cli");
    task_t* new_task = (task_t*)pmm_alloc_page();
    if (!new_task) {
        asm volatile("sti");
        return NULL;
    }
    memset(new_task, 0, sizeof(task_t));

    void* task_stack = pmm_alloc_page();
    if (!task_stack) {
        pmm_free_page(new_task);
        asm volatile("sti");
        return NULL;
    }
    new_task->id = next_task_id++;
    new_task->state = TASK_READY;
    new_task->parent = NULL;
    new_task->child_pid_waiting_on = 0;

    new_task->cpu_state.eflags = 0x202;
    new_task->cpu_state.eip = (uint32_t)(uintptr_t)entry_point;

    uint32_t stack_top = (uint32_t)(uintptr_t)task_stack + KERNEL_TASK_STACK_SIZE;
    new_task->cpu_state.esp = stack_top;
    new_task->cpu_state.ebp = 0;

    new_task->cpu_state.eax = 0;
    new_task->cpu_state.ebx = 0;
    new_task->cpu_state.ecx = 0;
    new_task->cpu_state.edx = 0;
    new_task->cpu_state.esi = 0;
    new_task->cpu_state.edi = 0;

    if (task_queue_head == NULL) {
        task_queue_head = new_task;
        new_task->next = new_task;
    } else {
        new_task->next = task_queue_head->next;
        task_queue_head->next = (struct task*)new_task;
    }
    asm volatile("sti");
    return new_task;
}

int create_user_process(const char* path_in_initrd, char* const argv_from_caller[]) {
    extern uint8_t _binary_shell_bin_start[];
    extern uint8_t _binary_shell_bin_end[];
    extern uint8_t _binary_fake_ai_bin_start[];
    extern uint8_t _binary_fake_ai_bin_end[];

    asm volatile("cli");

    uint8_t* elf_data = NULL;
    uint32_t elf_file_size = 0;

    if (strcmp(path_in_initrd, "shell.bin") == 0) {
        elf_data = _binary_shell_bin_start;
        elf_file_size = (uint32_t)(_binary_shell_bin_end - _binary_shell_bin_start);
    } else if (strcmp(path_in_initrd, "fake_ai.bin") == 0) {
        elf_data = _binary_fake_ai_bin_start;
        elf_file_size = (uint32_t)(_binary_fake_ai_bin_end - _binary_fake_ai_bin_start);
    } else {
        asm volatile("sti");
        return -1;
    }

    if (!elf_data || elf_file_size == 0) {
        asm volatile("sti");
        return -1;
    }

    task_t* new_task = (task_t*)pmm_alloc_page();
    if (!new_task) {
        asm volatile("sti");
        return -1;
    }
    memset(new_task, 0, sizeof(task_t));

    uint32_t entry_point = elf_load(elf_data);
    if (entry_point == 0) {
        pmm_free_page(new_task);
        asm volatile("sti");
        return -1;
    }

    for (int i = 0; i < USER_STACK_NUM_PAGES; ++i) {
        void* phys_page_for_stack = pmm_alloc_page();
        if (!phys_page_for_stack) {
            pmm_free_page(new_task);
            asm volatile("sti");
            return -1;
        }
        void* stack_page_vaddr = (void*)(USER_STACK_VIRTUAL_BOTTOM + i * PAGE_SIZE);
        vmm_map_user_page(stack_page_vaddr, phys_page_for_stack);
    }

    uint32_t esp_user_initial = USER_STACK_VIRTUAL_TOP;
    int argc = 0;
    size_t total_argv_strlen = 0;
    if (argv_from_caller) {
        for (argc = 0; argv_from_caller[argc] != NULL; argc++) {
            const char* arg = argv_from_caller[argc];
            size_t len = 0;
            while(arg[len]) len++;
            total_argv_strlen += (len + 1);
        }
    }

    uint32_t current_esp_user = esp_user_initial;
    current_esp_user -= total_argv_strlen;
    char* string_area_on_stack_start = (char*)(uintptr_t)current_esp_user;
    char* argv_on_stack_pointers[argc + 1];
    char* current_string_write_ptr = string_area_on_stack_start;
    for (int i = 0; i < argc; i++) {
        const char* arg_source = argv_from_caller[i];
        size_t len = 0;
        while(arg_source[len]) len++;
        memcpy(current_string_write_ptr, arg_source, len + 1);
        argv_on_stack_pointers[i] = current_string_write_ptr;
        current_string_write_ptr += (len + 1);
    }
    argv_on_stack_pointers[argc] = NULL;

    current_esp_user -= (argc + 1) * sizeof(char*);
    memcpy((void*)(uintptr_t)current_esp_user, argv_on_stack_pointers, (argc + 1) * sizeof(char*));
    new_task->argv_user_stack_ptr = (char**)(uintptr_t)current_esp_user;

    current_esp_user -= sizeof(int);
    *(int*)(uintptr_t)current_esp_user = argc;
    new_task->argc = argc;

    void* kernel_stack_ptr = pmm_alloc_page();
    if (!kernel_stack_ptr) {
        pmm_free_page(new_task);
        asm volatile("sti");
        return -1;
    }
    uint32_t kernel_stack_top = (uint32_t)(uintptr_t)kernel_stack_ptr + KERNEL_TASK_STACK_SIZE;

    new_task->id = next_task_id++;
    new_task->state = TASK_READY;
    new_task->parent = (struct task*)current_task;
    new_task->child_pid_waiting_on = 0;

    memset(&new_task->cpu_state, 0, sizeof(cpu_state_t));

    uint32_t* kstack = (uint32_t*)kernel_stack_top;

    *(--kstack) = USER_DATA_SELECTOR;
    *(--kstack) = current_esp_user;
    *(--kstack) = 0x00000202;
    *(--kstack) = USER_CODE_SELECTOR;
    *(--kstack) = entry_point;

    *(--kstack) = 0x00000202;

    *(--kstack) = 0; // EAX
    *(--kstack) = 0; // ECX
    *(--kstack) = 0; // EDX
    *(--kstack) = 0; // EBX
    *(--kstack) = 0; // ESP_original_dummy
    *(--kstack) = 0; // EBP
    *(--kstack) = 0; // ESI
    *(--kstack) = 0; // EDI

    new_task->cpu_state.esp = (uint32_t)(uintptr_t)kstack;
    new_task->cpu_state.eip = entry_point;
    new_task->cpu_state.eflags = 0x00000202;

    if (task_queue_head == NULL) {
        task_queue_head = new_task;
        new_task->next = new_task;
    } else {
        new_task->next = task_queue_head->next;
        task_queue_head->next = (struct task*)new_task;
    }

    asm volatile("sti");
    return new_task->id;
}

static char schedule_debug_char_val = '0';

void schedule() {
    debug_putc_at(schedule_debug_char_val, 78, 0, 0x0E); // Jaune sur Noir, (x=78, y=0)
    schedule_debug_char_val++;
    if (schedule_debug_char_val > '9') schedule_debug_char_val = '0';

    if (!current_task) {
        debug_putc_at('S', 0, 1, 0x0C); debug_putc_at('C', 1, 1, 0x0C); debug_putc_at('H', 2, 1, 0x0C);
        debug_putc_at('N', 4, 1, 0x0C); debug_putc_at('C', 5, 1, 0x0C); debug_putc_at('T', 6, 1, 0x0C); // Sched NoCurTask
        return;
    }

    task_t* prev_task = (task_t*)current_task;
    task_t* next_candidate = (task_t*)current_task->next;

    while ((next_candidate->state == TASK_TERMINATED ||
            next_candidate->state == TASK_WAITING_FOR_KEYBOARD ||
            next_candidate->state == TASK_WAITING_FOR_CHILD) &&
           next_candidate != current_task) {
        next_candidate = (task_t*)next_candidate->next;

        if (next_candidate == current_task) {
            if (current_task->state == TASK_TERMINATED ||
                current_task->state == TASK_WAITING_FOR_KEYBOARD ||
                current_task->state == TASK_WAITING_FOR_CHILD) {
                asm volatile("cli; hlt");
                return;
            }
            return;
        }
    }

    if (prev_task->state == TASK_RUNNING) {
        prev_task->state = TASK_READY;
    }

    if (next_candidate->state == TASK_TERMINATED ||
        next_candidate->state == TASK_WAITING_FOR_KEYBOARD ||
        next_candidate->state == TASK_WAITING_FOR_CHILD) {
        asm volatile("cli; hlt");
        return;
    }

    next_candidate->state = TASK_RUNNING;
    current_task = next_candidate;

    if (prev_task == current_task && current_task->state == TASK_TERMINATED) {
        asm volatile("cli; hlt");
        return;
    }

    // Debug: Afficher l'ID de la tâche vers laquelle on commute.
    // print_string(" Switching to PID: ", current_color);
    // char pid_str[12];
    // itoa(current_task->id, pid_str, 10);
    // print_string(pid_str, current_color);
    // print_string("\n", current_color);

    context_switch(&prev_task->cpu_state, (cpu_state_t*)&current_task->cpu_state);
}

// Tâche noyau worker simple pour le débogage
static char worker_task_char = 'W';
void kernel_worker_task_main() {
    debug_putc_at('K', 0, 2, 0x0A); // 'K'ernel 'W'orker 'S'tarted
    debug_putc_at('W', 1, 2, 0x0A);
    debug_putc_at('S', 2, 2, 0x0A);

    while(1) {
        debug_putc_at(worker_task_char, 68, 0, 0x0A); // Vert à (68,0)
        if (worker_task_char == 'W') worker_task_char = 'V';
        else worker_task_char = 'W';

        // Petite boucle pour ralentir l'affichage et céder potentiellement le CPU implicitement
        // via hlt si le timer interrompt pendant ce temps.
        // Un vrai OS utiliserait un appel système pour dormir ou attendre.
        for (volatile int i = 0; i < 5000000; ++i) {
            // Boucle d'attente simple pour rendre le changement visible
            // et permettre au timer d'interrompre.
        }
        // asm volatile("hlt"); // On pourrait aussi faire hlt pour attendre la prochaine interruption timer
    }
}
