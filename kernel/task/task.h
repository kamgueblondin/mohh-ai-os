#ifndef TASK_H
#define TASK_H

#include <stdint.h>

// Structure pour sauvegarder l'état du CPU
typedef struct cpu_state {
    uint32_t eax, ebx, ecx, edx;
    uint32_t esi, edi, ebp;
    uint32_t eip, esp, eflags;
} cpu_state_t;

// Énumération pour l'état de la tâche
typedef enum task_state {
    TASK_RUNNING,
    TASK_READY,
    TASK_TERMINATED, // Nouvel état pour les tâches qui ont terminé leur exécution
} task_state_t;

// Structure pour une tâche
typedef struct task {
    int id;
    cpu_state_t cpu_state;
    task_state_t state;
    struct task* next;
} task_t;

void tasking_init();
task_t* create_task(void (*entry_point)()); // Pour les tâches noyau Ring 0
task_t* create_user_task(uint32_t entry_point_addr); // Pour les tâches utilisateur Ring 3
void schedule();

#endif // TASK_H
