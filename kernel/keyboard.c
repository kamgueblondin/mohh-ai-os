#include "keyboard.h"
#include "interrupts.h" // For inb/outb
#include "task/task.h"  // For task_t, current_task, schedule, TASK_READY, TASK_WAITING_FOR_KEYBOARD
#include "libc.h"       // For memcpy
#include <stdint.h>
#include <stddef.h>     // For NULL

// Tâche courante (définie dans task.c, nécessaire pour keyboard_prepare_for_gets)
extern volatile task_t* current_task;

// Utilisation des variables et fonctions VGA globales (déclarées extern dans syscall.c, définies dans kernel.c/vga.c)
extern int vga_x;
extern int vga_y;
extern char current_color;
extern void print_char(char c, int x, int y, char color);
// extern void scroll_if_needed(); // Si une fonction de scroll globale existe

// Buffer interne au noyau pour SYS_GETS
#define KBD_INTERNAL_BUFFER_SIZE 256
static char kbd_internal_buffer[KBD_INTERNAL_BUFFER_SIZE];
static uint32_t kbd_internal_buffer_idx = 0;

// Informations sur la tâche et le buffer utilisateur pour SYS_GETS
static char* volatile user_target_buffer = NULL;
static volatile uint32_t user_target_buffer_max_size = 0;
static volatile task_t* task_waiting_for_input = NULL;
static volatile uint32_t num_chars_read_for_gets = 0;


// Table de correspondance Scancode -> ASCII (US QWERTY)
const char scancode_map[] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8', /* 9 */
  '9', '0', '-', '=', '\b', /* Backspace */
  '\t',         /* Tab */
  'q', 'w', 'e', 'r',   /* 19 */
  't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', /* Enter key */
    0,          /* 29   - Control */
  'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', /* 39 */
 '\'', '`',   0,        /* Left shift */
 '\\', 'z', 'x', 'c', 'v', 'b', 'n',            /* 49 */
  'm', ',', '.', '/',   0,              /* Right shift */
  '*',
    0,  /* Alt */
  ' ',  /* Space bar */
    0,  /* Caps lock */
    0,  /* 59 - F1 key ... > */
    0,   0,   0,   0,   0,   0,   0,   0,
    0,  /* < ... F10 */
    0,  /* 69 - Num lock*/
    0,  /* Scroll Lock */
    0,  /* Home key */ // 71
    0,  /* Up Arrow */ // 72
    0,  /* Page Up */  // 73
  '-',
    0,  /* Left Arrow */ // 75
    0,  // 76 - Center Key (Keypad 5)
    0,  /* Right Arrow */ // 77
  '+',
    0,  /* 79 - End key*/ // 79
    0,  /* Down Arrow */ // 80
    0,  /* Page Down */ // 81
    0,  /* Insert Key */ // 82
    0,  /* Delete Key */ // 83
    0,   0,   0,
    0,  /* F11 Key */
    0,  /* F12 Key */
    0,  /* All other keys are undefined */
};

// Appelée par le syscall SYS_GETS pour initialiser la lecture.
void keyboard_prepare_for_gets(char* user_buf, uint32_t user_buf_size) {
    // Idéalement, utiliser des mécanismes de synchronisation (cli/sti) si nécessaire.
    // Pour l'instant, on suppose que l'assignation est atomique ou que les interruptions ne causeront pas de problèmes.
    if (task_waiting_for_input != NULL) {
        // Gérer le cas où une autre tâche attend déjà.
        // Pour l'instant, on pourrait ignorer ou faire échouer le nouvel appel.
        // Ici, on va simplement laisser la tâche appelante se bloquer,
        // mais elle ne sera pas réveillée si elle n'est pas task_waiting_for_input.
        // Le handler syscall devrait vérifier cela et retourner une erreur.
        return;
    }
    user_target_buffer = user_buf;
    user_target_buffer_max_size = user_buf_size;
    kbd_internal_buffer_idx = 0;
    kbd_internal_buffer[0] = '\0';
    num_chars_read_for_gets = 0;
    task_waiting_for_input = (task_t*)current_task; // current_task est la tâche qui appelle SYS_GETS
}

// Appelée par keyboard_handler_main lorsqu'un caractère est tapé et qu'une tâche attend.
void keyboard_process_char_for_gets(char ascii) {
    if (!task_waiting_for_input || !user_target_buffer) {
        return; // Personne n'attend ou le buffer n'est pas prêt
    }

    if (ascii == '\n') {
        kbd_internal_buffer[kbd_internal_buffer_idx] = '\0'; // Terminer le buffer interne

        // Copier vers le buffer utilisateur, en respectant la taille max
        uint32_t copy_len = kbd_internal_buffer_idx;
        if (copy_len >= user_target_buffer_max_size) {
            copy_len = user_target_buffer_max_size - 1; // Laisser place pour le \0
        }
        memcpy(user_target_buffer, kbd_internal_buffer, copy_len);
        user_target_buffer[copy_len] = '\0'; // Terminer le buffer utilisateur
        num_chars_read_for_gets = copy_len;

        print_char(ascii, vga_x, vga_y, current_color); // Écho du retour à la ligne

        // Réveiller la tâche et stocker la valeur de retour
        if (task_waiting_for_input) { // Vérification de sécurité
            task_waiting_for_input->syscall_retval = num_chars_read_for_gets;
            task_waiting_for_input->state = TASK_READY;
        }

        // Réinitialiser pour le prochain appel à gets
        task_waiting_for_input = NULL;
        user_target_buffer = NULL;
        kbd_internal_buffer_idx = 0;
        // num_chars_read_for_gets sera réinitialisé implicitement par la logique de la prochaine lecture
        // ou n'est plus pertinent une fois la tâche réveillée et la valeur de retour stockée.

    } else if (ascii == '\b') { // Backspace
        if (kbd_internal_buffer_idx > 0) {
            kbd_internal_buffer_idx--;
            print_char(ascii, vga_x, vga_y, current_color); // Écho du backspace
        }
    } else if (ascii >= 32 && ascii <= 126) { // Caractères ASCII imprimables
        // Vérifier si on a de la place dans le buffer interne ET le buffer utilisateur
        if (kbd_internal_buffer_idx < KBD_INTERNAL_BUFFER_SIZE - 1 &&
            kbd_internal_buffer_idx < user_target_buffer_max_size - 1) {
            kbd_internal_buffer[kbd_internal_buffer_idx++] = ascii;
            print_char(ascii, vga_x, vga_y, current_color); // Écho du caractère
        }
    }
    // Les autres caractères (non imprimables, hors LF/BS) sont ignorés pour l'instant
}

// Handler principal d'interruption clavier
void keyboard_handler_main() {
    unsigned char scancode = inb(0x60);

    if (scancode < 0x80) { // On ne gère que les pressions de touche (pas les relâchements)
        if (scancode < sizeof(scancode_map) && scancode_map[scancode] != 0) {
            char c = scancode_map[scancode];
            if (task_waiting_for_input != NULL) {
                keyboard_process_char_for_gets(c);
            } else {
                // Si aucune tâche n'attend via SYS_GETS, on pourrait :
                // 1. Ignorer la touche
                // 2. L'envoyer à une console par défaut (si elle existe)
                // 3. Pour l'instant, on l'ignore car le shell sera toujours en attente de SYS_GETS.
            }
        }
    }
    // L'EOI (End Of Interrupt) est géré par le stub d'interruption commun ou l'ISR de plus haut niveau.
}

// Fonction pour que le syscall handler récupère le nombre de caractères lus.
uint32_t keyboard_get_chars_read_count() {
    return num_chars_read_for_gets;
}

// init_vga_kb, print_string_kb, clear_screen_kb ne sont plus nécessaires ici
// car on utilise les fonctions VGA globales.
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
