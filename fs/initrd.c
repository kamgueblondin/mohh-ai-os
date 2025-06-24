#include "initrd.h"
#include <stddef.h> // Pour NULL et size_t (bien que size_t ne soit pas utilisé ici)

// Fonctions externes (doivent être fournies par le noyau, par exemple kernel.c)
extern void print_string(const char* str, char color);
extern char current_color; // Couleur actuelle du texte, définie dans kernel.c
extern int strcmp(const char *s1, const char *s2); // Fonction de comparaison de chaînes
// extern void* memcpy(void *dest, const void *src, size_t n); // Si on copiait les fichiers

static tar_header_t* initrd_start_addr = NULL;
// static uint32_t initrd_file_count = 0; // Pourrait être calculé dans initrd_init

// Convertit une chaîne octale (terminée par NUL ou de taille fixe) en entier.
// `size` est le nombre maximum de caractères à lire de `str`.
static uint32_t octal_to_uint(char *str, unsigned int size) {
    uint32_t n = 0;
    char *c = str;
    while (size-- > 0 && *c >= '0' && *c <= '7') {
        n = n * 8 + (*c - '0');
        c++;
    }
    return n;
}

void initrd_init(uint32_t location) {
    initrd_start_addr = (tar_header_t*)location;
    // On pourrait vérifier ici le magic "ustar" du premier header pour valider.
    // print_string("Initrd initialisé à l'adresse: ", current_color);
    // char addr_str[10];
    // itoa(location, addr_str, 16); // Nécessiterait itoa
    // print_string(addr_str, current_color);
    // print_string("\n", current_color);
}

void initrd_list_files() {
    if (!initrd_start_addr) {
        print_string("Initrd non initialisé.\n", current_color);
        return;
    }

    tar_header_t* current_header = initrd_start_addr;
    uint32_t offset = 0;

    while (1) {
        current_header = (tar_header_t*)((char*)initrd_start_addr + offset);

        // La fin d'une archive TAR est marquée par au moins deux blocs de 512 octets remplis de zéros.
        // Un simple test sur le premier caractère du nom est souvent suffisant pour des archives simples.
        if (current_header->name[0] == '\0') {
            // On pourrait vérifier si c'est bien un bloc de zéros pour être plus robuste.
            break;
        }

        // Vérification du magic "ustar" pour s'assurer que c'est un header USTAR.
        // strcmp nécessite que magic se termine par \0, ce qui est le cas si version[0] est \0.
        // Le champ magic est de 6 octets, "ustar" suivi d'un NUL.
        if (current_header->magic[0] != 'u' || current_header->magic[1] != 's' || \
            current_header->magic[2] != 't' || current_header->magic[3] != 'a' || \
            current_header->magic[4] != 'r' || current_header->magic[5] != '\0') {
            print_string("Header TAR invalide ou fin non standard de l'archive.\n", current_color);
            break;
        }

        print_string("Fichier: ", current_color);
        print_string(current_header->name, current_color);

        uint32_t size = octal_to_uint(current_header->size, 11); // size est sur 11 char + 1 NUL implicite dans la struct
        // print_string(" | Taille (oct): ", current_color);
        // print_string(current_header->size, current_color); // Affiche la taille en octal
        // print_string(" | Taille (dec): ", current_color);
        // char size_dec_str[12];
        // itoa(size, size_dec_str, 10); // Nécessite itoa
        // print_string(size_dec_str, current_color);

        print_string(" (type: ", current_color);
        char type_str[2] = {current_header->typeflag, '\0'};
        print_string(type_str, current_color);
        print_string(")\n", current_color);

        // Calculer l'offset pour le prochain header.
        // Le contenu du fichier suit ce header. La taille totale du header est 512 octets.
        // Le contenu du fichier est `size` octets.
        // L'enregistrement total (header + contenu) est paddé à 512 octets.
        offset += 512; // Avancer d'un bloc header
        if (size > 0) {
            offset += (size + 511) & ~511; // Arrondir la taille du fichier au prochain multiple de 512
        }

        // Sécurité pour éviter boucle infinie si l'archive est corrompue
        if (offset > 10 * 1024 * 1024) { // Limite arbitraire (e.g., 10MB pour l'initrd)
            print_string("Dépassement de la taille max de l'initrd lors du listage.\n", current_color);
            break;
        }
    }
}

char* initrd_read_file(const char* filename, uint32_t* file_size_out) {
    if (!initrd_start_addr || !filename) {
        if(file_size_out) *file_size_out = 0;
        return NULL;
    }
    if (!strcmp) { // Si strcmp n'est pas disponible
         if(file_size_out) *file_size_out = 0;
        return NULL;
    }

    tar_header_t* current_header = initrd_start_addr;
    uint32_t offset = 0;

    while (1) {
        current_header = (tar_header_t*)((char*)initrd_start_addr + offset);

        if (current_header->name[0] == '\0') break;
        if (current_header->magic[0] != 'u' || current_header->magic[5] != '\0') break; // Simplifié

        if (strcmp(current_header->name, filename) == 0) {
            // Fichier trouvé
            if (current_header->typeflag == TAR_TYPEFLAG_NORMAL_FILE || current_header->typeflag == '\0') {
                uint32_t size = octal_to_uint(current_header->size, 11);
                if (file_size_out) {
                    *file_size_out = size;
                }
                // Le contenu du fichier est juste après le header (512 octets)
                return (char*)current_header + 512;
            } else { // Ce n'est pas un fichier normal
                if (file_size_out) *file_size_out = 0;
                return NULL;
            }
        }

        uint32_t size = octal_to_uint(current_header->size, 11);
        offset += 512;
        if (size > 0) {
            offset += (size + 511) & ~511;
        }
        if (offset > 10 * 1024 * 1024) break; // Sécurité
    }

    if (file_size_out) *file_size_out = 0;
    return NULL; // Fichier non trouvé
}

// Implémentation basique de strcmp si non fournie par le noyau
// Attention: cette version est très basique.
#ifndef KERNEL_STRCMP_DEFINED
int strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}
#endif
