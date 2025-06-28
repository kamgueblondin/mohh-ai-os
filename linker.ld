ENTRY(_start) /* Le point d'entrée de notre OS */

PHDRS
{
    text PT_LOAD FLAGS(5); /* R-X (Read + Execute) */
    data PT_LOAD FLAGS(6); /* RW- (Read + Write) */
}

SECTIONS
{
    /* Commence à placer le code à 1MB */
    . = 1M;

    /* Section pour le code exécutable (.text) et les données en lecture seule (.rodata) */
    .text : ALIGN(4K)
    {
        *(.multiboot) /* Place l'en-tête multiboot au tout début */
        *(.text .text.* .gnu.linkonce.t.*)     /* Place tout le code exécutable */
        *(.rodata .rodata.* .gnu.linkonce.r.*) /* Place les données en lecture seule */
    } :text

    /* Section pour les données initialisées (.data) */
    .data : ALIGN(4K)
    {
        *(.data .data.* .gnu.linkonce.d.*)
        /* Constructeurs/Destructeurs globaux si utilisés par le compilateur (rare sans stdlib) */
        /* KEEP (*(SORT_NONE(.init_array))) */
        /* KEEP (*(SORT_NONE(.fini_array))) */
    } :data

    /* Section pour les données non initialisées (.bss) */
    /* Doit être dans le segment 'data' car il est RW */
    .bss : ALIGN(4K)
    {
        *(.bss .bss.* .gnu.linkonce.b.*)
        *(COMMON) /* Variables communes non initialisées */
    } :data

    /* Sections à ne pas charger en mémoire mais utiles pour le débogage ou le lien */
    /DISCARD/ :
    {
        *(.comment)
        *(.eh_frame) /* Informations pour la gestion des exceptions C++ (non utilisées ici) */
        *(.note.GNU-stack) /* Section de note pour la pile, ne doit pas être chargée */
    }
}
