#include "task.h"
#include "kernel/mem/pmm.h" // Pour pmm_alloc_page, PAGE_SIZE
#include "kernel/mem/vmm.h" // Pour vmm_map_user_page
// #include "fs/initrd.h"   // initrd n'est plus utilisé pour charger le shell initialement
#include "kernel/elf.h"     // Pour elf_load
#include "kernel/libc.h"    // Pour memcpy, memset, strcmp
#include <stddef.h> // Pour NULL
#include <stdint.h> // Pour les types entiers standard

// Sélecteurs de segment GDT pour l'espace utilisateur (avec RPL=3, Niveau de Privilège Requis = 3).
// Ces valeurs doivent correspondre à votre configuration de la Table Globale de Descripteurs (GDT).
const uint16_t USER_CODE_SELECTOR = 0x18 | 3; // 0x1B: Sélecteur de code utilisateur (index 3 de la GDT, RPL 3)
const uint16_t USER_DATA_SELECTOR = 0x20 | 3; // 0x23: Sélecteur de données utilisateur (index 4 de la GDT, RPL 3)


// Fonctions externes (généralement définies en assembleur)
extern void context_switch(cpu_state_t* old_state, cpu_state_t* new_state); // Change le contexte CPU
extern uint32_t read_eip(); // Fonction (potentiellement inutilisée ici) pour lire EIP, utile pour la première tâche noyau.

// Pointeur vers la tâche en cours d'exécution. `volatile` car modifiable par des interruptions.
volatile task_t* current_task = NULL;
// Tête de la file d'attente circulaire des tâches.
volatile task_t* task_queue_head = NULL;
// Prochain ID de tâche à assigner. Commence à 1 (0 pourrait être réservé pour le noyau ou une tâche spéciale).
volatile uint32_t next_task_id = 1;

// Taille de la pile pour les tâches noyau (en octets).
#define KERNEL_TASK_STACK_SIZE PAGE_SIZE

// Constantes pour la pile utilisateur (peuvent aussi être dans un en-tête dédié à l'organisation mémoire).
#define USER_STACK_VIRTUAL_TOP         0xC0000000 // Adresse virtuelle du sommet de la pile utilisateur (ex: marque des 3Go).
#define USER_STACK_NUM_PAGES           4          // Nombre de pages pour la pile utilisateur (ex: 4 pages = 16Ko).
#define USER_STACK_SIZE_BYTES          (USER_STACK_NUM_PAGES * PAGE_SIZE) // Taille totale en octets.
#define USER_STACK_VIRTUAL_BOTTOM      (USER_STACK_VIRTUAL_TOP - USER_STACK_SIZE_BYTES) // Adresse virtuelle du bas de la pile.


// Initialise le système de gestion des tâches. Crée la première tâche (tâche "idle" du noyau).
void tasking_init() {
    asm volatile("cli"); // Désactiver les interruptions pendant l'initialisation.
    // Allouer de la mémoire pour la structure TCB (Task Control Block) de la première tâche.
    current_task = (task_t*)pmm_alloc_page();
    if (!current_task) {
        // Échec critique, impossible d'allouer la TCB pour la tâche initiale.
        // Un vrai noyau paniquerait ici.
        return;
    }
    current_task->id = next_task_id++; // ID 1.
    current_task->state = TASK_RUNNING; // La première tâche est immédiatement en cours d'exécution.
    current_task->cpu_state.eip = 0; // EIP/ESP/EBP ne sont pas critiques pour la tâche idle qui boucle.
    current_task->cpu_state.esp = 0;
    current_task->cpu_state.ebp = 0;
    // EFLAGS pour la tâche initiale du noyau (tâche idle).
    // IF=1 (0x200, interruptions activées) + bit 1 toujours à 1 (0x2) = 0x202.
    current_task->cpu_state.eflags = 0x00000202;
    current_task->next = (struct task*)current_task; // File circulaire : pointe sur elle-même.
    current_task->parent = NULL; // La tâche initiale n'a pas de parent.
    current_task->child_pid_waiting_on = 0; // N'attend aucun enfant.
    task_queue_head = current_task; // La tête de la file est cette tâche.
    asm volatile("sti"); // Réactiver les interruptions.
}

// Crée une tâche noyau simple (s'exécutant en Ring 0).
// entry_point: Pointeur vers la fonction que la tâche exécutera.
task_t* create_task(void (*entry_point)()) {
    asm volatile("cli"); // Désactiver les interruptions.
    task_t* new_task = (task_t*)pmm_alloc_page(); // Allouer de la mémoire pour la TCB.
    if (!new_task) {
        asm volatile("sti");
        return NULL; // Échec de l'allocation de la TCB.
    }
    memset(new_task, 0, sizeof(task_t)); // Initialiser la TCB à zéro.

    void* task_stack = pmm_alloc_page(); // Allouer une page pour la pile noyau de cette tâche.
    if (!task_stack) {
        pmm_free_page(new_task); // Libérer la TCB si la pile ne peut être allouée.
        asm volatile("sti");
        return NULL;
    }
    new_task->id = next_task_id++;
    new_task->state = TASK_READY; // La nouvelle tâche est prête à être exécutée.
    new_task->parent = NULL; // Les tâches noyau directes n'ont pas de parent de cette manière.
    new_task->child_pid_waiting_on = 0;

    // Configuration de l'état CPU pour une tâche Ring 0.
    // Pour une tâche noyau, CS/SS sont les sélecteurs noyau (implicites ou définis dans la GDT).
    // EFLAGS: IF=1 (0x200, interruptions activées) + bit 1 (0x2) = 0x202.
    new_task->cpu_state.eflags = 0x202;
    new_task->cpu_state.eip = (uint32_t)(uintptr_t)entry_point; // Point d'entrée de la tâche.

    // Initialisation de la pile noyau. ESP pointe au sommet de la pile (adresse la plus haute).
    // La pile grandit vers le bas.
    uint32_t stack_top = (uint32_t)(uintptr_t)task_stack + KERNEL_TASK_STACK_SIZE;
    new_task->cpu_state.esp = stack_top;
    // Pour une tâche noyau démarrant à `entry_point`, nous n'avons pas besoin de pousser grand-chose sur la pile
    // car elle ne résulte pas d'un `iret`. Cependant, si `entry_point` est une fonction C standard,
    // elle s'attendra à un EBP valide (généralement 0 pour la première frame ou une valeur de la pile).
    new_task->cpu_state.ebp = 0; // Ou `stack_top`, selon les conventions. 0 est courant.

    // Initialiser les autres registres généraux à 0.
    new_task->cpu_state.eax = 0;
    new_task->cpu_state.ebx = 0;
    new_task->cpu_state.ecx = 0;
    new_task->cpu_state.edx = 0;
    new_task->cpu_state.esi = 0;
    new_task->cpu_state.edi = 0;

    // Ajouter la nouvelle tâche à la file d'attente circulaire.
    if (task_queue_head == NULL) { // Si c'est la première tâche après tasking_init (ne devrait pas arriver ici).
        task_queue_head = new_task;
        new_task->next = new_task;
    } else {
        new_task->next = task_queue_head->next; // Insérer après la tête actuelle.
        task_queue_head->next = (struct task*)new_task;
    }
    asm volatile("sti"); // Réactiver les interruptions.
    return new_task;
}


// Crée un processus utilisateur à partir d'un fichier ELF.
// path_in_initrd: Nom du fichier exécutable (actuellement, les binaires sont embarqués).
// argv_from_caller: Tableau de chaînes d'arguments (style main(argc, argv)). Le dernier élément doit être NULL.
// Retourne: Le PID du nouveau processus, ou -1 en cas d'erreur.
int create_user_process(const char* path_in_initrd, char* const argv_from_caller[]) {
    // Les messages de débogage ont été retirés.

    // Déclarations pour les symboles du linker pour les binaires embarqués (générés par objcopy).
    // Ces symboles indiquent le début et la fin des données binaires des exécutables.
    extern uint8_t _binary_shell_bin_start[];
    extern uint8_t _binary_shell_bin_end[];
    extern uint8_t _binary_fake_ai_bin_start[];
    extern uint8_t _binary_fake_ai_bin_end[];

    asm volatile("cli"); // Désactiver les interruptions pendant la création du processus.

    uint8_t* elf_data = NULL;    // Pointeur vers les données ELF.
    uint32_t elf_file_size = 0;  // Taille des données ELF.

    // Comparer `path_in_initrd` pour déterminer quel binaire charger.
    if (strcmp(path_in_initrd, "shell.bin") == 0) {
        elf_data = _binary_shell_bin_start;
        elf_file_size = (uint32_t)(_binary_shell_bin_end - _binary_shell_bin_start);
    } else if (strcmp(path_in_initrd, "fake_ai.bin") == 0) {
        elf_data = _binary_fake_ai_bin_start;
        elf_file_size = (uint32_t)(_binary_fake_ai_bin_end - _binary_fake_ai_bin_start);
    } else {
        // Binaire non reconnu.
        asm volatile("sti");
        return -1;
    }

    if (!elf_data || elf_file_size == 0) {
        // Erreur : Fichier non trouvé ou vide.
        asm volatile("sti");
        return -1;
    }

    task_t* new_task = (task_t*)pmm_alloc_page(); // Allocation de mémoire pour la TCB.
    if (!new_task) {
        // Échec de l'allocation de la TCB.
        asm volatile("sti");
        return -1;
    }
    memset(new_task, 0, sizeof(task_t)); // Initialiser la TCB.

    // Charger l'exécutable ELF en mémoire. `elf_load` gère l'allocation et le mappage des segments.
    uint32_t entry_point = elf_load(elf_data);
    if (entry_point == 0) {
        // Échec du chargement de l'ELF.
        pmm_free_page(new_task); // Libérer la TCB allouée.
        asm volatile("sti");
        return -1;
    }

    // Allouer et mapper la pile utilisateur dans la plage virtuelle définie
    // (de USER_STACK_VIRTUAL_BOTTOM à USER_STACK_VIRTUAL_TOP).
    for (int i = 0; i < USER_STACK_NUM_PAGES; ++i) {
        void* phys_page_for_stack = pmm_alloc_page(); // Allouer une page physique pour la pile.
        if (!phys_page_for_stack) {
            // Échec de l'allocation d'une page de pile.
            // TODO: Nettoyer proprement les pages ELF et la TCB, ainsi que les pages de pile déjà allouées.
            // Pour l'instant, libération simple de la TCB.
            pmm_free_page(new_task);
            asm volatile("sti");
            return -1; // Erreur.
        }
        // Calculer l'adresse virtuelle pour la page de pile actuelle (en partant du bas).
        void* stack_page_vaddr = (void*)(USER_STACK_VIRTUAL_BOTTOM + i * PAGE_SIZE);
        vmm_map_user_page(stack_page_vaddr, phys_page_for_stack); // Mapper la page physique à l'adresse virtuelle.
    }

    // ESP pour le mode utilisateur doit pointer vers le sommet de la pile allouée (adresse la plus haute).
    uint32_t esp_user_initial = USER_STACK_VIRTUAL_TOP;

    // Préparer argc et argv sur la pile utilisateur.
    // 1. Compter argc et calculer la taille totale nécessaire pour les chaînes argv.
    int argc = 0;
    size_t total_argv_strlen = 0;
    if (argv_from_caller) {
        for (argc = 0; argv_from_caller[argc] != NULL; argc++) {
            const char* arg = argv_from_caller[argc];
            size_t len = 0;
            while(arg[len]) len++; // strlen basique.
            total_argv_strlen += (len + 1); // +1 pour le caractère nul de fin.
        }
    }

    // Calculer l'espace nécessaire sur la pile :
    // total_argv_strlen (pour les chaînes) + (argc + 1) * sizeof(char*) (pour le tableau de pointeurs argv) + sizeof(int) (pour argc).
    // Nous allons placer les chaînes tout en haut de la pile (aux adresses les plus basses de cette zone),
    // puis les pointeurs vers ces chaînes, puis argc. ESP pointera finalement vers argc.
    uint32_t current_esp_user = esp_user_initial;

    // Copier les chaînes d'arguments sur la pile utilisateur.
    // Les chaînes sont copiées en premier, aux adresses les plus hautes de la zone de pile utilisée par argv et les chaînes.
    // `current_esp_user` descendra (les adresses diminuent) au fur et à mesure.
    current_esp_user -= total_argv_strlen;
    char* string_area_on_stack_start = (char*)(uintptr_t)current_esp_user; // Début de la zone où les chaînes sont stockées.

    char* argv_on_stack_pointers[argc + 1]; // Tableau temporaire pour stocker les adresses virtuelles des chaînes sur la pile.

    char* current_string_write_ptr = string_area_on_stack_start;
    for (int i = 0; i < argc; i++) {
        const char* arg_source = argv_from_caller[i];
        size_t len = 0;
        while(arg_source[len]) len++;
        memcpy(current_string_write_ptr, arg_source, len + 1);
        argv_on_stack_pointers[i] = current_string_write_ptr; // C'est l'adresse virtuelle sur la pile utilisateur.
        current_string_write_ptr += (len + 1);
    }
    argv_on_stack_pointers[argc] = NULL; // Le dernier élément de argv (le tableau de pointeurs) est NULL.

    // Pousser les pointeurs argv (qui pointent vers les chaînes déjà sur la pile).
    // Ces pointeurs sont poussés après les chaînes elles-mêmes.
    current_esp_user -= (argc + 1) * sizeof(char*);
    memcpy((void*)(uintptr_t)current_esp_user, argv_on_stack_pointers, (argc + 1) * sizeof(char*));
    new_task->argv_user_stack_ptr = (char**)(uintptr_t)current_esp_user; // Sauvegarder où le tableau argv[] est sur la pile.

    // Pousser argc.
    current_esp_user -= sizeof(int);
    *(int*)(uintptr_t)current_esp_user = argc;
    new_task->argc = argc;

    // Allouer une pile noyau pour la tâche utilisateur.
    void* kernel_stack_ptr = pmm_alloc_page();
    if (!kernel_stack_ptr) {
        // Échec de l'allocation de la pile noyau.
        // TODO: Nettoyage plus complet (pages ELF, pile utilisateur, TCB).
        pmm_free_page(new_task); // Libérer la TCB.
        asm volatile("sti");
        return -1;
    }
    uint32_t kernel_stack_top = (uint32_t)(uintptr_t)kernel_stack_ptr + KERNEL_TASK_STACK_SIZE;

    // Initialiser l'état du CPU pour la nouvelle tâche.
    new_task->id = next_task_id++; // Assignation du PID.
    new_task->state = TASK_READY; // Prête à être exécutée.
    new_task->parent = (struct task*)current_task; // La tâche appelante est le parent.
    new_task->child_pid_waiting_on = 0; // L'enfant n'attend personne au début.

    memset(&new_task->cpu_state, 0, sizeof(cpu_state_t)); // Mettre tous les registres de la TCB à 0.

    // Préparer la pile noyau pour le premier `iret` vers le mode utilisateur.
    // Cette pile sera pointée par `new_task->cpu_state.esp`.
    // Ordre pour `iret`: EIP, CS, EFLAGS, ESP_user, SS_user (poussés par le CPU ou manuellement).
    // Ordre pour `popad` (si utilisé par `context_switch` pour restaurer les registres généraux) :
    //   La pile doit contenir EDI, ESI, EBP, ESP_dummy, EBX, EDX, ECX, EAX (du plus bas au plus haut sur la pile avant popad).
    //   `context_switch` fera `popad` pour restaurer EAX..EDI.
    // L'ESP final (après `popad` et avant `iret`) doit pointer vers EIP sur la pile.
    // La pile grandit vers le bas (les adresses diminuent lors d'un PUSH).

    uint32_t* kstack = (uint32_t*)kernel_stack_top; // Pointeur vers le sommet de la pile noyau.

    // Trame IRET pour passer en mode utilisateur.
    *(--kstack) = USER_DATA_SELECTOR;      // SS_user (Sélecteur de segment de données utilisateur).
    *(--kstack) = current_esp_user;        // ESP_user (pointeur de pile utilisateur, pointe vers argc).
    *(--kstack) = 0x00000202;              // EFLAGS_user (IF=1 pour interruptions, bit 1=1, IOPL=0).
    *(--kstack) = USER_CODE_SELECTOR;      // CS_user (Sélecteur de segment de code utilisateur).
    *(--kstack) = entry_point;             // EIP_user (point d'entrée du programme ELF).

    // Trame pour `popad` (registres généraux, initialisés à 0 pour une nouvelle tâche).
    // PUSHAD pousse dans l'ordre : EDI, ESI, EBP, ESP_kernel_dummy, EBX, EDX, ECX, EAX.
    // POPAD les restaure dans l'ordre inverse : EAX, ECX, EDX, EBX, ESP_dummy, EBP, ESI, EDI.
    // Donc, sur la pile, avant EIP pour `iret` (qui est déjà poussé),
    // nous devons avoir (du plus haut au plus bas sur la pile, donc poussés en dernier) :
    // EAX, ECX, EDX, EBX, ESP_dummy, EBP, ESI, EDI.
    // L'ESP du noyau pointera vers EAX (ou EDI si on suit l'ordre PUSHAD).
    // Le stub `context_switch` fera `popad`, donc l'ordre sur la pile doit être correct pour `popad`.
    *(--kstack) = 0; // EDI (pour `popad`).
    *(--kstack) = 0; // ESI.
    *(--kstack) = 0; // EBP.
    *(--kstack) = 0; // ESP_dummy (valeur non utilisée par `popad` mais une place est réservée sur la pile).
    *(--kstack) = 0; // EBX.
    *(--kstack) = 0; // EDX.
    *(--kstack) = 0; // ECX.
    *(--kstack) = 0; // EAX.

    // L'ESP du noyau pour la nouvelle tâche pointera ici (vers le sommet de la trame `popad`, c'est-à-dire EAX).
    // `context_switch` fera :
    //   mov esp, new_task->cpu_state.esp
    //   popad  (restaure EAX..EDI depuis la pile noyau)
    //   iret   (restaure EIP_user, CS_user, EFLAGS_user, ESP_user, SS_user depuis la pile noyau)
    new_task->cpu_state.esp = (uint32_t)(uintptr_t)kstack;

    // Les champs `eip` et `eflags` dans `cpu_state_t` ne sont pas directement utilisés
    // par ce mécanisme de premier lancement car `iret` prend toutes ses informations de la pile.
    // Nous les mettons à jour pour la cohérence ou le débogage futur.
    new_task->cpu_state.eip = entry_point;
    new_task->cpu_state.eflags = 0x00000202;


    // Ajouter la nouvelle tâche à la file d'attente circulaire des tâches.
    if (task_queue_head == NULL) { // Ne devrait être vrai que si `tasking_init` n'a pas été appelée ou a échoué.
        task_queue_head = new_task;
        new_task->next = new_task;
    } else {
        new_task->next = task_queue_head->next; // Insérer après la tête actuelle.
        task_queue_head->next = (struct task*)new_task; // La tête pointe vers la nouvelle tâche.
    }

    asm volatile("sti"); // Réactiver les interruptions.
    return new_task->id; // Retourner le PID de la nouvelle tâche.
}


// Ordonnanceur de tâches (scheduler).
void schedule() {
    // Les messages de débogage VGA ont été retirés.

    if (!current_task) {
        // Aucune tâche courante, situation anormale après l'initialisation.
        return;
    }

    task_t* prev_task = (task_t*)current_task;
    task_t* next_candidate = (task_t*)current_task->next;

    // Boucle pour trouver la prochaine tâche exécutable (ni terminée, ni en attente).
    while ((next_candidate->state == TASK_TERMINATED ||
            next_candidate->state == TASK_WAITING_FOR_KEYBOARD ||
            next_candidate->state == TASK_WAITING_FOR_CHILD) &&
           next_candidate != current_task) { // Éviter une boucle infinie si toutes les tâches sont non exécutables.
        // Si une tâche est terminée, nous pourrions vouloir la supprimer de la liste
        // et libérer ses ressources (TCB, pile, etc.). Pour l'instant, nous la sautons simplement.
        // Une gestion plus avancée impliquerait de trouver la tâche précédente dans la liste
        // pour la relier à `next_candidate->next` et de libérer la mémoire.

        // Si nous avons fait un tour complet et que toutes les tâches (y compris `current_task` si elle est non exécutable)
        // sont non exécutables, il faut un comportement défini (ex: boucle "idle", arrêt système).
        next_candidate = (task_t*)next_candidate->next;

        // Condition d'arrêt si on a fait un tour complet et que `current_task` est la seule option (ou aucune).
        if (next_candidate == current_task) {
            // Si `current_task` elle-même n'est pas exécutable, et que nous avons fait le tour,
            // cela signifie qu'aucune tâche n'est prête.
            if (current_task->state == TASK_TERMINATED ||
                current_task->state == TASK_WAITING_FOR_KEYBOARD ||
                current_task->state == TASK_WAITING_FOR_CHILD) {
                // Toutes les tâches sont soit terminées, soit en attente.
                // Le système devrait passer à une tâche "idle" dédiée ou s'arrêter proprement.
                // Pour l'instant, arrêt simple.
                asm volatile("cli; hlt"); // Désactiver les interruptions et arrêter le CPU.
                return; // Ne devrait pas être atteint si `hlt` fonctionne.
            }
            // Si `current_task` est exécutable (RUNNING ou READY), et que nous sommes revenus à elle,
            // cela signifie qu'il n'y a pas d'autre tâche prête à s'exécuter. On ne change pas de tâche.
            return;
        }
    }

    // Si on est sorti de la boucle `while`, soit `next_candidate` est exécutable,
    // soit `next_candidate == current_task` et `current_task` est exécutable (cas géré ci-dessus).

    // Si la `current_task` (maintenant `prev_task`) n'est pas terminée et pas en attente (donc était RUNNING),
    // la remettre à l'état READY car elle est préemptée.
    // Si elle est terminée ou en attente, elle conserve son état.
    if (prev_task->state == TASK_RUNNING) { // Seule une tâche RUNNING peut devenir READY par préemption.
        prev_task->state = TASK_READY;
    }

    // La tâche sélectionnée (`next_candidate`) doit être exécutable ici.
    // Si elle ne l'est pas, c'est une erreur de logique dans la boucle ci-dessus.
    if (next_candidate->state == TASK_TERMINATED ||
        next_candidate->state == TASK_WAITING_FOR_KEYBOARD ||
        next_candidate->state == TASK_WAITING_FOR_CHILD) {
        // Cela ne devrait pas arriver si la logique de la boucle est correcte et qu'il y a au moins une tâche "idle"
        // ou si la condition d'arrêt via `hlt` a été atteinte.
        asm volatile("cli; hlt"); // Erreur critique, arrêt.
        return;
    }

    next_candidate->state = TASK_RUNNING; // La nouvelle tâche devient RUNNING.
    current_task = next_candidate;        // Mettre à jour la tâche courante.

    // Si `prev_task` est la même que `current_task` (parce qu'on n'a pas trouvé d'autre tâche exécutable),
    // et que `current_task` est terminée (ce qui ne devrait pas arriver ici à cause des vérifications précédentes),
    // on ne devrait pas commuter vers elle-même si elle est terminée.
    if (prev_task == current_task && current_task->state == TASK_TERMINATED) {
        // Situation anormale, devrait être gérée par les logiques précédentes.
        asm volatile("cli; hlt"); // Arrêt pour débogage.
        return;
    }

    // Effectuer le changement de contexte.
    context_switch(&prev_task->cpu_state, (cpu_state_t*)&current_task->cpu_state);
    // Normalement, `context_switch` ne retourne pas ici pour `prev_task` (l'ancienne tâche).
    // Il retourne pour la tâche qui est commutée "entrante" (`current_task` ici).
}
