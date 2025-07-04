bits 32

section .text
global gdt_load
global segments_reload ; Exporter pour un appel potentiel depuis C, bien qu'ici on l'appelle localement.

gdt_load:
    ; Argument: pointeur vers la structure gdt_ptr sur la pile [esp+4]
    mov eax, [esp + 4]
    lgdt [eax]          ; Charge le registre GDTR avec la nouvelle table

    ; Après lgdt, les anciens sélecteurs de segment sont toujours actifs.
    ; Nous devons recharger tous les registres de segment (CS, DS, SS, ES, FS, GS).
    ; Un `jmp far` est nécessaire pour recharger CS.
    ; Les autres segments de données peuvent être rechargés avec `mov`.

    call segments_reload ; Appelle la routine pour recharger les segments.
                        ; L'adresse de retour de gdt_load sera sur la pile.
    ret                 ; Retourne à l'appelant C (gdt_init)

segments_reload:
    ; Recharger CS en faisant un "far jump" vers une étiquette dans ce segment.
    ; Le sélecteur 0x08 est pour le segment de code noyau (index 1 de la GDT).
    jmp 0x08:.reload_cs
.reload_cs:

    ; Recharger les segments de données.
    ; Le sélecteur 0x10 est pour le segment de données noyau (index 2 de la GDT).
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ret ; Retourne à gdt_load (ou à l'appelant de segments_reload si appelé directement)
