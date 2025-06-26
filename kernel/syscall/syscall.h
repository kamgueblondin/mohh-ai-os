#ifndef SYSCALL_H
#define SYSCALL_H

// cpu_state_t est nécessaire pour la déclaration de syscall_handler.
// Elle est définie dans kernel/task/task.h
#include "kernel/task/task.h"

// Initialise le gestionnaire d'appels système.
// Enregistre le handler pour l'interruption 0x80.
void syscall_init();

// Le handler C pour les appels système.
// Il est appelé par le stub d'interruption pour int 0x80.
// Le pointeur est vers la pile (GS sauvegardé), pas une vraie structure cpu_state_t.
void syscall_handler(void* stack_ptr_raw);

#endif // SYSCALL_H
