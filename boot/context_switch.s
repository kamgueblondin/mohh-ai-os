bits 32

global context_switch

; Structure des arguments sur la pile lors de l'appel depuis C:
; [esp+0] : adresse de retour (vers schedule)
; [esp+4] : old_task_ptr (premier argument)
; [esp+8] : new_task_ptr (deuxième argument)

; Offset de esp_kernel dans la structure task_t.
; Calculé comme suit:
; id (4) + cpu_state_t (68) + state (4) + next (4) + parent (4) +
; child_pid_waiting_on (4) + child_exit_status (4) + argc (4) + argv_user_stack_ptr (4) = 100
%define TASK_T_ESP_KERNEL_OFFSET 100

context_switch:
    ; Sauvegarder les registres non volatiles que nous utilisons (ebp, esi, edi)
    push ebp
    mov ebp, esp
    push esi
    push edi
    ; ebx est sauvegardé par pushad

    ; 1. Sauvegarde de l'état de l'ancienne tâche (old_task)
    ; old_task_ptr est à [ebp+8] (car ebp pointe vers l'ancien ebp,
    ; +4 pour l'adresse de retour, +8 pour old_task_ptr)
    mov esi, [ebp + 8]  ; esi = old_task_ptr

    ; Si esi est NULL (old_task_ptr), cela pourrait indiquer un problème ou un cas spécial.
    ; Pour l'instant, on suppose qu'il est toujours valide dans un changement de contexte normal.

    pushfd                  ; Sauvegarde eflags sur la pile noyau de l'ancienne tâche
    pushad                  ; Sauvegarde eax, ecx, edx, ebx, esp, ebp, esi, edi sur la pile
                            ; L'ESP sauvegardé par PUSHAD est celui *avant* PUSHAD.
                            ; L'EBP sauvegardé par PUSHAD est l'EBP de la fonction appelante (schedule).

    ; Sauvegarder la valeur actuelle de ESP (qui pointe maintenant vers les registres sauvegardés
    ; sur la pile noyau de l'ancienne tâche) dans old_task_t_ptr->esp_kernel.
    ; esi (old_task_ptr) est toujours valide.
    mov [esi + TASK_T_ESP_KERNEL_OFFSET], esp  ; old_task_ptr->esp_kernel = esp

    ; 2. Chargement de l'état de la nouvelle tâche (new_task)
    ; new_task_ptr est à [ebp+12]
    mov edi, [ebp + 12] ; edi = new_task_ptr

    ; Charger new_task_t_ptr->esp_kernel dans esp.
    mov esp, [edi + TASK_T_ESP_KERNEL_OFFSET]  ; esp = new_task_ptr->esp_kernel

    ; Restaurer les registres généraux de la nouvelle tâche
    popad                   ; Restaure edi, esi, ebp, esp (ignoré), ebx, edx, ecx, eax
                            ; L'EBP restauré ici est celui de la nouvelle tâche au moment de son dernier PUSHAD.
                            ; L'EDI et ESI restaurés ici sont ceux de la nouvelle tâche.
                            ; L'EBX restauré ici est celui de la nouvelle tâche.

    ; Restaurer eflags de la nouvelle tâche
    popfd                   ; Restaure eflags

    ; Restaurer les registres non volatiles que nous avons sauvegardés au début (sauf ebx)
    pop edi
    pop esi
    pop ebp                 ; Restaure l'ebp original de context_switch (qui était celui de schedule)

    ret                     ; L'EIP de la nouvelle tâche a été placé au sommet de sa pile noyau
                            ; (pointée par esp_kernel) avant eflags, donc 'ret' le chargera.
                            ; Ce 'ret' retourne à l'EIP qui est au sommet de la pile pointée
                            ; maintenant par ESP (après popfd).

SECTION .note.GNU-stack
    ; Cette section indique au linker que la pile ne doit pas être exécutable.
