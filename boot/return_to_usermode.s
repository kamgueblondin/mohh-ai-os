bits 32

global return_to_usermode

section .text
return_to_usermode:
    ; À ce stade, la pile est configurée par create_user_process (ou un switch ultérieur)
    ; pour contenir la frame IRET nécessaire:
    ; [ESP]      -> EIP_user
    ; [ESP + 4]  -> CS_user
    ; [ESP + 8]  -> EFLAGS_user
    ; [ESP + 12] -> ESP_user
    ; [ESP + 16] -> SS_user
    ;
    ; Les registres généraux (eax, ebx, etc.) ont été restaurés par POPAD
    ; dans context_switch. Les EFLAGS du noyau ont été restaurés par POPFD
    ; dans context_switch. Le RET de context_switch a sauté ici.
    ;
    ; Il n'y a rien d'autre à faire que iret.
    iret

SECTION .note.GNU-stack
    ; Cette section indique au linker que la pile ne doit pas être exécutable.
