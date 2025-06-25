#include "syscall.h"
#include "interrupts.h" // Pour idt_set_gate (indirectement, car syscall_init l'utilisera)
#include "idt.h"        // Pour idt_set_gate et la définition de cpu_state_t
#include "task/task.h"  // Pour current_task, TASK_TERMINATED, schedule(), cpu_state_t
#include <stdint.h>

// Déclarations externes pour les variables/fonctions de kernel.c
// Ces variables globales contrôlent la position du curseur VGA et la couleur.
// Idéalement, il faudrait une meilleure abstraction, mais pour l'instant, c'est direct.
extern int vga_x;
extern int vga_y;
extern char current_color; // Utilisé par print_char

// Déclaration externe pour la fonction print_char de kernel.c
// Signature: void print_char(char c, int x, int y, char color);
// Pour SYS_PUTC, nous allons utiliser vga_x, vga_y, current_color implicitement
// en appelant une version de print_char qui les utilise.
// Pour le moment, la version de print_char prend x, y, color.
// Nous allons appeler print_char(char_to_print, vga_x, vga_y, current_color);
// et print_char s'occupera de mettre à jour vga_x, vga_y.
extern void print_char(char c, int x, int y, char color);


// Déclaration externe pour la tâche courante (définie dans task.c)
extern volatile task_t* current_task;

// Déclaration du stub assembleur pour l'interruption 0x80
// Ce stub sera défini dans un fichier .s (par exemple, kernel/syscall_handler.s ou boot/isr_stubs.s)
// Il est responsable de sauvegarder l'état, appeler syscall_handler_c, restaurer l'état et iret.
extern void syscall_interrupt_handler_asm();

// Le handler C appelé par le stub assembleur de l'int 0x80
// Le pointeur cpu_state_t pointe vers la structure des registres sauvegardés sur la pile de la tâche.
void syscall_handler(cpu_state_t* cpu) {
    if (!cpu) return; // Sécurité

    // Le numéro de syscall est dans le registre EAX
    uint32_t syscall_number = cpu->eax;

    switch (syscall_number) {
        case 0: // SYS_EXIT
            if (current_task) {
                current_task->state = TASK_TERMINATED; // Marquer comme terminé
            }
            // print_string("Task exited via syscall.\n", 0x0F); // Debug
            schedule(); // Lancer le scheduler. On ne reviendra jamais à cette tâche.
                        // schedule() doit être capable de gérer les tâches terminées.
            break;

        case 1: // SYS_PUTC
            // L'argument (le caractère) est dans EBX
            // Utilise les globales vga_x, vga_y, current_color de kernel.c
            // print_char s'occupe d'avancer le curseur et du scroll.
            print_char((char)cpu->ebx, vga_x, vga_y, current_color);
            // Pas de valeur de retour spécifique pour putc via EAX pour l'instant
            break;

        default:
            // Syscall inconnu. Pour l'instant, ne rien faire.
            // Plus tard, on pourrait retourner une erreur dans EAX.
            // print_string("Unknown syscall: ", 0x0C);
            // char num_str[12];
            // itoa(syscall_number, num_str, 10); // itoa non disponible ici
            // print_string(num_str, 0x0C);
            // print_string("\n", 0x0C);
            cpu->eax = -1; // Indiquer une erreur (convention)
            break;
    }
}

void syscall_init() {
    // Enregistre notre handler assembleur pour l'interruption 0x80.
    // Le flag 0xEE:
    // - Bit 7 (Présent): 1 (l'entrée est présente)
    // - Bits 6-5 (DPL - Descriptor Privilege Level): 3 (11b) -> Permet l'appel depuis Ring 3
    // - Bit 4 (Storage Segment): 0 (pour les gates d'interruption/trap)
    // - Bits 3-0 (Type): 1110b (0xE) -> Trap Gate 32 bits
    // Le sélecteur 0x08 est le sélecteur de segment de code du noyau.
    idt_set_gate(0x80, (uint32_t)syscall_interrupt_handler_asm, 0x08, 0xEE);
    // print_string("Syscall handler registered for int 0x80.\n", 0x0F); // Debug
}
