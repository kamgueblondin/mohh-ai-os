#include "task.h"
#include "kernel/mem/pmm.h" // Pour pmm_alloc_page, PAGE_SIZE
#include "kernel/mem/vmm.h" // Pour vmm_map_user_page
// #include "fs/initrd.h"   // initrd n'est plus utilisé pour charger le shell initial
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
    // eflags pour la tâche initiale du noyau (idle task).
    // Mettre IF=1 (0x200) + bit 1 (0x2) = 0x202
    current_task->cpu_state.eflags = 0x00000202;
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
    // DEBUG: Utiliser une couleur distinctive pour les messages de create_user_process
    char debug_color = 0x0E; // Jaune sur noir pour le débogage

    // Déclarations pour les symboles du linker pour les binaires embarqués (générés par objcopy)
    extern uint8_t _binary_shell_bin_start[];
    extern uint8_t _binary_shell_bin_end[];

    extern uint8_t _binary_fake_ai_bin_start[];
    extern uint8_t _binary_fake_ai_bin_end[];

    print_string("DEBUG_CUP: Entered create_user_process for: ", debug_color); print_string(path_in_initrd, debug_color); print_string("\n", debug_color);

    asm volatile("cli"); // Désactiver les interruptions pendant la création

    uint8_t* elf_data = NULL;
    uint32_t elf_file_size = 0;

    // Comparer path_in_initrd pour déterminer quel binaire charger
    if (strcmp(path_in_initrd, "shell.bin") == 0) {
        elf_data = _binary_shell_bin_start;
        elf_file_size = (uint32_t)(_binary_shell_bin_end - _binary_shell_bin_start);
        print_string("DEBUG_CUP: Loading embedded shell.bin. Addr: ", debug_color); print_hex((uint32_t)elf_data, debug_color); print_string(", Size (objcopy): ", debug_color); print_hex(elf_file_size, debug_color); print_string("\n", debug_color);
    } else if (strcmp(path_in_initrd, "fake_ai.bin") == 0) {
        elf_data = _binary_fake_ai_bin_start;
        elf_file_size = (uint32_t)(_binary_fake_ai_bin_end - _binary_fake_ai_bin_start);
        print_string("DEBUG_CUP: Loading embedded fake_ai.bin. Addr: ", debug_color); print_hex((uint32_t)elf_data, debug_color); print_string(", Size (objcopy): ", debug_color); print_hex(elf_file_size, debug_color); print_string("\n", debug_color);
    } else {
        print_string("DEBUG_CUP: Unknown application requested: ", 0x0C); print_string(path_in_initrd, 0x0C); print_string("\n", 0x0C);
        asm volatile("sti");
        return -1; // Binaire non reconnu
    }

    if (!elf_data || elf_file_size == 0) {
        print_string("DEBUG_CUP: ELF data is NULL or size is zero for: ", 0x0C); print_string(path_in_initrd, 0x0C); print_string("\n", 0x0C);
        asm volatile("sti");
        return -1; // Erreur: Fichier non trouvé ou vide
    }

    print_string("DEBUG_CUP: Calling pmm_alloc_page for TCB\n", debug_color);
    task_t* new_task = (task_t*)pmm_alloc_page(); // Allocation pour la TCB
    print_string("DEBUG_CUP: pmm_alloc_page for TCB returned: ", debug_color); print_hex((uint32_t)new_task, debug_color); print_string("\n", debug_color);

    if (!new_task) {
        print_string("DEBUG_CUP: Failed to allocate TCB for new process\n", 0x0C);
        asm volatile("sti");
        return -1;
    }
    memset(new_task, 0, sizeof(task_t));

    print_string("DEBUG_CUP: Calling elf_load with elf_data: ", debug_color); print_hex((uint32_t)elf_data, debug_color); print_string("\n", debug_color);
    uint32_t entry_point = elf_load(elf_data); 
    print_string("DEBUG_CUP: elf_load returned entry_point: ", debug_color); print_hex(entry_point, debug_color); print_string("\n", debug_color);

    if (entry_point == 0) {
        print_string("DEBUG_CUP: Failed to load ELF. Path: ", 0x0C); print_string(path_in_initrd, 0x0C); print_string("\n", 0x0C);
        pmm_free_page(new_task); // Libérer la TCB allouée
        // Note: elf_data n'a pas besoin d'être libéré ici car il pointe vers une partie de l'initrd,
        // ou il est géré par initrd_read_file (qui ne semble pas allouer dynamiquement pour le retour).
        asm volatile("sti");
        return -1;
    }

    print_string("DEBUG_CUP: Allocating user stack pages...\n", debug_color);
    // Allouer et mapper la pile utilisateur dans la plage virtuelle définie
    // USER_STACK_VIRTUAL_BOTTOM à USER_STACK_VIRTUAL_TOP
    for (int i = 0; i < USER_STACK_NUM_PAGES; ++i) {
        print_string("DEBUG_CUP: User stack, page ", debug_color); print_hex(i, debug_color); print_string(": calling pmm_alloc_page\n", debug_color);
        void* phys_page_for_stack = pmm_alloc_page();
        print_string("DEBUG_CUP: User stack, page ", debug_color); print_hex(i, debug_color); print_string(": pmm_alloc_page returned: ", debug_color); print_hex((uint32_t)phys_page_for_stack, debug_color); print_string("\n", debug_color);

        if (!phys_page_for_stack) {
            print_string("DEBUG_CUP: Failed to allocate user stack page ", 0x0C); print_hex(i, 0x0C); print_string("\n", 0x0C);
            // TODO: Libérer les pages ELF et la TCB. Cela nécessite de savoir quelles pages ELF a allouées.
            // Et aussi les pages de pile utilisateur déjà allouées.
            // Pour l'instant, libération simple de la TCB.
            pmm_free_page(new_task);
            asm volatile("sti");
            return -1; // Erreur
        }
        // Calcule l'adresse virtuelle pour la page de pile actuelle (en partant du bas)
        void* stack_page_vaddr = (void*)(USER_STACK_VIRTUAL_BOTTOM + i * PAGE_SIZE);
        print_string("DEBUG_CUP: Mapping user stack page: vaddr=", debug_color); print_hex((uint32_t)stack_page_vaddr, debug_color); print_string(" to paddr=", debug_color); print_hex((uint32_t)phys_page_for_stack, debug_color); print_string("\n", debug_color);
        vmm_map_user_page(stack_page_vaddr, phys_page_for_stack);
    }
    print_string("DEBUG_CUP: User stack allocation complete.\n", debug_color);

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
    // new_task->id est assigné plus tard, après l'allocation de la pile noyau
    new_task->state = TASK_READY;
    new_task->parent = (struct task*)current_task; // La tâche appelante est le parent
    new_task->child_pid_waiting_on = 0; // L'enfant n'attend personne au début

    print_string("DEBUG_CUP: Calling pmm_alloc_page for kernel stack\n", debug_color);
    // Allouer une pile noyau pour la tâche utilisateur
    void* kernel_stack_ptr = pmm_alloc_page();
    print_string("DEBUG_CUP: pmm_alloc_page for kernel stack returned: ", debug_color); print_hex((uint32_t)kernel_stack_ptr, debug_color); print_string("\n", debug_color);

    if (!kernel_stack_ptr) {
        print_string("DEBUG_CUP: Failed to allocate kernel stack for new process\n", 0x0C);
        // TODO: Cleanup ELF pages, user stack pages, and TCB
        pmm_free_page(new_task); // Free TCB
        asm volatile("sti");
        return -1;
    }
    uint32_t kernel_stack_top = (uint32_t)kernel_stack_ptr + KERNEL_TASK_STACK_SIZE;

    // Initialiser l'état du CPU pour la nouvelle tâche
    new_task->id = next_task_id++; // Assignation du PID ici
    new_task->state = TASK_READY;
    new_task->parent = (struct task*)current_task; // La tâche appelante est le parent
    new_task->child_pid_waiting_on = 0; // L'enfant n'attend personne au début

    memset(&new_task->cpu_state, 0, sizeof(cpu_state_t)); // Met tous les regs de la TCB à 0

    // Préparer la pile noyau pour le premier iret vers le mode utilisateur
    // Cette pile sera pointée par new_task->cpu_state.esp
    // Ordre pour iret: EIP, CS, EFLAGS, ESP_user, SS_user
    // Ordre pour popad: EDI, ESI, EBP, ESP_dummy, EBX, EDX, ECX, EAX
    // ESP final doit pointer vers EAX (ou EDI si on considère l'ordre de popad)
    // La pile grandit vers le bas (les adresses diminuent)

    uint32_t* kstack = (uint32_t*)kernel_stack_top;

    // Trame IRET pour le mode utilisateur
    *(--kstack) = USER_DATA_SELECTOR;      // SS_user
    *(--kstack) = current_esp_user;        // ESP_user (pointe vers argc sur la pile utilisateur)
    *(--kstack) = 0x00000202;              // EFLAGS_user (IF=1, bit 1=1, IOPL=0)
    *(--kstack) = USER_CODE_SELECTOR;      // CS_user
    *(--kstack) = entry_point;             // EIP_user (point d'entrée ELF)

    // Trame pour popad (registres généraux, initialisés à 0 pour une nouvelle tâche)
    // PUSHAD pousse EDI, ESI, EBP, ESP_kernel_dummy, EBX, EDX, ECX, EAX.
    // POPAD les restaure dans l'ordre inverse: EAX, ECX, EDX, EBX, ESP_dummy, EBP, ESI, EDI.
    // Donc, sur la pile, avant EFLAGS pour popfd, nous devons avoir (du plus haut au plus bas sur la pile):
    // EAX, ECX, EDX, EBX, ESP_dummy, EBP, ESI, EDI
    *(--kstack) = 0; // EDI (pour popad)
    *(--kstack) = 0; // ESI
    *(--kstack) = 0; // EBP
    *(--kstack) = 0; // ESP_dummy (valeur non utilisée par popad mais une place est réservée)
    *(--kstack) = 0; // EBX
    *(--kstack) = 0; // EDX
    *(--kstack) = 0; // ECX
    *(--kstack) = 0; // EAX

    // L'ESP du noyau pour la nouvelle tâche pointera ici (vers EAX sur la pile noyau)
    // context_switch fera:
    // mov esp, new_task->cpu_state.esp
    // popad  (restaure EAX..EDI)
    // iret   (restaure EIP_user, CS_user, EFLAGS_user, ESP_user, SS_user)
    new_task->cpu_state.esp = (uint32_t)kstack;

    // Les champs eip et eflags dans cpu_state_t ne sont pas directement utilisés
    // par ce mécanisme de premier lancement car iret prend tout sur la pile.
    // On les met à jour pour la cohérence ou le débogage.
    new_task->cpu_state.eip = entry_point;
    new_task->cpu_state.eflags = 0x00000202;


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
    volatile unsigned short* video_debug_schedule = (unsigned short*)0xB8000;
    video_debug_schedule[10*80 + 0] = (video_debug_schedule[10*80 + 0] & 0x00FF) | (0x1F00); // Ligne 10, Char 0, Fond Bleu

    if (!current_task) {
        video_debug_schedule[10*80 + 1] = 'N'; // No current task
        return;
    }
    video_debug_schedule[10*80 + 1] = 'E'; // Entered schedule

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
    video_debug_schedule[10*80 + 2] = 'C'; // Calling Context Switch
    context_switch(&prev_task->cpu_state, (cpu_state_t*)&current_task->cpu_state);
    // Normalement, context_switch ne retourne pas ici pour prev_task.
    // Il retourne pour la tâche qui est switchée "in".
    video_debug_schedule[10*80 + 3] = 'R'; // Returned from context_switch (ne devrait pas être pour la même invocation)
}
