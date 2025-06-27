ENTRY(_start) /* Le point d'entrée de notre OS */

SECTIONS
{
    /* Commence à placer le code à 1MB */
    . = 1M;

    /* Section pour le code exécutable (.text) */
    .text :
    {
        *(.multiboot) /* Place l'en-tête multiboot au tout début */
        *(.text)     /* Place tout le reste du code (.text) après */
    }

    /* Section pour les données initialisées (.data) */
    .data :
    {
        *(.data)
    }

    /* Section pour les données non initialisées (.bss) */
    .bss :
    {
        *(.bss)
    }
}
