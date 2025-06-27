ENTRY(_start) /* Le point d'entrée de notre OS */

PHDRS
{
    text PT_LOAD FLAGS(5) ; RX = Read(4) + Execute(1) = 5
    data PT_LOAD FLAGS(6) ; RW = Read(4) + Write(2) = 6
    rodata PT_LOAD FLAGS(4) ; R = Read(4)
}

SECTIONS
{
    /* Commence à placer le code à 1MB */
    . = 1M;
    kernel_start_addr = .;

    /* Section pour le code exécutable (.text) */
    .text : ALIGN(4K)
    {
        *(.multiboot) /* Place l'en-tête multiboot au tout début */
        *(.text*)    /* Place tout le reste du code (.text et .text.*) après */
        *(.rodata*)  /* Données en lecture seule après le code */
    } :text :rodata /* Assigner .text et .rodata au segment 'text' (RX) et 'rodata' (R) */
                  /* Le linker devrait placer .rodata dans un segment R si possible,
                     sinon le mettre avec .text est une approximation.
                     Une meilleure approche serait d'avoir un segment PHDRS distinct pour rodata si on veut R et non RX.
                     Pour l'instant, on le groupe avec text pour la simplicité du PHDRS, ou on le met dans data.
                     Alternative: .text -> :text, .rodata -> :rodata. */

    /* Section pour les données initialisées (.data) */
    /* ALIGN(4K) est pour s'assurer que le segment de données commence sur une nouvelle page,
       ce qui est une bonne pratique pour les permissions de page. */
    .data : ALIGN(4K)
    {
        *(.data*)
    } :data

    /* Section pour les données non initialisées (.bss) */
    .bss : ALIGN(4K)
    {
        *(.bss*)
    } :data /* Les sections BSS vont aussi dans le segment de données RW */

    kernel_end_addr = ALIGN(4K); /* Adresse de fin du noyau, alignée sur une page */
}
