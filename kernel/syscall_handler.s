bits 32

section .note.GNU-stack noalloc noexec nowrite progbits

section .text
global syscall_interrupt_handler_asm

syscall_interrupt_handler_asm:
    ; Désactiver les interruptions pour être sûr pendant la manipulation VGA
    cli

    ; Modifier directement la mémoire VGA
    ; Adresse de la mémoire vidéo VGA: 0xB8000
    ; Nous allons modifier le 3ème caractère (index 2)
    mov edi, 0xB8000
    mov word [edi + 2*2], 0x2F53 ; Caractère 'S' (0x53), Fond Vert (0x2), Texte Blanc Brillant (0xF)

    ; Réactiver les interruptions (ou pas, selon ce qu'on veut tester après iret)
    ; Pour un test minimal, on peut omettre sti ici.
    ; sti

    iret
