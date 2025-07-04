bits 32

section .note.GNU-stack noalloc noexec nowrite progbits
    ; This section is just for the note.

section .text                 ; All code and data from here should be in .text
    global context_switch

    ; Offsets dans cpu_state_t (doivent correspondre à task.h)
    ; Ces offsets sont relatifs au début de la structure cpu_state_t
    ; %define OFF_EAX    0  ; Non utilisé directement par ce switch, PUSHAD/POPAD gère
    ; %define OFF_EBX    4
    ; %define OFF_ECX    8
    ; %define OFF_EDX    12
    ; %define OFF_ESI    16
    ; %define OFF_EDI    20
    ; %define OFF_EBP    24
    ; %define OFF_EIP    28 ; Selon la structure cpu_state_t dans task.h
    %define OFF_ESP    32 ; Selon la structure cpu_state_t dans task.h (après eip)
    ; %define OFF_EFLAGS 36 ; Selon la structure cpu_state_t dans task.h

context_switch:
    ; Arguments sur la pile du scheduler (appelant C):
    ; [esp+0] = Adresse de retour vers le scheduler (après l'appel à context_switch)
    ; [esp+4] = old_state_ptr (pointeur vers la TCB de l'ancienne tâche)
    ; [esp+8] = new_state_ptr (pointeur vers la TCB de la nouvelle tâche)

    ; --- Sauvegarder l'état de l'ancienne tâche ---
    mov esi, [esp + 4]      ; esi = old_state_ptr

    ; Sauvegarder les registres sur la pile actuelle (celle du noyau de l'ancienne tâche)
    pushfd                  ; Pousse EFLAGS de l'ancienne tâche
    pushad                  ; Pousse EAX, ECX, EDX, EBX, ESP_original, EBP, ESI, EDI de l'ancienne tâche
                            ; ESP_original est l'ESP *avant* PUSHAD.

    ; Sauvegarder le pointeur de pile actuel (qui pointe maintenant vers EDI sauvegardé par PUSHAD)
    ; dans la TCB de l'ancienne tâche. C'est l'ESP du noyau de l'ancienne tâche.
    mov [esi + OFF_ESP], esp

    ; --- Restaurer l'état de la nouvelle tâche ---
    mov edi, [esp + 8]      ; edi = new_state_ptr (TCB de la nouvelle tâche)
                            ; esp n'a pas changé depuis le début de cette fonction pour l'accès aux args.

    ; Charger l'ESP du noyau de la nouvelle tâche.
    ; Cet ESP (stocké dans new_state_ptr->cpu_state.esp) doit pointer vers :
    ;   - La trame PUSHAD (EDI au sommet), puis EFLAGS.
    ;   - Pour une nouvelle tâche utilisateur, au-dessus de cela, la trame IRET (EIP, CS, EFLAGS, ESP_user, SS_user).
    mov esp, [edi + OFF_ESP]

    ; Restaurer les registres généraux et EFLAGS depuis la pile noyau de la nouvelle tâche
    popad                   ; Restaure EDI, ESI, EBP, ESP_dummy, EBX, EDX, ECX, EAX
                            ; ESP_dummy est ignoré, l'ESP est restauré à sa valeur avant pushad.
    popfd                   ; Restaure EFLAGS

    iret                    ; Saute vers EIP_new, CS_new avec EFLAGS_new (déjà restauré par popfd),
                            ; et charge ESP_new, SS_new si changement de privilège.
                            ; Si pas de changement de privilège (noyau -> noyau), seuls EIP, CS, EFLAGS
                            ; sont effectivement chargés depuis la pile (SS:ESP ne sont pas touchés si CPL identique).
                            ; Pour une transition vers le mode utilisateur, les 5 valeurs sont dépilées.
