#include "task.h"
#include "kernel/mem/pmm.h" // Pour pmm_alloc_page, PAGE_SIZE
#include "kernel/mem/vmm.h" // Pour vmm_map_user_page
#include "fs/initrd.h"      // Pour initrd_read_file
#include "kernel/elf.h"     // Pour elf_load
#include "kernel/libc.h"    // Pour memcpy, memset, strcmp
#include <stddef.h>
#include <stdint.h>

// Sélecteurs GDT pour l'espace utilisateur (avec RPL=3)
// Ces valeurs doivent correspondre à votre configuration GDT.
const uint16_t USER_CODE_SELECTOR = 0x18 | 3; // 0x1B
const uint16_t USER_DATA_SELECTOR = 0x20 | 3; // 0x23


extern void context_switch(cpu_state_t* old_state, cpu_state_t* new_state);
extern uint32_t read_eip(); // Pour le premier point d'entrée de la tâche noyau

volatile task_t* current_task = NULL;
volatile task_t* task_queue_head = NULL;
volatile uint32_t next_task_id = 1; // Commencer les PID à 1 (0 pourrait être réservé)

#define KERNEL_TASK_STACK_SIZE PAGE_SIZE
// #define USER_STACK_PAGES 1 // Defined in task.h now or locally
// #define USER_STACK_SIZE (USER_STACK_PAGES * PAGE_SIZE) // Defined in task.h now

// User stack constants (can also be in a dedicated memory layout header)
#define USER_STACK_VIRTUAL_TOP         0xC0000000 // Example: 3GB mark
#define USER_STACK_NUM_PAGES           4          // Example: 16KB stack
#define USER_STACK_SIZE_BYTES          (USER_STACK_NUM_PAGES * PAGE_SIZE)
#define USER_STACK_VIRTUAL_BOTTOM      (USER_STACK_VIRTUAL_TOP - USER_STACK_SIZE_BYTES)


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
    // eflags pour la tâche initiale du noyau (idle task). IF = 0 pour l'instant.
    current_task->cpu_state.eflags = 0x00000002; // Bit 1 est toujours 1.
    current_task->next = (struct task*)current_task;
    current_task->parent = NULL;
    current_task->child_pid_waiting_on = 0;
    task_queue_head = current_task;
    asm volatile("sti");
}

// Crée une tâche noyau simple (Ring 0)
task_t* create_task(void (*entry_point)()) {
    asm volatile("cli");
    task_t* new_task = (task_t*)pmm_alloc_page(); // Alloue pour la TCB
    if (!new_task) {
        asm volatile("sti");
        return NULL;
    }
    memset(new_task, 0, sizeof(task_t)); // Initialise la TCB à zéro

    void* task_stack = pmm_alloc_page(); // Alloue pour la pile noyau
    if (!task_stack) {
        pmm_free_page(new_task);
        asm volatile("sti");
        return NULL;
    }
    new_task->id = next_task_id++;
    new_task->state = TASK_READY;
    new_task->parent = NULL; // Les tâches noyau directes n'ont pas de parent de cette manière
    new_task->child_pid_waiting_on = 0;

    // Configuration de la pile pour Ring 0
    // Pour une tâche noyau, CS/SS sont les sélecteurs noyau.
    // EFLAGS: IF=1 (0x200) + bit 1 (0x2) = 0x202
    new_task->cpu_state.eflags = 0x202;
    new_task->cpu_state.eip = (uint32_t)entry_point;

    // Initialisation de la pile noyau. L'ESP pointe au sommet de la pile.
    // La pile grandit vers le bas.
    uint32_t stack_top = (uint32_t)task_stack + KERNEL_TASK_STACK_SIZE;
    new_task->cpu_state.esp = stack_top;
    // Pour une tâche noyau démarrant à entry_point, on n'a pas besoin de pousser grand chose sur la pile
    // car elle ne fait pas "iret". Mais si entry_point est une fonction C, elle s'attendra à un ebp valide.
    new_task->cpu_state.ebp = 0; // Ou stack_top, selon les conventions. 0 est commun.

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


// Crée un processus utilisateur à partir d'un fichier ELF dans l'initrd
// path_in_initrd: nom du fichier exécutable dans l'initrd
// argv_from_caller: tableau de chaînes d'arguments (style main(argc, argv))
//                   Le dernier élément doit être NULL.
// Retourne le PID du nouveau processus, ou -1 en cas d'erreur.
int create_user_process(const char* path_in_initrd, char* const argv_from_caller[]) {
    asm volatile("cli"); // Désactiver les interruptions pendant la création

    uint32_t elf_file_size = 0;
    uint8_t* elf_data = (uint8_t*)initrd_read_file(path_in_initrd, &elf_file_size);

    if (!elf_data || elf_file_size == 0) {
        // print_string("ELF file not found or empty: ", 0x0C); print_string(path_in_initrd, 0x0C); print_char('\n',0,0,0);
        asm volatile("sti");
        return -1; // Erreur: Fichier non trouvé ou vide
    }

    task_t* new_task = (task_t*)pmm_alloc_page(); // Allocation pour la TCB
    if (!new_task) {
        // print_string("Failed to allocate TCB for new process\n", 0x0C);
        asm volatile("sti");
        return -1;
    }
    memset(new_task, 0, sizeof(task_t));

    // Charger l'ELF. elf_load s'occupe d'allouer les pages physiques
    // et de les mapper dans l'espace virtuel via vmm_map_user_page.
    // IMPORTANT: elf_load doit être conscient qu'il ne peut pas simplement mapper à des adresses physiques.
    // Il doit choisir des adresses virtuelles utilisateur (par exemple, à partir de 0x00010000 ou 0x08048000).
    uint32_t entry_point = elf_load(elf_data); // elf_load needs to handle virtual addresses.
    if (entry_point == 0) {
        // print_string("Failed to load ELF: ", 0x0C); print_string(path_in_initrd, 0x0C); print_char('\n',0,0,0);
        pmm_free_page(new_task);
        asm volatile("sti");
        return -1;
    }

    // Allouer et mapper la pile utilisateur dans la plage virtuelle définie
    // USER_STACK_VIRTUAL_BOTTOM à USER_STACK_VIRTUAL_TOP
    for (int i = 0; i < USER_STACK_NUM_PAGES; ++i) {
        void* phys_page_for_stack = pmm_alloc_page();
        if (!phys_page_for_stack) {
            // print_string("Failed to allocate user stack page\n", 0x0C);
            // TODO: Libérer les pages ELF et la TCB. Cela nécessite de savoir quelles pages ELF a allouées.
            // Pour l'instant, libération simple de la TCB.
            pmm_free_page(new_task);
            asm volatile("sti");
            return -1; // Erreur
        }
        // Calcule l'adresse virtuelle pour la page de pile actuelle (en partant du bas)
        void* stack_page_vaddr = (void*)(USER_STACK_VIRTUAL_BOTTOM + i * PAGE_SIZE);
        vmm_map_user_page(stack_page_vaddr, phys_page_for_stack);
    }

    // ESP pour le mode utilisateur doit pointer vers le sommet de la pile allouée.
    uint32_t esp_user_initial = USER_STACK_VIRTUAL_TOP;

    // Préparer argc et argv sur la pile utilisateur
    // 1. Compter argc et calculer la taille totale des chaînes argv
    int argc = 0;
    size_t total_argv_strlen = 0;
    if (argv_from_caller) {
        for (argc = 0; argv_from_caller[argc] != NULL; argc++) {
            const char* arg = argv_from_caller[argc];
            size_t len = 0;
            while(arg[len]) len++; // strlen basique
            total_argv_strlen += (len + 1); // +1 pour le NUL terminateur
        }
    }

    // L'espace nécessaire sur la pile: total_argv_strlen + (argc + 1) * sizeof(char*) + sizeof(int) pour argc
    // On va placer les chaînes tout en haut de la pile (aux adresses les plus basses de la zone argc/argv),
    // puis les pointeurs, puis argc. ESP pointera finalement vers argc.
    uint32_t current_esp_user = esp_user_initial;

    // Copier les chaînes d'arguments sur la pile utilisateur
    // Les chaînes sont copiées en premier, aux adresses les plus hautes de la zone de pile utilisée par argv et les chaînes.
    // current_esp_user descendra au fur et à mesure.
    current_esp_user -= total_argv_strlen;
    char* string_area_on_stack_start = (char*)current_esp_user; // Début de la zone où les chaînes sont stockées

    char* argv_on_stack_pointers[argc + 1]; // Tableau temporaire pour stocker les adresses virtuelles des chaînes sur la pile

    char* current_string_write_ptr = string_area_on_stack_start;
    for (int i = 0; i < argc; i++) {
        const char* arg_source = argv_from_caller[i];
        size_t len = 0;
        while(arg_source[len]) len++;
        memcpy(current_string_write_ptr, arg_source, len + 1);
        argv_on_stack_pointers[i] = current_string_write_ptr; // C'est l'adresse virtuelle sur la pile utilisateur
        current_string_write_ptr += (len + 1);
    }
    argv_on_stack_pointers[argc] = NULL; // Dernier élément de argv est NULL

    // Pousser les pointeurs argv (qui pointent vers les chaînes déjà sur la pile)
    // Ces pointeurs sont poussés après les chaînes elles-mêmes.
    current_esp_user -= (argc + 1) * sizeof(char*);
    memcpy((void*)current_esp_user, argv_on_stack_pointers, (argc + 1) * sizeof(char*));
    new_task->argv_user_stack_ptr = (char**)current_esp_user; // Sauvegarder où le tableau argv[] est sur la pile

    // Pousser argc
    current_esp_user -= sizeof(int);
    *(int*)current_esp_user = argc;
    new_task->argc = argc;

    // Initialiser l'état du CPU pour la nouvelle tâche
    new_task->id = next_task_id++;
    new_task->state = TASK_READY;
    new_task->parent = (struct task*)current_task; // La tâche appelante est le parent
    new_task->child_pid_waiting_on = 0; // L'enfant n'attend personne au début

    memset(&new_task->cpu_state, 0, sizeof(cpu_state_t));
    new_task->cpu_state.eip = entry_point;
    new_task->cpu_state.esp = current_esp_user; // ESP pointe vers argc sur la pile utilisateur
    new_task->cpu_state.ebp = 0;                // Ou current_esp_user, ou USER_STACK_VIRTUAL_TOP. 0 est commun pour le _start.

    // EFLAGS: IF=1 (interruptions activées), IOPL=0 (pas d'accès I/O direct pour user), bit 1 toujours à 1.
    new_task->cpu_state.eflags = 0x00000202;
                                        // Pour IOPL=3 (accès aux ports), il faudrait 0x3202.
                                        // Les syscalls wrappers utilisent int 0x80, donc IOPL n'est pas critique ici.

    // Segments utilisateur (doivent être configurés dans la GDT)
    // TODO: La structure cpu_state_t actuelle ne stocke pas les sélecteurs de segment.
    // Ils devront être mis sur la pile d'une manière ou d'une autre avant le premier IRET vers user mode.
    // new_task->cpu_state.cs = USER_CODE_SELECTOR;
    // new_task->cpu_state.ss = USER_DATA_SELECTOR;
    // new_task->cpu_state.ds = USER_DATA_SELECTOR;
    // new_task->cpu_state.es = USER_DATA_SELECTOR;
    // new_task->cpu_state.fs = USER_DATA_SELECTOR; // Ou un sélecteur TLS si utilisé
    // new_task->cpu_state.gs = USER_DATA_SELECTOR; // Ou un sélecteur TLS si utilisé

    // Ajouter à la file des tâches
    if (task_queue_head == NULL) { // Devrait être la tâche noyau initiale
        task_queue_head = new_task;
        new_task->next = new_task;
    } else {
        new_task->next = task_queue_head->next;
        task_queue_head->next = (struct task*)new_task;
    }

    asm volatile("sti");
    return new_task->id;
}


void schedule() {
    if (!current_task) {
        return; // Pas de tâche à scheduler
    }

    task_t* prev_task = (task_t*)current_task;
    task_t* next_candidate = (task_t*)current_task->next;

    // Boucle pour trouver la prochaine tâche exécutable (ni terminée, ni en attente)
    while ((next_candidate->state == TASK_TERMINATED ||
            next_candidate->state == TASK_WAITING_FOR_KEYBOARD ||
            next_candidate->state == TASK_WAITING_FOR_CHILD) &&
           next_candidate != current_task) {
        // Si une tâche est terminée, nous pourrions vouloir la supprimer de la liste
        // et libérer ses ressources. Pour l'instant, nous la sautons simplement.
        // Si toutes les tâches sont terminées (sauf current_task si elle l'est aussi),
        // current_task->next finirait par pointer vers current_task elle-même.

        // Optionnel : suppression de la tâche terminée de la liste chaînée
        // Cela nécessite de trouver la tâche qui pointe vers `next_candidate`
        // pour la relier à `next_candidate->next`.
        // Pour simplifier cette étape, nous ne libérons pas la mémoire ni ne la retirons explicitement.
        // Nous nous contentons de ne pas la sélectionner.
        // Attention: si toutes les tâches sauf une sont terminées, et que cette dernière se termine,
        // cette boucle pourrait devenir infinie si current_task est aussi TASK_TERMINATED.
        // Il faut une condition de sortie si on ne trouve que des tâches terminées.

        // Si nous avons fait un tour complet et que toutes les tâches (y compris current_task)
        // sont terminées ou pourraient l'être, il faut un "idle loop" ou un arrêt système.
        // Pour l'instant, si on ne trouve que des tâches terminées, on reste sur current_task
        // (qui pourrait elle-même être terminée, menant à un hlt).
        if (next_candidate == task_queue_head && next_candidate->state == TASK_TERMINATED) {
             // Cas où la tête de file est terminée, et on est revenu à elle.
             // Cela signifie que toutes les tâches pourraient être terminées.
             // Pour l'instant, on ne fait rien de spécial, la boucle context_switch se fera.
             // Une meilleure gestion serait de passer à une tâche "idle" ou de gérer l'arrêt.
        }
        next_candidate = (task_t*)next_candidate->next;

        // Condition d'arrêt si on a fait un tour complet et que current_task est la seule exécutable (ou aucune)
        if (next_candidate == current_task) {
            // Si current_task n'est pas exécutable, et qu'on a fait le tour, alors il n'y a rien à faire.
            if (current_task->state == TASK_TERMINATED ||
                current_task->state == TASK_WAITING_FOR_KEYBOARD ||
                current_task->state == TASK_WAITING_FOR_CHILD) {
                // Toutes les tâches sont soit terminées, soit en attente.
                // print_string("All tasks are waiting or terminated. Halting.\n", 0x0F); // Debug
                asm volatile("cli; hlt"); // Arrêter le système ou passer à une tâche idle.
                return; // Ne devrait pas être atteint si hlt fonctionne.
            }
            // Si current_task est exécutable, et on est revenu à elle, on ne change pas.
            return;
        }
    }

    // Si on est sorti de la boucle while, soit next_candidate est exécutable,
    // soit next_candidate == current_task et current_task est exécutable (géré ci-dessus).

    // Si la current_task (prev_task) n'est pas terminée et pas en attente, la remettre à READY.
    // Si elle est terminée ou en attente, elle garde son état.
    if (prev_task->state == TASK_RUNNING) { // Seule une tâche RUNNING peut devenir READY par preemption.
        prev_task->state = TASK_READY;
    }

    // La tâche sélectionnée (next_candidate) doit être exécutable ici.
    // Si elle ne l'est pas, c'est une erreur de logique dans la boucle ci-dessus.
    // (e.g. si toutes les tâches sont en attente et current_task était la dernière RUNNING)
    if (next_candidate->state == TASK_TERMINATED ||
        next_candidate->state == TASK_WAITING_FOR_KEYBOARD ||
        next_candidate->state == TASK_WAITING_FOR_CHILD) {
        // Cela ne devrait pas arriver si la logique de la boucle est correcte et qu'il y a au moins une tâche idle.
        // print_string("Scheduler error: selected a non-runnable task. Halting.\n", 0x0C);
        asm volatile("cli; hlt");
        return;
    }

    next_candidate->state = TASK_RUNNING;
    current_task = next_candidate;

    // Si prev_task est current_task (parce qu'on n'a pas trouvé d'autre tâche),
    // et que current_task est TASK_TERMINATED, on ne devrait pas switcher vers elle-même
    // si elle est terminée.
    if (prev_task == current_task && current_task->state == TASK_TERMINATED) {
        // Cela ne devrait pas arriver si la logique ci-dessus est correcte.
        // print_string("Scheduler tried to switch to a terminated current_task. Halting.\n", 0x0C); // Debug
        asm volatile("cli; hlt");
        return;
    }

    context_switch(&prev_task->cpu_state, &current_task->cpu_state);
}
