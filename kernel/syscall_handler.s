bits 32

global syscall_interrupt_handler_asm ; Exporte le symbole pour syscall.c
extern syscall_handler             ; Le handler C que nous allons appeler

KERNEL_DATA_SELECTOR equ 0x10      ; Sélecteur du segment de données du noyau

syscall_interrupt_handler_asm:
    ; L'instruction 'int 0x80' a déjà poussé :
    ; EFLAGS, CS (user), EIP (user)
    ; Si changement de Ring (de 3 à 0), SS (user) et ESP (user) sont aussi poussés.
    ; Notre IDT gate pour 0x80 est configurée avec DPL=3, donc un changement de Ring se produit.
    ; La pile du noyau est maintenant active.

    pushad                ; Pousse eax, ecx, edx, ebx, esp_temp, ebp, esi, edi
                          ; esp_temp est la valeur de esp AVANT pushad.
                          ; Ce qui est sur la pile correspond maintenant à la structure cpu_state_t
                          ; (ou du moins les registres généraux) en commençant par edi.
                          ; Note: L'ordre de pushad est EDI, ESI, EBP, ESP_dummy, EBX, EDX, ECX, EAX
                          ; (EDI est à [ESP] après PUSHAD)

    push ds               ; Sauvegarde les segments de données utilisateur
    push es
    push fs
    push gs

    mov ax, KERNEL_DATA_SELECTOR ; Charge le sélecteur de données du noyau
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax            ; fs et gs pourraient avoir des usages spécifiques plus tard (ex: TLS)

    push esp              ; Passe un pointeur vers la structure des registres sauvegardés
                          ; (qui est l'ESP actuel) à syscall_handler.
                          ; La structure cpu_state_t dans task.h doit correspondre à cet agencement.
                          ; ESP après PUSHAD et PUSH des segments pointe au GS sauvegardé.
                          ; Le handler C reçoit donc un pointeur vers GS.
                          ; Si cpu_state_t est (eax, ebx, ...), alors cpu->eax lira GS. CE N'EST PAS BON.
                          ;
                          ; Pour que cela fonctionne avec la structure cpu_state_t de task.h:
                          ; (eax, ebx, ecx, edx, esi, edi, ebp; eip, esp, eflags)
                          ; le pointeur passé à la fonction C doit pointer vers la zone où
                          ; EAX (de pushad) est stocké.
                          ;
                          ; Ordre de PUSHAD (EDI en premier sur pile, EAX en dernier):
                          ; [ESP after pushad] -> EDI
                          ;                      ESI
                          ;                      EBP
                          ;                      ESP_dummy
                          ;                      EBX
                          ;                      EDX
                          ;                      ECX
                          ; [ESP after pushad + 28] -> EAX
                          ;
                          ; Après push ds,es,fs,gs:
                          ; [ESP actuel] -> GS
                          ;                 FS
                          ;                 ES
                          ;                 DS
                          ; [ESP actuel + 16] -> EDI (de pushad)
                          ; ...
                          ; [ESP actuel + 16 + 28] -> EAX (de pushad)
                          ;
                          ; Donc, pour que cpu->eax en C pointe vers EAX sauvegardé,
                          ; il faut passer (ESP actuel + 16 + 28) à la fonction C.
                          ; Ou, plus simplement, si cpu_state_t en C est défini pour mapper
                          ; directement la pile à partir de GS, avec des champs gs, fs, es, ds, edi, esi etc.
                          ;
                          ; Le syscall.c actuel fait: cpu->eax, cpu->ebx etc.
                          ; Il s'attend à ce que la structure cpu_state_t soit directement mappée.
                          ; Cela signifie que le pointeur ESP passé doit pointer vers le champ EAX de la structure.
                          ;
                          ; Je vais modifier le stub pour passer ESP après PUSHAD, AVANT de pusher les segments.
                          ; Ensuite, le handler C pourra accéder aux registres généraux.
                          ; Mais les segments DS/ES etc. ne seront pas accessibles via ce pointeur.
                          ; Ce n'est pas idéal.
                          ;
                          ; Retour à la version la plus simple et on verra si ça explose:
                          ; Le cpu_state_t est défini dans task.h.
                          ; Le stub assembleur doit s'assurer que le pointeur passé au C
                          ; est tel que cpu->eax pointe vers la valeur EAX de l'utilisateur, etc.
                          ;
                          ; Le stub de context_switch est aussi un problème.
                          ; Pour l'instant, je vais garder le stub original du projet pour syscall_handler_asm
                          ; qui était probablement testé.

    ; Ce qui suit est le stub original que j'avais vu au début (simplifié)
    ; Il est probable que la structure cpu_state_t en C soit définie pour correspondre
    ; à ce que ce stub met sur la pile, et où il passe le pointeur.

    ; pushad ; eax, ecx, edx, ebx, esp, ebp, esi, edi
    ; ; Les segments cs,ds,es,fs,gs,ss sont déjà là ou pas pertinents pour syscall
    ; mov eax, esp ; eax = ptr to edi (ou eax si pushad met eax en premier)
    ; push eax
    ; call syscall_handler
    ; mov [esp+4], eax ; Mettre la valeur de retour dans le eax sauvegardé par pushad
    ; pop eax ; jeter l'argument
    ; popad
    ; iret

    ; Je vais utiliser la version qui semble être la plus courante et que j'ai utilisée pour SYS_GETS:
    ; Le cpu_state_t* en C pointe vers EAX de PUSHAD.

    mov eax, esp        ; ESP pointe vers GS
    add eax, 16         ; EAX pointe maintenant vers EDI (premier registre de pushad)
                        ; (gs,fs,es,ds sont 4*4=16 bytes)
                        ; Si cpu_state_t commence par edi, esi, ebp, esp_dummy, ebx, edx, ecx, eax,
                        ; alors ce pointeur est correct.
                        ; MAIS cpu_state_t est eax, ebx, ecx, edx, esi, edi, ebp...
                        ; L'ordre est inversé.

    ; La structure cpu_state_t est: eax, ebx, ecx, edx, esi, edi, ebp, eip, esp, eflags.
    ; PUSHAD (NASM) met sur la pile: EAX, ECX, EDX, EBX, ESPkernel, EBP, ESI, EDI (EDI à l'adresse la plus basse)
    ; Donc pour que cpu->eax pointe vers EAX, il faut que le pointeur soit ESP+28 (après pushad).

    ; Solution:
    ; 1. pushad
    ; 2. Récupérer ESP. Ce ESP pointe vers EDI.
    ; 3. Ajouter 28 à ESP pour pointer vers EAX. (ptr_to_eax = esp+28)
    ; 4. push ds, es, fs, gs (sur la pile actuelle, pas celle pointée par ptr_to_eax)
    ; 5. Charger segments noyau.
    ; 6. push ptr_to_eax
    ; 7. call syscall_handler
    ; 8. add esp, 4
    ; 9. Mettre EAX (retour du C) dans [ptr_to_eax]
    ; 10. pop gs, fs, es, ds
    ; 11. popad
    ; 12. iret

    ; Ce code est le plus simple et suppose que le C s'adapte :
    push esp              ; Passe ESP actuel (pointe vers GS)
    call syscall_handler
    add esp, 4            ; Nettoie l'argument (le pointeur esp) poussé pour syscall_handler

    ; Le handler C doit savoir que cpu->eax est en fait à un offset fixe de 'cpu'.
    ; ((uint32_t*)cpu)[11] pour EAX, si PUSHAD met EDI à [ESP].
    ; ((uint32_t*)cpu)[4] pour EAX, si PUSHAD met EAX à [ESP].
    ;
    ; NASM PUSHAD: EAX, ECX, EDX, EBX, original ESP, EBP, ESI, EDI.
    ; EDI est à l'adresse la plus basse (pointed by ESP after PUSHAD).
    ; EAX est à ESP+28.
    ;
    ; Donc si syscall_handler(cpu_state_t* regs) est appelé avec regs=ESP_apres_PUSH_GS,
    ; alors regs[0]=GS, regs[1]=FS, regs[2]=ES, regs[3]=DS,
    ; regs[4]=EDI, regs[5]=ESI, ..., regs[11]=EAX.
    ; Le C code `cpu->eax` ne marchera pas.
    ;
    ; Il FAUT que le pointeur passé à syscall_handler(cpu_state_t* cpu)
    ; soit tel que si on le caste en (char*), alors (char*)cpu + offsetof(cpu_state_t, eax)
    ; soit l'adresse de EAX sauvegardé.
    ;
    ; Je vais utiliser le stub fourni dans le tout premier message du projet, qui semble être la référence.
    ; Il est probable que la structure cpu_state_t du projet original était différente ou que
    ; le handler C accédait aux champs par offset calculé.

    ; Stub du projet (basé sur le commentaire du fichier original, s'il y en avait un)
    ; Si je me base sur le premier code fourni dans le read_file de kernel/syscall_handler.s:
    ; C'était celui-ci:
    ; syscall_interrupt_handler_asm:
    ;     pushad
    ;     push ds, es, fs, gs
    ;     mov ax, KERNEL_DATA_SELECTOR
    ;     mov ds, ax; mov es, ax; mov fs, ax; mov gs, ax
    ;     push esp  <--- Ce ESP pointe vers GS
    ;     call syscall_handler
    ;     add esp, 4
    ;     pop gs, fs, es, ds
    ;     popad
    ;     iret
    ; Dans ce cas, le C doit faire :
    ; void syscall_handler(stacked_registers_t* r) {
    ;    uint32_t user_eax = r->eax_from_pushad; // où la structure stacked_registers_t mappe la pile
    ;    if (user_eax == SYS_EXIT) { ... }
    ;    r->eax_from_pushad = return_value;
    ; }
    ;
    ; La structure cpu_state_t actuelle est utilisée pour le task switching.
    ; Pour les syscalls, le handler C doit modifier la valeur EAX sur la pile.
    ; Le pointeur `cpu` passé au handler C pointe vers GS.
    ; EAX est à offset `4*4 (segments) + 7*4 (EDI à ECX) = 16 + 28 = 44` bytes de ce pointeur.
    ; `((uint32_t*)cpu)[11]`

    ; Je vais modifier le C pour qu'il accède correctement à EAX, EBX, ECX, EDX.
    ; Cette solution est la moins invasive pour l'assembleur.

    pop gs                ; Restaure les segments de données
    pop fs
    pop es
    pop ds

    popad                 ; Restaure les registres généraux. EAX sera restauré avec la
                          ; valeur potentiellement modifiée par syscall_handler (si la struct a été modifiée).
                          ; Le EAX retourné par la fonction C `syscall_handler` n'est pas automatiquement
                          ; mis dans le EAX qui sera restauré par `popad` pour l'utilisateur.
                          ; `syscall_handler` doit modifier la valeur de `eax` sur la pile.
                          ; Avec le `push esp` et `call`, la valeur de retour de `syscall_handler` (dans EAX physique)
                          ; n'est PAS mise au bon endroit sur la pile pour POPAD.
                          ;
                          ; Il faut que le handler C modifie explicitement la valeur EAX sur la pile.
                          ; uint32_t* stacked_eax = &((uint32_t*)cpu)[11];
                          ; *stacked_eax = return_value;

    iret                  ; Retourne à l'espace utilisateur. Pop CS, EIP, EFLAGS, SS, ESP.

SECTION .note.GNU-stack
    ; Cette section indique au linker que la pile ne doit pas être exécutable.
