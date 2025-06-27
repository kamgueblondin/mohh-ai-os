#ifndef TASK_H
#define TASK_H

#include <stdint.h>

// Structure pour sauvegarder l'état du CPU
// L'ordre des champs est important pour context_switch et iret.
// Les registres généraux sont typiquement sauvegardés par PUSHAD/POPAD.
// Les segments et eip/eflags/esp_user sont pour la frame IRET.
typedef struct cpu_state {
    // Registres sauvegardés par PUSHAD (dans l'ordre inverse de PUSHAD pour faciliter la lecture)
    uint32_t edi, esi, ebp, esp_dummy; // esp_dummy est une placeholder, le vrai esp est géré par esp_kernel
    uint32_t ebx, edx, ecx, eax;

    // Registres de segment (pour le mode utilisateur, ou si la tâche noyau utilise des segments différents)
    uint32_t gs, fs, es, ds; // cs et ss sont gérés plus spécifiquement pour iret

    // Pour iret (et le saut initial via ret pour les tâches noyau)
    uint32_t eip;       // Instruction pointer
    uint32_t cs;        // Code segment for iret
    uint32_t eflags;    // Flags register

    // Uniquement pour iret vers un niveau de privilège différent (ex: noyau vers utilisateur)
    uint32_t esp_user;  // User mode stack pointer (si CPL change)
    uint32_t ss_user;   // User mode stack segment (si CPL change)
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

    // Informations pour le démarrage d'un processus utilisateur
    // Ces champs pourraient être dans une structure séparée si task_t devient trop gros.
    int argc;
    char** argv_user_stack_ptr; // Pointeur vers argv sur la pile de l'utilisateur

    // Gestion de la pile noyau
    uint32_t esp_kernel;        // Pointeur vers le sommet de la pile noyau sauvegardée
    uint32_t kernel_stack_top;  // Adresse de base (sommet initial) de la pile noyau allouée

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
