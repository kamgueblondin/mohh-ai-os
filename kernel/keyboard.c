#include "keyboard.h"
#include "interrupts.h" // Pour inb/outb
#include "task/task.h"  // Pour task_t, current_task, schedule, TASK_READY, TASK_WAITING_FOR_KEYBOARD
#include "libc.h"       // Pour memcpy
#include <stdint.h>
#include <stddef.h>     // Pour NULL

// Tâche courante (définie dans task.c, nécessaire pour keyboard_prepare_for_gets)
extern volatile task_t* current_task;

// Utilisation des variables et fonctions VGA globales (déclarées extern dans syscall.c, définies dans kernel.c/vga.c)
extern int vga_x;
extern int vga_y;
extern char current_color;
extern void print_char(char c, int x, int y, char color);
// extern void scroll_if_needed(); // Si une fonction de défilement globale existe

// Tampon interne au noyau pour SYS_GETS
#define KBD_INTERNAL_BUFFER_SIZE 256
static char kbd_internal_buffer[KBD_INTERNAL_BUFFER_SIZE]; // Tampon pour accumuler les caractères
static uint32_t kbd_internal_buffer_idx = 0; // Index courant dans le tampon interne

// Informations sur la tâche et le tampon utilisateur pour SYS_GETS
static char* volatile user_target_buffer = NULL; // Pointeur vers le tampon de l'espace utilisateur
static volatile uint32_t user_target_buffer_max_size = 0; // Taille maximale du tampon utilisateur
static volatile task_t* task_waiting_for_input = NULL; // Tâche en attente d'une entrée clavier
static volatile uint32_t num_chars_read_for_gets = 0; // Nombre de caractères lus pour le SYS_GETS courant


// Table de correspondance Scancode -> ASCII (US QWERTY)
// Un scancode est un numéro envoyé par le clavier lorsqu'une touche est pressée ou relâchée.
const char scancode_map[] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8', /* Scancode 9 */
  '9', '0', '-', '=', '\b', /* Backspace (Retour arrière) */
  '\t',         /* Tab (Tabulation) */
  'q', 'w', 'e', 'r',   /* Scancode 19 */
  't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', /* Enter key (Touche Entrée) */
    0,          /* 29   - Control (Ctrl) */
  'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', /* Scancode 39 */
 '\'', '`',   0,        /* Left shift (Majuscule gauche) */
 '\\', 'z', 'x', 'c', 'v', 'b', 'n',            /* Scancode 49 */
  'm', ',', '.', '/',   0,              /* Right shift (Majuscule droite) */
  '*', // Keypad * (Pave num *)
    0,  /* Alt */
  ' ',  /* Space bar (Barre d'espace) */
    0,  /* Caps lock (Verr Maj) */
    0,  /* 59 - F1 key ... > (Touche F1) */
    0,   0,   0,   0,   0,   0,   0,   0, // F2-F9
    0,  /* < ... F10 (Touche F10) */
    0,  /* 69 - Num lock (Verr Num) */
    0,  /* Scroll Lock (Arrêt défil) */
    0,  /* Home key (Début) */ // Scancode 71
    0,  /* Up Arrow (Flèche haut) */ // Scancode 72
    0,  /* Page Up (Page préc.) */  // Scancode 73
  '-', // Keypad - (Pave num -)
    0,  /* Left Arrow (Flèche gauche) */ // Scancode 75
    0,  // 76 - Center Key (Touche centrale du pavé numérique, souvent 5)
    0,  /* Right Arrow (Flèche droite) */ // Scancode 77
  '+', // Keypad + (Pave num +)
    0,  /* 79 - End key (Fin) */ // Scancode 79
    0,  /* Down Arrow (Flèche bas) */ // Scancode 80
    0,  /* Page Down (Page suiv.) */ // Scancode 81
    0,  /* Insert Key (Inser) */ // Scancode 82
    0,  /* Delete Key (Suppr) */ // Scancode 83
    0,   0,   0, // Scancodes non utilisés ou spécifiques
    0,  /* F11 Key (Touche F11) */
    0,  /* F12 Key (Touche F12) */
    0,  /* Toutes les autres touches sont non définies (ou non gérées) */
};

// Appelée par l'appel système SYS_GETS pour initialiser la lecture depuis le clavier.
void keyboard_prepare_for_gets(char* user_buf, uint32_t user_buf_size) {
    // Idéalement, utiliser des mécanismes de synchronisation (cli/sti ou sémaphores) si nécessaire.
    // Pour l'instant, on suppose que l'assignation est atomique ou que les interruptions
    // pendant ces quelques instructions ne causeront pas de problèmes critiques.
    if (task_waiting_for_input != NULL) {
        // Gérer le cas où une autre tâche attend déjà une entrée.
        // Actuellement, le gestionnaire d'appel système devrait probablement vérifier cela en amont
        // et retourner une erreur (par exemple, EBUSY) si une lecture est déjà en cours pour une autre tâche.
        // Si on arrive ici, cela pourrait signifier une logique incorrecte ailleurs.
        // Pour l'instant, on ne fait rien, la tâche appelante restera bloquée mais ne sera pas
        // la `task_waiting_for_input` officielle.
        return; // Ou indiquer une erreur à l'appelant.
    }
    user_target_buffer = user_buf; // Tampon de destination dans l'espace utilisateur
    user_target_buffer_max_size = user_buf_size; // Taille max de ce tampon
    kbd_internal_buffer_idx = 0; // Réinitialiser l'index du tampon interne
    kbd_internal_buffer[0] = '\0'; // Commencer avec un tampon interne vide
    num_chars_read_for_gets = 0; // Aucun caractère lu pour cette session gets
    task_waiting_for_input = (task_t*)current_task; // La tâche courante est celle qui attend l'entrée
}

// Appelée par keyboard_handler_main lorsqu'un caractère est tapé et qu'une tâche attend une entrée via SYS_GETS.
void keyboard_process_char_for_gets(char ascii) {
    if (!task_waiting_for_input || !user_target_buffer) {
        return; // Personne n'attend ou le tampon utilisateur n'est pas configuré.
    }

    if (ascii == '\n') { // Si la touche Entrée est pressée
        kbd_internal_buffer[kbd_internal_buffer_idx] = '\0'; // Terminer la chaîne dans le tampon interne

        // Copier le contenu du tampon interne vers le tampon utilisateur, en respectant la taille maximale.
        uint32_t copy_len = kbd_internal_buffer_idx;
        if (copy_len >= user_target_buffer_max_size) { // Si le contenu est plus grand que le tampon utilisateur
            copy_len = user_target_buffer_max_size - 1; // Laisser de la place pour le caractère nul de fin
        }
        memcpy(user_target_buffer, kbd_internal_buffer, copy_len); // Copier les données
        user_target_buffer[copy_len] = '\0'; // Assurer la terminaison nulle du tampon utilisateur
        num_chars_read_for_gets = copy_len; // Stocker le nombre de caractères copiés

        print_char(ascii, vga_x, vga_y, current_color); // Afficher (écho) le retour à la ligne à l'écran

        // Réveiller la tâche en attente et stocker le nombre de caractères lus comme valeur de retour de l'appel système.
        if (task_waiting_for_input) { // Vérification de sécurité additionnelle
            task_waiting_for_input->syscall_retval = num_chars_read_for_gets; // Valeur de retour pour SYS_GETS
            task_waiting_for_input->state = TASK_READY; // Marquer la tâche comme prête à être exécutée
            // Le scheduler la reprendra éventuellement.
        }

        // Réinitialiser les variables d'état pour le prochain appel à SYS_GETS.
        task_waiting_for_input = NULL;
        user_target_buffer = NULL;
        kbd_internal_buffer_idx = 0;
        // num_chars_read_for_gets sera réinitialisé par keyboard_prepare_for_gets lors du prochain appel.

    } else if (ascii == '\b') { // Si la touche Backspace (retour arrière) est pressée
        if (kbd_internal_buffer_idx > 0) { // S'il y a des caractères dans le tampon interne
            kbd_internal_buffer_idx--; // Reculer l'index (effacer le dernier caractère)
            print_char(ascii, vga_x, vga_y, current_color); // Afficher (écho) le backspace à l'écran
        }
    } else if (ascii >= 32 && ascii <= 126) { // Si c'est un caractère ASCII imprimable
        // Vérifier s'il y a de la place dans le tampon interne ET dans le tampon utilisateur (moins 1 pour le nul).
        if (kbd_internal_buffer_idx < KBD_INTERNAL_BUFFER_SIZE - 1 &&
            kbd_internal_buffer_idx < user_target_buffer_max_size - 1) {
            kbd_internal_buffer[kbd_internal_buffer_idx++] = ascii; // Ajouter le caractère au tampon interne
            print_char(ascii, vga_x, vga_y, current_color); // Afficher (écho) le caractère à l'écran
        }
        // Si les tampons sont pleins, la frappe est ignorée pour ce caractère.
    }
    // Les autres caractères (non imprimables, hors Entrée et Backspace) sont ignorés pour l'instant.
}

// Gestionnaire principal d'interruption clavier (appelé depuis le stub d'interruption IRQ1).
void keyboard_handler_main() {
    unsigned char scancode = inb(0x60); // Lire le scancode depuis le port de données du clavier

    // On ne gère que les événements de "touche pressée" (scancodes < 0x80).
    // Les événements de "touche relâchée" ont le bit 7 positionné (scancode + 0x80).
    if (scancode < 0x80) {
        // Vérifier si le scancode est dans les limites de notre table de mappage et s'il a un caractère ASCII associé.
        if (scancode < sizeof(scancode_map) && scancode_map[scancode] != 0) {
            char c = scancode_map[scancode]; // Obtenir le caractère ASCII
            if (task_waiting_for_input != NULL) { // Si une tâche attend une entrée (via SYS_GETS)
                keyboard_process_char_for_gets(c); // Traiter le caractère pour cette tâche
            } else {
                // Si aucune tâche n'attend via SYS_GETS, on pourrait :
                // 1. Ignorer la touche (comportement actuel implicite).
                // 2. L'envoyer à une console/terminal par défaut (si une telle abstraction existe).
                // 3. La mettre dans un tampon global du noyau pour une lecture ultérieure par un autre mécanisme.
                // Pour l'instant, on suppose que si une entrée est tapée, c'est généralement pour le shell
                // qui sera en attente via SYS_GETS.
            }
        }
    }
    // L'EOI (End Of Interrupt - Fin d'interruption) est envoyé au PIC par le gestionnaire d'IRQ de plus haut niveau
    // (par exemple, dans irq_handler_c ou le stub assembleur qui l'appelle), pas directement ici.
}

// Fonction pour que le gestionnaire d'appel système récupère le nombre de caractères lus par le dernier SYS_GETS.
// Cette fonction est appelée après que la tâche a été réveillée et que SYS_GETS est sur le point de retourner.
uint32_t keyboard_get_chars_read_count() {
    // Cette valeur est déjà stockée dans task_waiting_for_input->syscall_retval
    // au moment où la tâche est réveillée. Le gestionnaire syscall pourrait la lire directement
    // depuis la structure de la tâche. Cependant, avoir cette fonction peut être utile
    // si la valeur doit être récupérée avant que le scheduler ne change de tâche.
    // Pour l'instant, elle retourne la variable globale, qui est cohérente avec ce qui a été mis
    // dans syscall_retval.
    return num_chars_read_for_gets;
}

// Les fonctions init_vga_kb, print_string_kb, clear_screen_kb ne sont plus nécessaires ici
// car nous utilisons les fonctions VGA globales définies dans kernel.c (ou vga.c).
// La fonction init_vga_kb(int x, int y, char color) pourrait être retirée ou adaptée
// si la gestion du curseur VGA doit être synchronisée spécifiquement par keyboard.c
// au démarrage, mais généralement kernel.c/vga.c s'en occupera.
// Pour l'instant, je la laisse commentée ou la supprime.
/*
void init_vga_kb(int x_pos, int y_pos, char color_val) {
    // Cette fonction est maintenant redondante si kernel.c initialise vga_x, vga_y, current_color
    // et que print_char les utilise.
}
*/
