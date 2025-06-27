[bits 32]

section .text
global load_page_directory
global enable_paging

; void load_page_directory(uint32_t* page_directory_physical_addr);
load_page_directory:
    push ebp
    mov ebp, esp
    mov eax, [ebp + 8]  ; Récupère l'argument (page_directory_physical_addr) de la pile
    mov cr3, eax        ; Charge l'adresse du Page Directory dans le registre CR3
    pop ebp
    ret

; void enable_paging();
enable_paging:
    push ebp
    mov ebp, esp
    mov eax, cr0        ; Récupère la valeur actuelle du registre CR0
    or eax, 0x80000000  ; Met à 1 le bit 31 (PG - Paging Enable) de CR0
                        ; CR0[PG] = 1
    mov cr0, eax        ; Écrit la nouvelle valeur dans CR0 pour activer le paging
    pop ebp
    ret
