bits 32

section .note.GNU-stack noalloc noexec nowrite progbits
    ; This section is just for the note.

section .text                 ; All code and data from here should be in .text
    global context_switch

    %define OFF_EAX    0
    %define OFF_EBX    4
    %define OFF_ECX    8
    %define OFF_EDX    12
    %define OFF_ESI    16
    %define OFF_EDI    20
    %define OFF_EBP    24
    %define OFF_EIP    28
    %define OFF_ESP    32
    %define OFF_EFLAGS 36

context_switch:
    push esi
    push edi

    mov esi, [esp + 12]  ; esi = old_state_ptr
    mov edi, [esp + 16]  ; edi = new_state_ptr

    mov eax, [esp + 8]
    mov [esi + OFF_EIP], eax

    lea eax, [esp + 12]
    mov [esi + OFF_ESP], eax

    mov [esi + OFF_EBP], ebp
    pushfd
    pop dword [esi + OFF_EFLAGS]

    mov esp, [edi + OFF_ESP]
    mov ebp, [edi + OFF_EBP]

    push dword [edi + OFF_EFLAGS]
    popfd

    push dword [edi + OFF_EIP]

    pop edi
    pop esi

    ret
