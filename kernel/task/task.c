#include "task.h"
#include "kernel/mem/pmm.h"
#include <stddef.h>
#include <stdint.h>

extern void context_switch(cpu_state_t* old_state, cpu_state_t* new_state);

volatile task_t* current_task = NULL;
volatile task_t* task_queue_head = NULL;
volatile uint32_t next_task_id = 0;

#define TASK_STACK_SIZE PAGE_SIZE // PAGE_SIZE doit être défini dans kernel/mem/pmm.h

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
    current_task->cpu_state.eflags = 0;
    current_task->next = (struct task*)current_task;
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
    void* task_stack = pmm_alloc_page();
    if (!task_stack) {
        pmm_free_page(new_task);
        asm volatile("sti");
        return NULL;
    }
    new_task->id = next_task_id++;
    new_task->state = TASK_READY;
    new_task->cpu_state.eflags = 0x202;
    new_task->cpu_state.eip = (uint32_t)entry_point;

    uint32_t* stack_ptr = (uint32_t*)((char*)task_stack + TASK_STACK_SIZE);
    *(--stack_ptr) = (uint32_t)entry_point;
    new_task->cpu_state.esp = (uint32_t)stack_ptr;
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

void schedule() {
    if (!current_task) {
        return; // Pas de tâche à scheduler
    }

    task_t* prev_task = (task_t*)current_task;
    task_t* next_candidate = (task_t*)current_task->next;

    // Boucle pour trouver la prochaine tâche non terminée
    while (next_candidate->state == TASK_TERMINATED && next_candidate != current_task) {
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
        if (next_candidate == current_task && current_task->state == TASK_TERMINATED) {
            // Toutes les tâches sont terminées, y compris la tâche actuelle.
            // Le système devrait probablement s'arrêter ou entrer dans une boucle idle profonde.
            // Pour l'instant, on ne fait rien, context_switch sera appelé avec la même tâche (terminée).
            // print_string("All tasks terminated. Halting.\n", 0x0F); // Debug
            asm volatile("cli; hlt"); // Arrêter le système
            return;
        }
         if (next_candidate == current_task && current_task->state != TASK_TERMINATED) {
            // On a fait le tour et la tâche actuelle est la seule non terminée.
            // On ne change pas de tâche.
            return;
        }
    }

    // Si la tâche suivante candidate est la tâche actuelle et qu'elle n'est pas terminée,
    // il n'y a pas d'autre tâche prête à part current_task.
    if (next_candidate == current_task && current_task->state != TASK_TERMINATED) {
        return; // Pas besoin de changer de contexte si c'est la seule tâche active.
    }

    // Si la tâche suivante candidate est terminée, et c'est la seule autre tâche (current_task est différent),
    // cela signifie qu'il n'y a plus de tâche valide à exécuter si current_task est aussi terminée.
    // Ce cas est couvert par la boucle while et la vérification "toutes tâches terminées" à la fin.

    if (prev_task->state != TASK_TERMINATED) {
        prev_task->state = TASK_READY;
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
