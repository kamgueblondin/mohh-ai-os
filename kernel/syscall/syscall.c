#include "syscall.h"
#include "kernel/idt.h"        // Pour idt_set_gate et la définition potentielle de cpu_state_t (via interrupts.h implicitement)
#include "kernel/task/task.h"  // Pour current_task, états de tâche, schedule(), et cpu_state_t (si utilisé directement)
#include "kernel/keyboard.h"   // Pour keyboard_prepare_for_gets, keyboard_get_chars_read_count
#include <stdint.h>     // Pour uint32_t, int etc.
#include <stddef.h>     // Pour NULL

// Déclarations externes pour les variables et fonctions VGA (probablement définies dans kernel.c ou un module vga.c dédié)
extern int vga_x; // Position x actuelle du curseur VGA
extern int vga_y; // Position y actuelle du curseur VGA
extern char current_color; // Couleur actuelle du texte VGA
extern void print_char(char c, int x, int y, char color); // Fonction d'affichage de caractère VGA
// extern void print_string(const char* str, char color); // Si nécessaire pour le débogage interne

// Tâche courante (définie dans task.c, pointe vers la structure de la tâche en cours d'exécution)
extern volatile task_t* current_task;

// Gestionnaire d'interruption assembleur pour int 0x80 (défini dans syscall_handler.s ou un fichier assembleur similaire)
// Ce stub sauvegarde l'état du CPU, appelle syscall_handler (cette fonction C), puis restaure l'état et retourne.
extern void syscall_interrupt_handler_asm();

// Le gestionnaire C des appels système, appelé par le stub assembleur de l'interruption 0x80.
// Le pointeur `stack_ptr_raw` pointe vers la pile du noyau où les registres ont été sauvegardés par le stub.
// L'ordre exact dépend de la convention du stub (par exemple, PUSHAD, puis segments DS, ES, FS, GS).
// La structure cpu_state_t de task.h n'est PAS directement et universellement mappée ici ;
// nous accédons aux registres par des offsets définis par la convention du stub.
// Supposons l'ordre PUSHAD de NASM : EDI, ESI, EBP, ESP_dummy, EBX, EDX, ECX, EAX.
// Si les segments sont poussés avant PUSHAD, les offsets changent.
// Le stub actuel (basé sur les commentaires précédents) semble pousser GS, FS, ES, DS PUIS PUSHAD.
// Donc, sur la pile du noyau, nous aurions (de l'adresse la plus haute à la plus basse avant l'appel C) :
// [EAX_user] [ECX_user] [EDX_user] [EBX_user] [ESP_dummy_user] [EBP_user] [ESI_user] [EDI_user] [DS_user] [ES_user] [FS_user] [GS_user] <--- stack_ptr_raw pointe ici (ou à EAX si PUSHAD est fait après les segments)
//
// D'après le commentaire du fichier source original, l'ordre sur la pile pointée par stack_ptr_raw est :
// [GS, FS, ES, DS, EDI, ESI, EBP, ESP_d, EBX, EDX, ECX, EAX]
// Index:   0   1   2   3   4    5    6     7      8    9    10   11 (où l'index 0 est la plus petite adresse)
// Donc stack_ptr_raw[0] = GS, stack_ptr_raw[11] = EAX.
#define STACK_IDX_GS  0
#define STACK_IDX_FS  1
#define STACK_IDX_ES  2
#define STACK_IDX_DS  3
#define STACK_IDX_EDI 4
#define STACK_IDX_ESI 5
#define STACK_IDX_EBP 6
#define STACK_IDX_ESP_KERNEL_DUMMY 7 // ESP sauvegardé par PUSHAD, non l'ESP utilisateur.
#define STACK_IDX_EBX 8
#define STACK_IDX_EDX 9
#define STACK_IDX_ECX 10
#define STACK_IDX_EAX 11 // Contient le numéro de l'appel système.

// Le type `void* stack_ptr_raw` est utilisé car la structure exacte de la pile dépend du stub assembleur.
void syscall_handler(void* stack_ptr_raw) {
    uint32_t* regs = (uint32_t*)stack_ptr_raw; // Caster le pointeur void en pointeur uint32_t pour accéder aux registres.

    // Vérifications de base pour éviter les déréférencements de pointeur nul.
    if (!regs || !current_task) {
        if (regs) regs[STACK_IDX_EAX] = (uint32_t)-1; // Tenter de retourner une erreur si possible.
        return; // Quitter si les pointeurs sont invalides.
    }

    // Le numéro de l'appel système est traditionnellement passé dans EAX.
    uint32_t syscall_number = regs[STACK_IDX_EAX];

    switch (syscall_number) {
        case 0: // SYS_EXIT - Terminer le processus courant.
            current_task->state = TASK_TERMINATED; // Marquer la tâche comme terminée.
            // Si un parent attend cette tâche spécifiquement (waitpid-like)
            if (current_task->parent &&
                current_task->parent->state == TASK_WAITING_FOR_CHILD &&
                current_task->parent->child_pid_waiting_on == current_task->id) {
                current_task->parent->state = TASK_READY; // Réveiller le parent.
                current_task->parent->child_exit_status = (int)regs[STACK_IDX_EBX]; // Le statut de sortie est dans EBX.
                current_task->parent->child_pid_waiting_on = 0; // Le parent n'attend plus ce PID.
            }
            schedule(); // Appeler le scheduler pour choisir une autre tâche. Cette fonction ne retourne pas ici.
            break;

        case 1: // SYS_PUTC - Afficher un caractère.
            // Le caractère est passé dans EBX.
            print_char((char)regs[STACK_IDX_EBX], vga_x, vga_y, current_color);
            regs[STACK_IDX_EAX] = 0; // Retourner 0 (succès).
            break;

        case 4: // SYS_GETS - Lire une chaîne depuis le clavier.
            {
                char* user_buf = (char*)regs[STACK_IDX_EBX]; // EBX contient le pointeur vers le tampon utilisateur.
                uint32_t user_buf_size = regs[STACK_IDX_ECX]; // ECX contient la taille du tampon.

                if (user_buf == NULL || user_buf_size == 0) {
                    regs[STACK_IDX_EAX] = (uint32_t)-1; // Erreur : tampon invalide.
                    break;
                }
                // Préparer le pilote clavier pour la lecture. `current_task` est assigné à `task_waiting_for_input` dans cette fonction.
                keyboard_prepare_for_gets(user_buf, user_buf_size);
                current_task->state = TASK_WAITING_FOR_KEYBOARD; // Mettre la tâche en attente d'entrée clavier.

                // La valeur de retour (nombre de caractères lus) sera mise dans `current_task->syscall_retval` par le pilote clavier
                // lorsque l'entrée sera complétée (touche Entrée pressée).
                // EAX (via `regs[STACK_IDX_EAX]`) sera mis à jour après le réveil de la tâche.
                schedule(); // Changer de tâche. `current_task` peut changer ici si une autre tâche est élue.
                            // La tâche actuelle (celle qui a appelé SYS_GETS) est maintenant endormie (TASK_WAITING_FOR_KEYBOARD).
                            // Elle ne reprendra son exécution (ici, dans le noyau) qu'après avoir été réveillée
                            // par le gestionnaire d'interruption clavier et que `schedule()` l'ait choisie à nouveau.
                            // Lorsque cela se produit, l'exécution reprend juste après l'appel à `schedule()`.

                // Au réveil, `current_task` est la tâche qui a initié l'appel SYS_GETS et qui est maintenant réveillée.
                // `regs` pointe toujours vers la zone de pile sauvegardée pour cette tâche.
                // La valeur de retour (nombre de chars lus) a été placée dans `syscall_retval` par le handler clavier.
                regs[STACK_IDX_EAX] = current_task->syscall_retval;
            }
            break;

        case 5: // SYS_EXEC - Exécuter un nouveau programme.
            {
                const char* path = (const char*)regs[STACK_IDX_EBX]; // EBX: chemin du fichier exécutable.
                char** argv = (char**)regs[STACK_IDX_ECX];          // ECX: tableau d'arguments (argv).

                if (path == NULL) {
                    regs[STACK_IDX_EAX] = (uint32_t)-1; // Erreur: chemin invalide.
                    break;
                }
                // Tenter de créer et charger le nouveau processus utilisateur.
                int child_pid = create_user_process(path, argv);

                if (child_pid < 0) { // Si la création du processus a échoué.
                    regs[STACK_IDX_EAX] = (uint32_t)-1; // Retourner une erreur (PID négatif).
                } else {
                    // Si la création réussit, le processus parent (courant) attend la fin du fils.
                    // C'est un comportement simplifié type `fork+wait` ou `spawn bloquant`.
                    current_task->state = TASK_WAITING_FOR_CHILD; // Mettre le parent en attente.
                    current_task->child_pid_waiting_on = child_pid; // Noter quel enfant il attend.
                    schedule(); // Changer de tâche.
                    // Au réveil (après la fin de l'enfant), le statut de sortie de l'enfant
                    // aura été placé dans `current_task->child_exit_status` par SYS_EXIT de l'enfant.
                    regs[STACK_IDX_EAX] = (uint32_t)current_task->child_exit_status; // Retourner le statut de sortie de l'enfant.
                }
            }
            break;

        default: // Numéro d'appel système inconnu.
            regs[STACK_IDX_EAX] = (uint32_t)-1; // Retourner une erreur.
            break;
    }
}

// Initialise le mécanisme des appels système.
void syscall_init() {
    // Enregistre `syscall_interrupt_handler_asm` comme gestionnaire pour l'interruption 0x80.
    // Drapeaux 0xEE :
    //   - P (Présent) = 1
    //   - DPL (Niveau de Privilège du Descripteur) = 3 (0b11) -> Permet l'appel depuis le mode utilisateur (ring 3).
    //   - Type = 0xE (Trap Gate 32 bits). Les interruptions sont activées pendant le handler.
    // Sélecteur 0x08 : Sélecteur du segment de code du noyau (Kernel Code Segment).
    idt_set_gate(0x80, (uint32_t)(uintptr_t)syscall_interrupt_handler_asm, 0x08, 0xEE);
    // print_string("Gestionnaire d'appels systeme enregistre pour int 0x80.\n", 0x0F); // Message de débogage (blanc sur noir)
}
