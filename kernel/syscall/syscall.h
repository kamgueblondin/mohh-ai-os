#ifndef SYSCALL_H
#define SYSCALL_H

#include "interrupts.h" // Pour cpu_state_t si syscall_handler est aussi déclaré ici

// Initialise le gestionnaire d'appels système.
// Enregistre le handler pour l'interruption 0x80.
void syscall_init();

// Le handler C pour les appels système.
// Il est appelé par le stub d'interruption pour int 0x80.
// Note: La définition de cpu_state_t est dans interrupts.h
void syscall_handler(cpu_state_t* cpu);

#endif // SYSCALL_H
