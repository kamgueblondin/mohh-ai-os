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
    TASK_TERMINATED,
    TASK_WAITING_FOR_KEYBOARD, // Nouvelle état pour SYS_GETS
    TASK_WAITING_FOR_CHILD,  // Pour exec bloquant
} task_state_t;

// Structure pour une tâche
typedef struct task {
    int id;                 // Identifiant unique de la tâche
    cpu_state_t cpu_state;  // État des registres du CPU
    task_state_t state;     // État actuel de la tâche (RUNNING, READY, etc.)
    struct task* next;      // Pointeur vers la prochaine tâche dans la liste chaînée

    // Pour la relation parent/enfant et exec() bloquant
    struct task* parent;        // Tâche parente (celle qui a appelé exec)
    int child_pid_waiting_on; // PID de l'enfant que cette tâche attend (si elle est parent)
    int child_exit_status;    // Statut de sortie de l'enfant attendu
    uint32_t syscall_retval;  // Valeur de retour générique pour les syscalls bloquants

    // Informations pour le démarrage d'un processus utilisateur
    // Ces champs pourraient être dans une structure séparée si task_t devient trop gros.
    int argc;
    char** argv_user_stack_ptr; // Pointeur vers argv sur la pile de l'utilisateur

    // Plus tard: page_directory_t* page_directory; // Propre à chaque tâche
} task_t;

void tasking_init();
// create_task est pour les tâches noyau simples.
task_t* create_task(void (*entry_point)());

// create_user_process est pour charger et exécuter un programme ELF.
// Renvoie le PID du nouveau processus, ou -1 en cas d'erreur.
int create_user_process(const char* path_in_initrd, char* const argv_from_caller[]);

void schedule();

// Déclaration pour les sélecteurs de segment GDT (à définir dans gdt.c/gdt.h)
extern const uint16_t USER_CODE_SELECTOR;
extern const uint16_t USER_DATA_SELECTOR;

#endif // TASK_H
