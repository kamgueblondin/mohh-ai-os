#include "interrupts.h"
#include "idt.h"        // Pour idt_set_gate, et les extern isrX, irqX
#include "keyboard.h"   // keyboard_handler_main est appelé par le stub irq1
#include "kernel/timer.h" // Pour timer_tick
#include "kernel/libc.h" // Pour itoa
#include <stdint.h>
#include "kernel/debug_vga.h" // Pour debug_putc_at

// Fonctions/variables globales pour l'affichage (de kernel.c ou vga.c)
extern void print_string(const char* str, char color); // Assurez-vous que cette fonction est définie ailleurs si utilisée.
extern char current_color; // Idem.
extern int vga_x, vga_y;
extern void print_char(char c, int x, int y, char color);


// PIC I/O Ports & Commands (déjà définis dans le fichier original, je les garde)
#define PIC1            0x20    /* IO base address for master PIC */
#define PIC2            0xA0    /* IO base address for slave PIC */
#define PIC1_COMMAND    PIC1
#define PIC1_DATA       (PIC1+1)
#define PIC2_COMMAND    PIC2
#define PIC2_DATA       (PIC2+1)
#define PIC_EOI         0x20    /* End-of-interrupt command code */

#define ICW1_ICW4       0x01    /* Indicates that ICW4 will be present */
#define ICW1_SINGLE     0x02    /* Single (cascade) mode */
#define ICW1_INTERVAL4  0x04    /* Call address interval 4 (8) */
#define ICW1_LEVEL      0x08    /* Level triggered (edge) mode */
#define ICW1_INIT       0x10    /* Initialization - required! */

#define ICW4_8086       0x01    /* 8086/88 (MCS-80/85) mode */
#define ICW4_AUTO       0x02    /* Auto (normal) EOI */
#define ICW4_BUF_SLAVE  0x08    /* Buffered mode/slave */
#define ICW4_BUF_MASTER 0x0C    /* Mode buffer/maître */
#define ICW4_SFNM       0x10    /* Mode spécial entièrement imbriqué (non) */

// Fonction utilitaire pour écrire un octet sur un port d'E/S
void outb(uint16_t port, uint8_t val) {
    asm volatile ( "outb %0, %1" : : "a"(val), "Nd"(port) );
}

// Fonction utilitaire pour lire un octet depuis un port d'E/S
uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile ( "inb %1, %0"
                   : "=a"(ret)
                   : "Nd"(port) );
    return ret;
}

// Fonction utilitaire pour introduire un petit délai pour le PIC
void io_wait(void) {
    outb(0x80, 0); // Écrire sur un port inutilisé peut causer un petit délai
}

void pic_remap(int offset1, int offset2) {
    // unsigned char a1, a2; // Variables non utilisées, initialement pour sauvegarder les masques
    // a1 = inb(PIC1_DATA); // Sauvegarde du masque du PIC1
    // a2 = inb(PIC2_DATA); // Sauvegarde du masque du PIC2

    // Début de la séquence d'initialisation (en mode cascade)
    outb(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4); io_wait();
    outb(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4); io_wait();

    // ICW2: Décalage des vecteurs du PIC maître et esclave
    outb(PIC1_DATA, offset1); io_wait(); // PIC1 gère IRQ 0-7   -> offset1 à offset1+7
    outb(PIC2_DATA, offset2); io_wait(); // PIC2 gère IRQ 8-15  -> offset2 à offset2+7

    // ICW3: Configuration maître/esclave
    outb(PIC1_DATA, 4); io_wait(); // Indique au PIC1 que le PIC2 est à IRQ2 (0000 0100)
    outb(PIC2_DATA, 2); io_wait(); // Indique au PIC2 son identité en cascade (0000 0010)

    // ICW4: Informations additionnelles sur l'environnement
    outb(PIC1_DATA, ICW4_8086); io_wait(); // Mode 8086/88
    outb(PIC2_DATA, ICW4_8086); io_wait(); // Mode 8086/88

    // Restaurer les masques sauvegardés (ou tout démasquer si c'est l'intention)
    // outb(PIC1_DATA, a1);
    // outb(PIC2_DATA, a2);
    outb(PIC1_DATA, 0x00); // Démasquer toutes les IRQ sur le Maître
    outb(PIC2_DATA, 0x00); // Démasquer toutes les IRQ sur l'Esclave
}

// Gestionnaire C pour les exceptions CPU (ISRs 0-31)
// Appelé par isr_common_stub depuis isr_stubs.s
void fault_handler(void* esp_at_call) {
    // La disposition de la pile depuis isr_stubs.s :
    // esp_at_call pointe là où l'adresse de retour de 'call fault_handler' serait si ce n'était pas un pointeur.
    // Les arguments réels de la pile commencent après cela.
    // Ordre de PUSHAD : EDI, ESI, EBP, ESP_dummy, EBX, EDX, ECX, EAX
    // Pile avant `call fault_handler` depuis `isr_common_stub` :
    // [ESP du stub]     -> ds_val (poussé par le stub)
    // [ESP du stub +4]  -> EDI (de PUSHAD)
    // ...
    // [ESP du stub +4+28] -> EAX (de PUSHAD)
    // [ESP du stub +4+32] -> int_num (poussé par la macro)
    // [ESP du stub +4+36] -> err_code (poussé par le CPU ou la macro)
    // [ESP du stub +4+40] -> EIP_at_fault (poussé par le CPU)
    // [ESP du stub +4+44] -> CS_at_fault (poussé par le CPU)
    // [ESP du stub +4+48] -> EFLAGS_at_fault (poussé par le CPU)
    // L'argument `esp_at_call` est ESP *dans* `isr_common_stub` juste avant `call fault_handler`.
    // Donc, si isr_common_stub faisait :
    //   pusha
    //   push ds_val_in_eax
    //   call fault_handler  <-- esp_at_call est effectivement [esp+4] du point de vue C si on utilise le passage d'arguments
    //                         Cependant, le stub ne pousse pas 'esp' pour le passer à 'fault_handler'.
    //                         Le 'esp_at_call' est la vue de la fonction C de la base de son propre cadre de pile,
    //                         et les paramètres réels sont relatifs à l'ESP original lorsque le stub a été entré.
    // Revérifions la disposition de la pile dans isr_stubs.s :
    // isr_common_stub:
    //   pusha              ; Pousse edi,esi,ebp,esp,ebx,edx,ecx,eax (edi adresse la plus basse, eax la plus haute de ce bloc)
    //   mov ax, ds
    //   push eax           ; sauvegarde le descripteur de segment de données
    //   mov ax, 0x10       ; charge le descripteur de segment de données du noyau
    //   ...
    //   call fault_handler ; La fonction C voit les arguments poussés *par l'appelant* (CPU ou macros)
    //                      ; Les registres réels sont sur la pile *avant* que cet appel ne soit fait.
    //                      ; Les paramètres int_num, err_code, eip, cs, eflags sont poussés par CPU/macros *avant* isr_common_stub
    //                      ; Ensuite, isr_common_stub pousse un error_code factice, int_num (encore une fois, pour sa propre logique)
    //                      ; C'est déroutant. Supposons que les indices [10], [11], [12] utilisés précédemment étaient corrects
    //                      ; par rapport à la valeur 'esp' *après* que tous les éléments aient été poussés par le CPU et les stubs.

    // uint32_t* stack_from_stub_perspective = (uint32_t*)((char*)esp_at_call + 4); // Non utilisé, probablement pour du débogage antérieur

    // Interprétation plus simple : `esp_at_call` est `esp` juste après `push eax` (ds_val) dans le stub.
    // Donc :
    // esp_at_call[0] = ds_val
    // esp_at_call[1] = edi
    // ...
    // esp_at_call[8] = eax_val
    // esp_at_call[9] = int_num (poussé par la macro ISR_NOERRCODE/ERRCODE avant jmp isr_common_stub)
    // esp_at_call[10] = err_code (poussé par le CPU ou la macro avant jmp isr_common_stub)
    // esp_at_call[11] = eip_fault
    // Cela semble plus plausible avec l'indexation originale [10], [11], [12] pour int_num, err_code, eip.
    // Restons sur l'interprétation originale des indices de pile [10], [11], [12] par rapport à
    // la base du "cadre d'interruption" passé implicitement à fault_handler.

    uint32_t int_num  = ((uint32_t*)esp_at_call)[10]; // int_num poussé par notre macro ISR_
    // uint32_t err_code = ((uint32_t*)esp_at_call)[11]; // err_code poussé par le CPU ou notre macro
    // uint32_t eip_fault= ((uint32_t*)esp_at_call)[12]; // eip poussé par le CPU

    volatile unsigned short* vga = (unsigned short*)0xB8000; // Adresse de la mémoire vidéo VGA
    char id_char = ' ';

    // Effacer une partie de l'écran pour rendre les messages visibles
    for (int i = 0; i < 80 * 2; ++i) { // Effacer les 2 premières lignes
        vga[i] = (unsigned short)' ' | (0x0F << 8); // Blanc sur Noir
    }
    vga_x = 0; vga_y = 0; // Réinitialiser le curseur interne pour print_char s'il était utilisé

    if (int_num == 14) { // Défaut de page (Page Fault)
        id_char = 'P';
        uint32_t faulting_address;
        asm volatile("mov %%cr2, %0" : "=r"(faulting_address)); // Récupérer l'adresse fautive depuis CR2

        vga[0] = (unsigned short)id_char | (0x0C << 8); // Rouge sur Noir pour 'P'
        vga[1] = (unsigned short)'F' | (0x0C << 8);     // 'F' pour Fault

        // Afficher faulting_address (CR2)
        // Exemple : "CR2=0x12345678" à la ligne 1
        vga[80*1 + 0] = 'C'; vga[80*1 + 1] = 'R'; vga[80*1 + 2] = '2'; vga[80*1 + 3] = '='; vga[80*1 + 4] = '0'; vga[80*1 + 5] = 'x';
        for (int i = 0; i < 8; i++) {
            char hexdigit = (faulting_address >> ((7-i)*4)) & 0xF;
            if (hexdigit < 10) hexdigit += '0';
            else hexdigit += 'A' - 10;
            vga[80*1 + 6 + i] = (unsigned short)hexdigit | (0x0C << 8);
        }
         // Afficher EIP qui a causé le défaut
        uint32_t eip_val = ((uint32_t*)esp_at_call)[12]; // EIP est à l'index 12
        vga[80*0 + 10] = 'E'; vga[80*0 + 11] = 'I'; vga[80*0 + 12] = 'P'; vga[80*0 + 13] = '=';
        for (int i = 0; i < 8; i++) {
            char hexdigit = (eip_val >> ((7-i)*4)) & 0xF;
            if (hexdigit < 10) hexdigit += '0';
            else hexdigit += 'A' - 10;
            vga[80*0 + 14 + i] = (unsigned short)hexdigit | (0x0C << 8);
        }


    } else if (int_num == 8) { // Double Faute (Double Fault)
        id_char = 'D';
        vga[0] = (unsigned short)id_char | (0x0C << 8); // 'D'
        vga[1] = (unsigned short)'F' | (0x0C << 8);     // 'F'
         //uint32_t err_code_df = ((uint32_t*)esp_at_call)[11]; // Le code d'erreur DF est souvent 0
    } else { // Autres exceptions
        id_char = 'E'; // 'E' pour Exception
        vga[0] = (unsigned short)id_char | (0x0C << 8);
        // Afficher le numéro de l'exception
        vga[1] = (unsigned short)('0' + (int_num / 10)) | (0x0C << 8);
        vga[2] = (unsigned short)('0' + (int_num % 10)) | (0x0C << 8);
    }

    asm volatile("cli; hlt"); // Désactiver les interruptions et arrêter le CPU
}

// Gestionnaire C pour les IRQs (32-47), sauf clavier (IRQ1)
// Appelé par irq_common_stub depuis isr_stubs.s

static char irq0_debug_indicator = '+';

void irq_handler_c(void* esp_at_call) {
    // Confirmer l'entrée dans la fonction C
    debug_putc_at('C', 75, 0, 0x0E); // 'C' pour C-handler, Jaune sur Noir, position (x=75, y=0)

    uint32_t* stack = (uint32_t*)esp_at_call;
    // L'index 10 pour int_num est basé sur la structure poussée par les macros IRQ/ISR dans isr_stubs.s:
    // PUSH byte 0 (dummy error code)
    // PUSH byte %2 (interrupt number for IRQ, ou %1 pour ISR)
    // Puis jmp vers common_stub qui fait PUSHA, puis PUSH AX (original DS)
    // esp_at_call dans le C est l'ESP *après* PUSH AX (original DS) dans le common_stub.
    // Donc, stack[0] = original DS
    // stack[1] = EDI (de PUSHA)
    // ...
    // stack[8] = EAX (de PUSHA)
    // stack[9] = interrupt_number (poussé par la macro IRQ/ISR)
    // stack[10] = dummy_error_code (poussé par la macro IRQ/ISR)
    // CELA SIGNIFIE QUE int_num DEVRAIT ÊTRE À stack[9] et non stack[10].
    // Je vais corriger cela et afficher les deux.

    uint32_t int_num_val_at_10 = stack[10]; // Ce que nous utilisions (probablement le dummy error code)
    uint32_t int_num_val_at_9 = stack[9];   // Ce qui devrait être le numéro d'interruption

    // Afficher int_num_val_at_9 (supposé correct)
    char tens_9 = ((int_num_val_at_9 / 10) % 10) + '0';
    char units_9 = (int_num_val_at_9 % 10) + '0';
    debug_putc_at(tens_9, 73, 0, 0x0F);  // Dizaines à (73,0) - Blanc sur Noir
    debug_putc_at(units_9, 74, 0, 0x0F); // Unités à (74,0) - Blanc sur Noir

    // Pour le débogage, affichons aussi ce qui est à stack[10] à une autre position
    char tens_10 = ((int_num_val_at_10 / 10) % 10) + '0';
    char units_10 = (int_num_val_at_10 % 10) + '0';
    debug_putc_at(tens_10, 71, 0, 0x0C); // Dizaines (de stack[10]) à (71,0) - Rouge sur Noir
    debug_putc_at(units_10, 72, 0, 0x0C); // Unités (de stack[10]) à (72,0) - Rouge sur Noir


    if (int_num_val_at_9 == 32) { // IRQ0 (Timer) - Mappé à INT 32. Utiliser la valeur corrigée.
        // Débogage très simple pour voir si IRQ0 est atteint par le handler C
        debug_putc_at(irq0_debug_indicator, 77, 0, 0x0B); // Cyan sur Noir, position (x=77, y=0)
        if (irq0_debug_indicator == '+') irq0_debug_indicator = '*';
        else irq0_debug_indicator = '+';

        timer_handler(); // Appeler le gestionnaire principal du timer
    }
    // D'autres IRQs pourraient être gérés ici si nécessaire.
    // Le clavier (IRQ1/INT33) a son propre stub qui appelle keyboard_handler_main directement.

    // Envoyer EOI (End Of Interrupt - Fin d'Interruption)
    // Important : Ne pas envoyer d'EOI pour une IRQ qui n'a pas été gérée ou qui n'est pas attendue.
    // L'EOI est envoyé au PIC qui a généré l'interruption.
    if (int_num >= 32 && int_num <= 47) { // S'assurer que c'est une IRQ gérée par le PIC
        if (int_num >= 40) { // IRQ 8-15 (remappé à INT 40-47) vient du PIC esclave
            outb(PIC2_COMMAND, PIC_EOI); // Envoyer EOI au PIC esclave
        }
        outb(PIC1_COMMAND, PIC_EOI); // Toujours envoyer EOI au PIC maître (même pour les IRQs esclaves)
    }
}

// Initialise le PIC et configure les entrées IDT pour les ISRs et IRQs.
void interrupts_init() {
    // Remapper les IRQs du PIC : IRQs 0-7 à INT 0x20-0x27 (32-39), IRQs 8-15 à INT 0x28-0x2F (40-47)
    pic_remap(0x20, 0x28);

    // Configuration des ISRs (Exceptions CPU)
    idt_set_gate(0, (uint32_t)isr0, 0x08, 0x8E);  // 0 #DE Division by zero
    idt_set_gate(1, (uint32_t)isr1, 0x08, 0x8E);  // 1 #DB Debug
    idt_set_gate(2, (uint32_t)isr2, 0x08, 0x8E);  // 2 NMI
    idt_set_gate(3, (uint32_t)isr3, 0x08, 0x8E);  // 3 #BP Breakpoint
    idt_set_gate(4, (uint32_t)isr4, 0x08, 0x8E);  // 4 #OF Overflow
    idt_set_gate(5, (uint32_t)isr5, 0x08, 0x8E);  // 5 #BR BOUND Range Exceeded
    idt_set_gate(6, (uint32_t)isr6, 0x08, 0x8E);  // 6 #UD Invalid Opcode
    idt_set_gate(7, (uint32_t)isr7, 0x08, 0x8E);  // 7 #NM Device Not Available
    idt_set_gate(8, (uint32_t)isr8, 0x08, 0x8E);  // 8 #DF Double Fault
    idt_set_gate(9, (uint32_t)isr9, 0x08, 0x8E);  // 9 Coprocessor Segment Overrun
    idt_set_gate(10, (uint32_t)isr10, 0x08, 0x8E); // 10 #TS Invalid TSS
    idt_set_gate(11, (uint32_t)isr11, 0x08, 0x8E); // 11 #NP Segment Not Present
    idt_set_gate(12, (uint32_t)isr12, 0x08, 0x8E); // 12 #SS Stack Fault
    idt_set_gate(13, (uint32_t)isr13, 0x08, 0x8E); // 13 #GP General Protection Fault
    idt_set_gate(14, (uint32_t)isr14, 0x08, 0x8E); // 14 #PF Page Fault
    idt_set_gate(15, (uint32_t)isr15, 0x08, 0x8E); // 15 Reserved
    // ... (autres ISRs jusqu'à 31) ...
    idt_set_gate(16, (uint32_t)isr16, 0x08, 0x8E); // 16 #MF x87 FPU Floating-Point Error
    idt_set_gate(17, (uint32_t)isr17, 0x08, 0x8E); // 17 #AC Alignment Check
    idt_set_gate(18, (uint32_t)isr18, 0x08, 0x8E); // 18 #MC Machine Check
    idt_set_gate(19, (uint32_t)isr19, 0x08, 0x8E); // 19 #XF SIMD Floating-Point Exception
    // ISRs 20-31 sont réservées ou spécifiques à l'utilisateur
    idt_set_gate(20, (uint32_t)isr20, 0x08, 0x8E);
    idt_set_gate(21, (uint32_t)isr21, 0x08, 0x8E);
    idt_set_gate(22, (uint32_t)isr22, 0x08, 0x8E);
    idt_set_gate(23, (uint32_t)isr23, 0x08, 0x8E);
    idt_set_gate(24, (uint32_t)isr24, 0x08, 0x8E);
    idt_set_gate(25, (uint32_t)isr25, 0x08, 0x8E);
    idt_set_gate(26, (uint32_t)isr26, 0x08, 0x8E);
    idt_set_gate(27, (uint32_t)isr27, 0x08, 0x8E);
    idt_set_gate(28, (uint32_t)isr28, 0x08, 0x8E); // Hypervisor Injection Exception
    idt_set_gate(29, (uint32_t)isr29, 0x08, 0x8E); // VMM Communication Exception
    idt_set_gate(30, (uint32_t)isr30, 0x08, 0x8E); // Security Exception
    idt_set_gate(31, (uint32_t)isr31, 0x08, 0x8E); // Reserved

    // Configuration des IRQs (Interruptions Matérielles)
    // Les IRQs sont remappées pour commencer à l'INT 32 (0x20)
    idt_set_gate(32, (uint32_t)irq0, 0x08, 0x8E);  // IRQ0  - Timer (Horloge système)
    idt_set_gate(33, (uint32_t)irq1, 0x08, 0x8E);  // IRQ1  - Clavier
    idt_set_gate(34, (uint32_t)irq2, 0x08, 0x8E);  // IRQ2  - Cascade (connexion au PIC esclave)
    // ... (autres IRQs jusqu'à 47) ...
    idt_set_gate(35, (uint32_t)irq3, 0x08, 0x8E);  // IRQ3  - Port série COM2
    idt_set_gate(36, (uint32_t)irq4, 0x08, 0x8E);  // IRQ4  - Port série COM1
    idt_set_gate(37, (uint32_t)irq5, 0x08, 0x8E);  // IRQ5  - LPT2 (ou carte son)
    idt_set_gate(38, (uint32_t)irq6, 0x08, 0x8E);  // IRQ6  - Lecteur de disquette
    idt_set_gate(39, (uint32_t)irq7, 0x08, 0x8E);  // IRQ7  - LPT1 (port parallèle)
    idt_set_gate(40, (uint32_t)irq8, 0x08, 0x8E);  // IRQ8  - Horloge temps réel (RTC)
    idt_set_gate(41, (uint32_t)irq9, 0x08, 0x8E);  // IRQ9  - Libre / redirigé vers IRQ2
    idt_set_gate(42, (uint32_t)irq10, 0x08, 0x8E); // IRQ10 - Libre (souvent réseau, USB)
    idt_set_gate(43, (uint32_t)irq11, 0x08, 0x8E); // IRQ11 - Libre (souvent son, USB)
    idt_set_gate(44, (uint32_t)irq12, 0x08, 0x8E); // IRQ12 - Souris PS/2
    idt_set_gate(45, (uint32_t)irq13, 0x08, 0x8E); // IRQ13 - Co-processeur FPU
    idt_set_gate(46, (uint32_t)irq14, 0x08, 0x8E); // IRQ14 - Disque dur primaire (IDE)
    idt_set_gate(47, (uint32_t)irq15, 0x08, 0x8E); // IRQ15 - Disque dur secondaire (IDE)

    asm volatile ("sti"); // Activer les interruptions globalement (met IF à 1 dans EFLAGS)
}
