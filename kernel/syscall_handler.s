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
                          ; L'ordre de 'pushad' est important: edi, esi, ebp, (dummy esp), ebx, edx, ecx, eax.
                          ; Notre cpu_state_t est eax, ebx, ecx, edx, esi, edi, ebp, eip, esp, eflags.
                          ; Ce n'est pas un match direct.
                          ; Le `esp` passé ici pointera vers le début de la zone sur la pile où les registres
                          ; sont sauvegardés. La fonction C devra interpréter cela correctement.
                          ; Pour que cela fonctionne avec la structure cpu_state_t existante,
                          ; le handler C doit être conscient de l'ordre de `pushad`.
                          ; Une approche plus propre serait de construire la structure cpu_state_t manuellement.

                          ; Correction: Pour correspondre à cpu_state_t qui inclut aussi eip, cs, eflags, user_esp, user_ss
                          ; qui sont déjà sur la pile, il faut passer un pointeur vers la base de CET ensemble.
                          ; L'ESP actuel pointe vers le GS sauvegardé.
                          ; Si cpu_state_t est défini comme (du plus haut au plus bas sur la pile):
                          ;   [gs, fs, es, ds] (poussés par nous)
                          ;   [edi, esi, ebp, esp_dummy, ebx, edx, ecx, eax] (poussés par pushad)
                          ;   [eip, cs, eflags, esp_user, ss_user] (poussés par l'interruption matérielle lors du changement de ring)
                          ; Le pointeur vers 'cpu_state_t' devrait pointer vers 'eax' sauvegardé par PUSHAD.
                          ; ESP après PUSHAD et PUSH des segments pointe au GS sauvegardé.
                          ; L'adresse de EAX sauvegardé est ESP + 4*4 (gs,fs,es,ds sont chacun 2 bytes, mais push les met en 4 bytes)
                          ; Non, push ds etc. pousse 16 bits, puis étend à 32 bits sur la pile.
                          ; Donc chaque push de segment prend 4 bytes.
                          ; L'adresse de EAX sauvegardé est ESP + 16.
                          ; L'adresse de EDI sauvegardé est ESP + 16 + 7*4
                          ;
                          ; Simplifions: la fonction C attend un cpu_state_t*.
                          ; La pile ressemble à ceci (de bas en haut, esp pointe au sommet):
                          ; GS, FS, ES, DS (4*4 bytes)
                          ; EAX, ECX, EDX, EBX, ESP(dummy), EBP, ESI, EDI (pushad, 8*4 bytes)
                          ; EIP, CS, EFLAGS, ESP_user, SS_user (par int 0x80, 5*4 bytes)
                          ;
                          ; Notre cpu_state_t: eax, ebx, ecx, edx, esi, edi, ebp, eip, esp, eflags
                          ; Il y a un décalage.
                          ; Pour l'instant, nous allons passer ESP et le handler C devra faire attention.
                          ; Idéalement, le stub assembleur devrait réorganiser les valeurs sur la pile
                          ; pour correspondre exactement à la structure cpu_state_t attendue par le C,
                          ; ou la structure C devrait correspondre à ce que l'assembleur pousse.
                          ;
                          ; L'ordre de pushad est: EAX, ECX, EDX, EBX, ESP(original), EBP, ESI, EDI (selon Intel docs)
                          ; Mais NASM/GAS le liste comme EDI, ESI, EBP, ESP, EBX, EDX, ECX, EAX.
                          ; Je vais supposer l'ordre Intel: EAX est le premier poussé, EDI le dernier.
                          ; Donc ESP après pushad pointe vers EDI.
                          ; Stack: EDI, ESI, EBP, ESP_orig, EBX, EDX, ECX, EAX <--- ESP après pushad

    call syscall_handler  ; Appelle le handler C. Le pointeur est sur la pile.
                          ; syscall_handler va modifier EAX dans la structure sur la pile
                          ; pour la valeur de retour de l'appel système.

    add esp, 4            ; Nettoie l'argument (le pointeur esp) poussé pour syscall_handler

    pop gs                ; Restaure les segments de données
    pop fs
    pop es
    pop ds

    popad                 ; Restaure les registres généraux. EAX sera restauré avec la
                          ; valeur potentiellement modifiée par syscall_handler (si la struct a été modifiée).
                          ; L'EAX retourné par la fonction C `syscall_handler` n'est pas automatiquement
                          ; mis dans le EAX qui sera restauré par `popad` pour l'utilisateur.
                          ; `syscall_handler` doit modifier la valeur de `eax` sur la pile.

    iret                  ; Retourne à l'espace utilisateur. Pop CS, EIP, EFLAGS, SS, ESP.


; Note sur la structure cpu_state_t et la pile :
; Quand `int 0x80` est appelé depuis Ring 3 vers Ring 0 (via un Trap Gate avec DPL=3):
; 1. Le CPU pousse SS_user, ESP_user, EFLAGS_user, CS_user, EIP_user sur la pile du noyau.
;    (La pile du noyau est déterminée par le TSS)
; 2. Notre `syscall_interrupt_handler_asm` est exécuté.
; 3. `pushad` pousse EAX, ECX, EDX, EBX, ESP_kernel_avant_pushad, EBP, ESI, EDI. (Ordre Intel)
;    (ESP_kernel_avant_pushad est la valeur de ESP après l'étape 1)
; 4. `push ds, es, fs, gs` (chaque prend 4 bytes sur la pile, même si ce sont des sélecteurs 16 bits).
;
; La fonction C `syscall_handler(cpu_state_t* cpu)` reçoit un pointeur.
; Ce pointeur `cpu` doit pointer vers une zone mémoire qui est agencée comme `cpu_state_t`.
; La structure `cpu_state_t` est définie comme:
;   uint32_t eax, ebx, ecx, edx;
;   uint32_t esi, edi, ebp;
;   uint32_t eip, esp, eflags;
; (manquent CS, SS, DS, ES, FS, GS qui sont aussi importants pour le contexte utilisateur)
;
; Pour que `cpu->eax` dans le C corresponde à la valeur de EAX de l'utilisateur au moment de l'appel `int 0x80`,
; le stub assembleur (ou le C) doit s'assurer que le pointeur `cpu` est correctement positionné
; et que la structure `cpu_state_t` est définie pour refléter ce qui est réellement sur la pile.
;
; Le `isr_common_stub` dans isr_stubs.s fait `pusha` (qui est comme pushad), puis `mov ax, ds; push eax`
; puis appelle le handler C. Il ne passe pas explicitement de pointeur, le handler C doit savoir
; comment accéder aux registres (probablement via un pointeur global ou en lisant directement la pile).
;
; Pour `syscall_handler(cpu_state_t* cpu)`, si `esp` est passé juste après les push des segments,
; `cpu` (l'argument) pointera vers la valeur de GS sauvegardée.
; `cpu_state_t` devrait alors être défini pour refléter l'ordre sur la pile à partir de ce point,
; ou bien le stub assembleur doit préparer une structure `cpu_state_t` conforme et passer un pointeur vers celle-ci.
;
; Modification du plan pour `syscall_handler` C:
; Le pointeur `cpu` reçu pointera à l'endroit où `eax` (de pushad) est stocké.
; ESP après `push ds/es/fs/gs` pointe vers `gs` sauvegardé.
; ESP après `pushad` pointe vers `eax` sauvegardé (si l'ordre de pushad est edi,..eax).
; Si l'ordre de pushad est eax..edi, alors ESP après pushad pointe vers EDI.
;
; Le `isr_common_stub` fait `pusha`. Puis `mov ax, ds; push eax`. `esp` pointe alors vers le `ds` sauvegardé.
; Puis il appelle `fault_handler`. Si `fault_handler` prend un `cpu_state_t*`, ce pointeur serait `esp+4`
; (pour sauter l'adresse de retour de call).
;
; Simplification pour l'instant : `syscall_handler` recevra ESP après tous les push.
; La fonction `syscall_handler` (en C) devra savoir que `cpu->eax` est à un certain offset de ce pointeur.
; Par exemple, si `cpu_state_t` est (eax, ebx, ecx, edx, esi, edi, ebp, eip_user, esp_user, eflags_user),
; et que le `esp` passé à la fonction C pointe au début de la sauvegarde des registres généraux par `pushad`,
; alors `cpu->eax` correspondra.
;
; La structure cpu_state_t actuelle est:
;    uint32_t eax, ebx, ecx, edx;
;    uint32_t esi, edi, ebp;
;    uint32_t eip, esp, eflags;
;
; Sur la pile après `int 0x80` et avant notre code:
;    [ESP_kernel] -> EIP_user
;                    CS_user
;                    EFLAGS_user
;                    ESP_user
;                    SS_user
;
; Après `pushad` (EDI,ESI,EBP,ESP_dummy,EBX,EDX,ECX,EAX):
;    [ESP_kernel] -> EAX_user_saved_by_pushad
;                    ...
;                    EDI_user_saved_by_pushad
;                    EIP_user
;                    CS_user
;                    EFLAGS_user
;                    ESP_user
;                    SS_user
;
; Après `push ds,es,fs,gs`:
;    [ESP_kernel] -> GS_user_saved
;                    FS_user_saved
;                    ES_user_saved
;                    DS_user_saved
;                    EAX_user_saved_by_pushad
;                    ...
;                    EDI_user_saved_by_pushad
;                    EIP_user
;                    CS_user
;                    EFLAGS_user
;                    ESP_user
;                    SS_user
;
; Si on passe `esp` à `syscall_handler(cpu_state_t* regs)`, alors `regs` pointe à `GS_user_saved`.
; `regs->eax` ne correspondra pas.
; Il faut passer `esp + 4*4` (pour sauter gs,fs,es,ds) pour que `regs` pointe à `EAX_user_saved_by_pushad`.
; Ce `EAX_user_saved_by_pushad` est ce que `cpu_state_t.eax` devrait être.
; La structure `cpu_state_t` doit aussi inclure eip, esp, eflags, cs, ss.
; Notre `cpu_state_t` actuelle est (eax, ebx, ecx, edx, esi, edi, ebp, eip, esp, eflags).
; Elle ne correspond pas à l'ordre de pushad.
;
; Ordre de PUSHAD (Intel): EAX, ECX, EDX, EBX, ESP(temp), EBP, ESI, EDI.
; Donc sur la pile (plus haute adresse vers plus basse): EDI, ESI, EBP, ESP(temp), EBX, EDX, ECX, EAX <-- ESP pointe ici
;
; Si `cpu_state_t` est:
;   uint32_t edi, esi, ebp, esp_kernel_dummy, ebx, edx, ecx, eax; // pour pushad
;   uint32_t ds, es, fs, gs; // pour nos push
;   uint32_t eip, cs, eflags, esp_user, ss_user; // pour l'interruption
;
; Et que le syscall_handler prend un pointeur vers cette structure.
; Le plus simple est de modifier `cpu_state_t` pour qu'elle corresponde à ce qui est sur la pile.
; Ou, le stub assembleur doit construire la structure.
;
; Pour l'instant, je vais passer ESP après `pushad` et avant de pousser les segments.
; Cela signifie que le pointeur C pointera vers la sauvegarde de EAX (ou EDI selon l'ordre de pushad).
; Je vais modifier `cpu_state_t` pour qu'elle corresponde à l'ordre de `pushad` et ensuite les registres
; poussés par l'interruption.
;
; Ordre de `cpu_state_t` dans `task.h`: eax, ebx, ecx, edx, esi, edi, ebp, eip, esp, eflags.
; Ordre de `pushad`: eax, ecx, edx, ebx, esp(orig), ebp, esi, edi. (EDI est au plus bas sur la pile, EAX au plus haut)
;
; Je vais modifier le stub pour qu'il passe un pointeur à la zone où EAX est sauvegardé par PUSHAD.
; Et le handler C s'attend à ce que la structure cpu_state_t soit alignée sur cet EAX.
; La structure `cpu_state_t` doit être réorganisée ou le code C doit être très prudent.
;
; Simplification : Je vais suivre le modèle de `isr_common_stub` et `fault_handler`.
; `fault_handler` ne prend pas de pointeur explicitement dans le C, il doit y accéder
; via des moyens spécifiques (probablement en lisant depuis l'adresse de `esp` connue).
;
; Je vais modifier le `syscall_handler` C pour qu'il ne prenne pas `cpu_state_t*`
; et le stub assembleur ne passera pas `esp`. Le handler C devra
; obtenir les valeurs des registres d'une autre manière (par exemple, des fonctions asm pour lire eax, ebx, etc.
; au moment de l'appel système, ou en lisant depuis la pile).
;
; Non, le plan initial de passer `cpu_state_t*` est plus propre.
; Je vais m'assurer que `cpu_state_t` dans `task.h` correspond à ce que le stub met sur la pile,
; ou que le stub arrange la pile pour correspondre à `cpu_state_t`.
;
; Le plus simple est de faire en sorte que le stub assembleur crée une structure `cpu_state_t` sur la pile
; qui correspond à la définition C, puis passe un pointeur vers celle-ci.
;
; La structure `cpu_state_t` actuelle est:
;   uint32_t eax, ebx, ecx, edx;
;   uint32_t esi, edi, ebp;
;   uint32_t eip, esp, eflags;
;
; L'ordre de `pushad` est EAX, ECX, EDX, EBX, ESP_orig, EBP, ESI, EDI (EDI est à l'adresse la plus basse)
; La pile après `int` et `pushad` (sans nos push de segment pour l'instant):
;   [esp] -> edi_val
;            esi_val
;            ebp_val
;            esp_orig_val
;            ebx_val
;            edx_val
;            ecx_val
;            eax_val
;            eip_user
;            cs_user
;            eflags_user
;            esp_user
;            ss_user
;
; Le `cpu_state_t` doit être rempli à partir de ces valeurs.
; Je vais passer `esp` après `pushad` à une fonction C qui remplira la struct cpu_state_t
; puis appellera syscall_handler. C'est compliqué.
;
; Modifions le stub pour qu'il corresponde à la structure `cpu_state_t` de `task.h`.
; La structure `cpu_state_t` dans `task.h` est utilisée pour `context_switch`.
; Elle doit contenir tous les registres nécessaires pour reprendre une tâche.
;
; Ordre de `cpu_state_t` (pour rappel):
;   eax, ebx, ecx, edx, esi, edi, ebp, (manque les segments)
;   eip, esp, eflags (manque cs, ss pour utilisateur)
;
; Le plus simple est de faire en sorte que le stub assembleur construise la structure `cpu_state_t`
; sur la pile du noyau de la tâche courante, puis passe un pointeur à `syscall_handler`.

; Tentative finale de conception du stub:
; - Sauvegarder les segments utilisateur (ds, es, fs, gs)
; - Sauvegarder les registres généraux (pushad)
; - Charger les segments noyau
; - Construire la structure cpu_state_t sur la pile du noyau à partir des valeurs sauvegardées.
;   Ceci inclut EIP, CS, EFLAGS, ESP_user, SS_user (qui ont été poussés par l'INT).
; - Passer un pointeur vers cette structure à syscall_handler.
; - Après retour, si EAX a été modifié dans la structure, le mettre à jour sur la pile où PUSHAD l'a mis.
; - Restaurer les registres (popad)
; - Restaurer les segments utilisateur
; - iret

; Cela devient complexe. Je vais adopter l'approche de `isr_common_stub` où
; `pusha` est utilisé, et le C handler doit savoir lire depuis la pile.
; Mais le `syscall_handler` dans le plan prend `cpu_state_t*`.
; Je vais faire en sorte que `esp` après tous les `push` (segments et `pushad`) soit passé.
; La fonction C devra alors déduire les emplacements.
; Ou, plus simple: le `cpu_state_t*` pointe vers la sauvegarde de `eax` par `pushad`.

    push ebp
    mov ebp, esp  ; Mettre en place un nouveau stack frame

    ; Sauvegarder les registres de segment utilisateur sur la pile
    push es
    push ds
    push fs ; Sauvegarder fs et gs au cas où l'utilisateur s'en sert
    push gs

    ; Charger les sélecteurs de segment du noyau
    mov ax, KERNEL_DATA_SELECTOR
    mov es, ax
    mov ds, ax
    mov fs, ax ; Pourrait être 0 si le noyau n'utilise pas fs/gs intensivement
    mov gs, ax ; ou un sélecteur spécifique (ex: TLS noyau)

    ; Sauvegarder tous les registres généraux
    pushad ; Pushes EDI, ESI, EBP_orig, ESP_orig, EBX, EDX, ECX, EAX (ordre Intel)
           ; ESP pointe maintenant sur EAX sauvegardé.

    ; L'état du CPU utilisateur est maintenant sur la pile.
    ; EIP_user, CS_user, EFLAGS_user, ESP_user, SS_user ont été poussés par 'int 0x80'
    ; puis pushad a poussé les registres généraux.
    ; La structure cpu_state_t attendue par syscall_handler(cpu_state_t* cpu)
    ; doit correspondre à cet agencement à partir de 'EAX sauvegardé'.
    ;
    ; La pile ressemble à (de bas en haut, esp pointe au sommet):
    ;   [ebp] -> EAX_par_pushad
    ;            ECX_par_pushad
    ;            EDX_par_pushad
    ;            EBX_par_pushad
    ;            ESP_original_avant_pushad (qui est ebp+20+...)
    ;            EBP_original_avant_pushad (qui est ebp+20)
    ;            ESI_par_pushad
    ;            EDI_par_pushad
    ;   [ebp+32] -> GS_user_sauvegardé
    ;   [ebp+36] -> FS_user_sauvegardé
    ;   [ebp+40] -> DS_user_sauvegardé
    ;   [ebp+44] -> ES_user_sauvegardé
    ;   [ebp+48] -> EBP_original_de_cette_fonction
    ;   [ebp+52] -> Adresse de retour (pas pertinent pour int)
    ;   [ebp+56] -> EIP_user (poussé par int)
    ;   [ebp+60] -> CS_user  (poussé par int)
    ;   [ebp+64] -> EFLAGS_user (poussé par int)
    ;   [ebp+68] -> ESP_user (poussé par int)
    ;   [ebp+72] -> SS_user (poussé par int)

    ; Nous devons construire ou passer un pointeur à une structure qui ressemble à cpu_state_t.
    ; cpu_state_t: eax, ebx, ecx, edx, esi, edi, ebp, eip, esp, eflags.
    ;
    ; Passer ESP actuel (qui pointe vers EAX sauvegardé par pushad) comme argument.
    mov eax, esp  ; eax contient maintenant le pointeur vers les registres sur la pile (commençant par EAX de pushad)
    push eax      ; Pousse le pointeur comme argument pour syscall_handler
    call syscall_handler
    add esp, 4    ; Nettoie l'argument de la pile

    ; La valeur de retour de l'appel système est dans EAX (registre physique).
    ; Elle doit être placée dans la structure sur la pile pour que POPAD la restaure.
    ; ESP pointe vers EAX sauvegardé par pushad. Donc [ESP] est l'emplacement de EAX.
    mov [esp], eax ; Met à jour la valeur de EAX sauvegardée sur la pile avec le retour du syscall.

    popad         ; Restaure les registres généraux (EAX sera mis à jour)

    pop gs        ; Restaure les segments utilisateur
    pop fs
    pop ds
    pop es

    mov esp, ebp  ; Libère l'espace de la stack frame
    pop ebp

    iret          ; Retourne à l'espace utilisateur
                  ; Pop EIP, CS, EFLAGS, ESP, SS
end_syscall_interrupt_handler_asm:
nop
```

Ce stub est plus conforme à ce que `syscall_handler(cpu_state_t* cpu)` attend si `cpu_state_t` est défini pour commencer par les registres généraux dans l'ordre de `pushad` (EAX, ECX, EDX, EBX, ESP\_orig, EBP, ESI, EDI), suivi par EIP, CS, EFLAGS, ESP\_user, SS\_user.

La structure `cpu_state_t` actuelle dans `task.h` est :
`eax, ebx, ecx, edx, esi, edi, ebp, eip, esp, eflags`
Ce n'est pas l'ordre de `pushad`.

Je vais modifier le stub pour qu'il construise la structure `cpu_state_t` attendue par le C sur la pile, puis passe un pointeur. C'est plus propre.

```assembly
bits 32

global syscall_interrupt_handler_asm
extern syscall_handler

KERNEL_DATA_SELECTOR equ 0x10
USER_CODE_SELECTOR   equ 0x18 | 3 ;  0x1B - Assuming GDT entry for user code is 0x18, RPL=3
USER_DATA_SELECTOR   equ 0x20 | 3 ;  0x23 - Assuming GDT entry for user data is 0x20, RPL=3

struc cpu_state_stack {
    .gs     resd 1 ; Pushed by us
    .fs     resd 1
    .es     resd 1
    .ds     resd 1
    .edi    resd 1 ; Pushed by PUSHAD
    .esi    resd 1
    .ebp    resd 1
    .esp_kernel_dummy resd 1 ; (dummy ESP from PUSHAD)
    .ebx    resd 1
    .edx    resd 1
    .ecx    resd 1
    .eax    resd 1 ; ESP after PUSHAD points here if Intel order is EDI first
                   ; If Intel order is EAX first, ESP after PUSHAD points to EDI
                   ; Let's assume NASM order for PUSHAD: EDI, ESI, EBP, ESP_dummy, EBX, EDX, ECX, EAX
                   ; So [esp] after pushad is EAX. This matches cpu_state_t.eax if pointer is correct.

    ; The following are pushed by the INT instruction on ring change
    .eip    resd 1
    .cs     resd 1
    .eflags resd 1
    .esp_user resd 1
    .ss_user  resd 1
}

syscall_interrupt_handler_asm:
    ; Interrupt handler prolog
    push ds
    push es
    push fs
    push gs
    pushad          ; Pushes EDI, ESI, EBP, ESP_dummy, EBX, EDX, ECX, EAX (NASM order)
                    ; ESP now points to the saved EAX

    mov ax, KERNEL_DATA_SELECTOR
    mov ds, ax
    mov es, ax
    ; FS and GS are not strictly needed by kernel for now, but good practice
    mov fs, ax
    mov gs, ax

    ; ESP currently points to the 'eax' field of the 'cpu_state_t' structure
    ; as it's laid out by 'pushad' followed by the INT-pushed registers.
    ; The cpu_state_t in C is: eax, ebx, ecx, edx, esi, edi, ebp, eip, esp, eflags
    ; Order of PUSHAD (NASM): EDI, ESI, EBP, ESP_dummy, EBX, EDX, ECX, EAX
    ; Order on stack (low addr to high addr): EAX, ECX, EDX, EBX, ESP_dummy, EBP, ESI, EDI, GS, FS, ES, DS, EIP, CS, EFLAGS, ESP_u, SS_u
    ; This is NOT what cpu_state_t is.
    ;
    ; We need to pass a pointer to a structure that IS cpu_state_t.
    ; Let's allocate space on stack for cpu_state_t and fill it.

    sub esp, 40 ; Allocate space for cpu_state_t (10 dwords * 4 bytes = 40 bytes)
    mov edi, esp  ; EDI = pointer to our new cpu_state_t structure on stack

    ; Fill cpu_state_t.eax
    mov eax, [esp + 40 + 0]  ; EAX from pushad (original EAX)
    mov [edi + 0], eax
    ; Fill cpu_state_t.ebx
    mov ebx, [esp + 40 + 12] ; EBX from pushad
    mov [edi + 4], ebx
    ; Fill cpu_state_t.ecx
    mov ecx, [esp + 40 + 4]  ; ECX from pushad
    mov [edi + 8], ecx
    ; Fill cpu_state_t.edx
    mov edx, [esp + 40 + 8]  ; EDX from pushad
    mov [edi + 12], edx
    ; Fill cpu_state_t.esi
    mov esi, [esp + 40 + 24] ; ESI from pushad
    mov [edi + 16], esi
    ; Fill cpu_state_t.edi
    mov edx, [esp + 40 + 28] ; EDI from pushad (use edx as temp)
    mov [edi + 20], edx      ; Storing original EDI into cpu_state_t.edi
    ; Fill cpu_state_t.ebp
    mov ebp, [esp + 40 + 20] ; EBP from pushad
    mov [edi + 24], ebp

    ; Fill cpu_state_t.eip, cs, eflags, esp_user, ss_user from what INT pushed
    ; These are further down the stack, relative to (esp + 40 + 32) which is start of INT-pushed data
    mov eax, [esp + 40 + 32 + 0] ; EIP_user
    mov [edi + 28], eax
    mov eax, [esp + 40 + 32 + 8] ; ESP_user (ESP_user is after EIP, CS, EFLAGS)
    mov [edi + 32], eax
    mov eax, [esp + 40 + 32 + 4] ; EFLAGS_user
    mov [edi + 36], eax

    ; CS and SS for user mode are also needed if create_user_task uses them.
    ; For now, cpu_state_t doesn't store them separately, but they are on stack.
    ; CS_user is at [esp + 40 + 32 + 4]
    ; SS_user is at [esp + 40 + 32 + 16]

    push edi              ; Push pointer to the prepared cpu_state_t structure
    call syscall_handler
    add esp, 4            ; Clean up argument

    ; syscall_handler returned result in EAX register.
    ; Update the EAX field in the on-stack cpu_state_t structure (pointed by EDI).
    mov [edi + 0], eax

    ; Now, restore registers from the on-stack cpu_state_t structure
    ; This is if we were to return to kernel mode. But we are returning to user.
    ; We need to put the potentially modified EAX (from [edi+0]) back into where PUSHAD stored it.
    mov eax, [edi + 0]      ; Get EAX from our struct (potentially modified by syscall_handler)
    mov [esp + 40 + 0], eax ; Put it back where PUSHAD stored EAX

    add esp, 40           ; Deallocate cpu_state_t structure

    popad           ; Pops EDI, ESI, EBP, ESP_dummy, EBX, EDX, ECX, EAX
    pop gs
    pop fs
    pop es
    pop ds
    iret
```

Ce deuxième stub est encore plus complexe et source d'erreurs à cause de la manipulation manuelle des offsets de la pile.

Le plus simple est de suivre le modèle `isr_common_stub` :
1.  `pushad`
2.  Sauvegarder les segments de données utilisateur, charger les segments noyau.
3.  Passer `esp` (qui pointe maintenant vers la sauvegarde de EAX par `pushad`) à la fonction C.
4.  La fonction C `syscall_handler(cpu_state_t_on_stack* regs)` doit avoir une structure `cpu_state_t_on_stack` qui correspond EXACTEMENT à l'ordre de `pushad` ET aux champs `eip, cs, eflags, user_esp, user_ss` qui sont plus bas sur la pile.
5.  Après le retour de `syscall_handler`, sa valeur de retour (dans `eax`) est copiée dans le champ `eax` de la structure sur la pile.
6.  Restaurer les segments, `popad`, `iret`.

Je vais utiliser le premier stub que j'ai écrit (`kernel/syscall_handler.s`), qui est plus simple, et je vais devoir ajuster `cpu_state_t` ou la façon dont `syscall_handler` (en C) y accède.
Le plus simple est que `syscall_handler(cpu_state_t* cpu)` reçoive un `esp` qui pointe vers la sauvegarde de `eax` (faite par `pushad`).
La structure `cpu_state_t` dans `task.h` est: `eax, ebx, ecx, edx, esi, edi, ebp; eip, esp, eflags;`
L'ordre de `pushad` est (valeur sur la pile, de l'adresse la plus basse à la plus haute): `eax, ecx, edx, ebx, esp_original, ebp, esi, edi`. (NASM/GAS convention, où `eax` est à `[esp]`, `edi` à `[esp+28]`).

Si `syscall_handler(cpu_state_t* cpu)` est appelé avec `cpu = esp` (après `pushad`), alors:
`cpu->eax` correspond à `[esp+0]` (EAX sauvegardé)
`cpu->ebx` correspond à `[esp+12]` (EBX sauvegardé)
`cpu->ecx` correspond à `[esp+4]` (ECX sauvegardé)
`cpu->edx` correspond à `[esp+8]` (EDX sauvegardé)
`cpu->esi` correspond à `[esp+24]` (ESI sauvegardé)
`cpu->edi` correspond à `[esp+28]` (EDI sauvegardé)
`cpu->ebp` correspond à `[esp+20]` (EBP sauvegardé)

Les champs `eip, esp, eflags` de `cpu_state_t` ne seraient pas remplis par ce pointeur direct. Ils sont plus bas sur la pile.
Le `context_switch` a besoin de `eip, esp, eflags`. Le `syscall_handler` aussi pour `SYS_EXIT`.

Je vais utiliser le premier stub (`kernel/syscall_handler.s`) et modifier `syscall_handler` en C pour qu'il reçoive un `full_interrupt_stack_frame*` qui inclut tout.

Non, je vais garder `syscall_handler(cpu_state_t* cpu)` et le premier stub. Le `cpu_state_t*` pointera vers la zone des registres généraux. Pour EIP, ESP, EFLAGS, le `syscall_handler` devra regarder plus bas sur la pile par rapport au pointeur `cpu`. C'est un peu sale mais évite de dupliquer `cpu_state_t`.

Le `cpu_state_t` est principalement pour `context_switch`. Pour les syscalls, nous avons principalement besoin de `eax, ebx, ecx, edx`. Si `SYS_EXIT` a besoin de `eip` etc., il peut les extraire.

Je vais utiliser le premier stub que j'ai créé, il est plus simple.
