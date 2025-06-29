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
    // Utiliser une couleur de débogage distincte pour cette fonction
    char list_dbg_color = 0x0A; // Vert clair sur noir

    print_string("DEBUG_ILF: initrd_list_files called.\n", list_dbg_color);
    print_string("DEBUG_ILF: initrd_start_addr = ", list_dbg_color); print_hex((uint32_t)initrd_start_addr, list_dbg_color); print_string("\n", list_dbg_color);

    if (!initrd_start_addr) {
        print_string("DEBUG_ILF: Initrd non initialisé (initrd_start_addr is NULL).\n", list_dbg_color);
        return;
    }

    tar_header_t* current_header = initrd_start_addr;
    uint32_t offset = 0;
    int entry_idx = 0;

    print_string("DEBUG_ILF: Starting to iterate TAR headers...\n", list_dbg_color);

    // Itérer au plus N fois pour éviter une boucle infinie sur un initrd corrompu.
    // Et pour afficher les détails du premier header même s'il est vide.
    for (entry_idx = 0; entry_idx < 10; ++entry_idx) { // Limiter à 10 entrées pour le debug
        current_header = (tar_header_t*)((char*)initrd_start_addr + offset);

        print_string("DEBUG_ILF: Examining header at offset: ", list_dbg_color); print_hex(offset, list_dbg_color); print_string("\n", list_dbg_color);

        char name_buf[101];
        for(int k=0; k<100; ++k) name_buf[k] = current_header->name[k]; // Copier pour être sûr de la terminaison NUL
        name_buf[100] = '\0';
        print_string("DEBUG_ILF: Header Name: \"", list_dbg_color); print_string(name_buf, list_dbg_color); print_string("\"\n", list_dbg_color);

        char magic_buf[7];
        for(int k=0; k<6; ++k) magic_buf[k] = current_header->magic[k];
        magic_buf[6] = '\0';
        print_string("DEBUG_ILF: Header Magic: \"", list_dbg_color); print_string(magic_buf, list_dbg_color); print_string("\"\n", list_dbg_color);
        
        char version_buf[3];
        version_buf[0] = current_header->version[0];
        version_buf[1] = current_header->version[1];
        version_buf[2] = '\0';
        print_string("DEBUG_ILF: Header Version: \"", list_dbg_color); print_string(version_buf, list_dbg_color); print_string("\"\n", list_dbg_color);


        print_string("DEBUG_ILF: Header Typeflag: ", list_dbg_color); print_hex(current_header->typeflag, list_dbg_color); print_string("\n", list_dbg_color);

        if (current_header->name[0] == '\0') {
            print_string("DEBUG_ILF: Name[0] is NUL. Considered end of archive.\n", list_dbg_color);
            if (entry_idx == 0) {
                 print_string("DEBUG_ILF: First header (offset 0) indicates empty archive.\n", list_dbg_color);
            }
            // On peut vérifier si le reste du bloc est aussi NUL
            int all_zeros = 1;
            for(int i = 0; i < 512; ++i) {
                if (((char*)current_header)[i] != 0) {
                    all_zeros = 0;
                    break;
                }
            }
            if(all_zeros) {
                print_string("DEBUG_ILF: Confirmed: This header block is all zeros.\n", list_dbg_color);
            } else {
                print_string("DEBUG_ILF: Warning: Name[0] is NUL, but rest of block is not all zeros.\n", list_dbg_color);
            }
            break; 
        }

        // Vérification du magic "ustar" (plus permissif pour GNU tar)
        int is_valid_ustar = (current_header->magic[0] == 'u' && current_header->magic[1] == 's' &&
                              current_header->magic[2] == 't' && current_header->magic[3] == 'a' &&
                              current_header->magic[4] == 'r');
        if (!is_valid_ustar) {
            print_string("DEBUG_ILF: Invalid TAR magic (does not start with 'ustar').\n", list_dbg_color);
            break;
        }
        if (current_header->magic[5] != '\0' && !(current_header->magic[5] == ' ' && current_header->version[0] == ' ' && current_header->version[1] == ' ')) {
             print_string("DEBUG_ILF: Invalid TAR magic[5] or version field for USTAR.\n", list_dbg_color);
             break;
        }


        print_string("DEBUG_ILF: Fichier listé: ", list_dbg_color);
        print_string(name_buf, list_dbg_color); // Utiliser name_buf qui est NUL-terminé

        uint32_t size = octal_to_uint(current_header->size, 11);
        print_string(" | Taille (dec): ", list_dbg_color); print_hex(size, list_dbg_color); // print_hex pour la taille

        print_string(" (type: ", list_dbg_color);
        char type_char_buf[2] = {current_header->typeflag, '\0'};
        print_string(type_char_buf, list_dbg_color);
        print_string(")\n", list_dbg_color);

        offset += 512; // Avancer d'un bloc header
        if (size > 0) {
            offset += (size + 511) & ~511; // Arrondir la taille du fichier au prochain multiple de 512
        }

        if (offset > 10 * 1024 * 1024) { // Limite arbitraire
            print_string("DEBUG_ILF: Dépassement de la taille max de l'initrd lors du listage.\n", list_dbg_color);
            break;
        }
        if (offset == 0 && size == 0 && entry_idx > 0) { // Eviter boucle infinie si offset n'avance pas
             print_string("DEBUG_ILF: Offset did not advance. Breaking to prevent infinite loop.\n", list_dbg_color);
             break;
        }
    }
    print_string("DEBUG_ILF: Finished listing files (or reached debug limit).\n", list_dbg_color);
}

char* initrd_read_file(const char* filename, uint32_t* file_size_out) {
    // Utiliser une couleur de débogage distincte
    char dbg_color = 0x0D; // Magenta sur noir

    print_string("DEBUG_IRF: initrd_read_file called for: ", dbg_color); print_string(filename ? filename : "NULL", dbg_color); print_string("\n", dbg_color);
    print_string("DEBUG_IRF: initrd_start_addr = ", dbg_color); print_hex((uint32_t)initrd_start_addr, dbg_color); print_string("\n", dbg_color);

    if (!initrd_start_addr || !filename) {
        print_string("DEBUG_IRF: Early exit - initrd_start_addr or filename is NULL.\n", dbg_color);
        if(file_size_out) *file_size_out = 0;
        return NULL;
    }

    tar_header_t* current_header = initrd_start_addr;
    uint32_t offset = 0;
    int file_idx = 0;

    while (1) {
        current_header = (tar_header_t*)((char*)initrd_start_addr + offset);

        print_string("DEBUG_IRF: Entry ", dbg_color); print_hex(file_idx, dbg_color); print_string(" at offset ", dbg_color); print_hex(offset, dbg_color); print_string("\n", dbg_color);

        // Afficher les premiers caractères du nom (attention, name n'est pas forcément terminé par NUL dans les 100 chars)
        char name_buf[101];
        for(int k=0; k<100; ++k) name_buf[k] = current_header->name[k];
        name_buf[100] = '\0';
        print_string("DEBUG_IRF: Header Name: \"", dbg_color); print_string(name_buf, dbg_color); print_string("\"\n", dbg_color);

        // Afficher le champ magic
        char magic_buf[7];
        for(int k=0; k<6; ++k) magic_buf[k] = current_header->magic[k];
        magic_buf[6] = '\0';
        print_string("DEBUG_IRF: Header Magic: \"", dbg_color); print_string(magic_buf, dbg_color); print_string("\"\n", dbg_color);
        print_string("DEBUG_IRF: Header Typeflag: ", dbg_color); print_hex(current_header->typeflag, dbg_color); print_string("\n", dbg_color);


        if (current_header->name[0] == '\0') {
            print_string("DEBUG_IRF: End of archive (name[0] is NUL).\n", dbg_color);
            break;
        }
        // Vérification plus robuste du magic "ustar" et de la version "00"
        if (current_header->magic[0] != 'u' || current_header->magic[1] != 's' ||
            current_header->magic[2] != 't' || current_header->magic[3] != 'a' ||
            current_header->magic[4] != 'r' || current_header->magic[5] != '\0' /* POSIX USTAR magic */ ) {
            // Gnu tar met "ustar  \0" (notez les deux espaces) pour version " \0"
            // Si version est "00", magic est "ustar\0"
            // On va être un peu plus permissif sur magic[5] si version est "00"
            if (!(current_header->magic[5] == ' ' && current_header->version[0] == ' ' && current_header->version[1] == ' ') &&
                !(current_header->magic[5] == '\0' && current_header->version[0] == '0' && current_header->version[1] == '0')) {
                 print_string("DEBUG_IRF: Invalid TAR magic or version. Magic: '", dbg_color);
                 print_string(magic_buf, dbg_color); 
                 print_string("', Version: '", dbg_color); 
                 char version_buf[3]; version_buf[0]=current_header->version[0]; version_buf[1]=current_header->version[1]; version_buf[2]='\0';
                 print_string(version_buf, dbg_color);
                 print_string("'. Breaking loop.\n", dbg_color);
                 break;
            }
        }
        
        int cmp_result = strcmp(current_header->name, filename);
        print_string("DEBUG_IRF: strcmp(\"", dbg_color); print_string(current_header->name, dbg_color); print_string("\", \"", dbg_color); print_string(filename, dbg_color); print_string("\") = ", dbg_color); print_hex(cmp_result, dbg_color); print_string("\n", dbg_color);

        if (cmp_result == 0) {
            print_string("DEBUG_IRF: File \"", dbg_color); print_string(filename, dbg_color); print_string("\" found by strcmp!\n", dbg_color);
            if (current_header->typeflag == TAR_TYPEFLAG_NORMAL_FILE || current_header->typeflag == '\0' || current_header->typeflag == '0') {
                print_string("DEBUG_IRF: Typeflag is normal file ('", dbg_color); print_hex(current_header->typeflag, dbg_color); print_string("'). Reading file.\n", dbg_color);
                uint32_t size = octal_to_uint(current_header->size, 11);
                if (file_size_out) {
                    *file_size_out = size;
                }
                print_string("DEBUG_IRF: File size: ", dbg_color); print_hex(size, dbg_color); print_string("\n", dbg_color);
                return (char*)current_header + 512;
            } else {
                print_string("DEBUG_IRF: File found, but typeflag is not normal: ", dbg_color); print_hex(current_header->typeflag, dbg_color); print_string(". Returning NULL.\n", dbg_color);
                if (file_size_out) *file_size_out = 0;
                return NULL;
            }
        }

        uint32_t size = octal_to_uint(current_header->size, 11);
        offset += 512; // Header block
        if (size > 0) {
            offset += (size + 511) & ~511; // Data blocks, rounded up to 512 boundary
        }
        
        file_idx++;
        if (offset > 10 * 1024 * 1024) { // Safety break
            print_string("DEBUG_IRF: Offset exceeded safety limit. Breaking.\n", dbg_color);
            break; 
        }
    }

    print_string("DEBUG_IRF: File not found or error in archive. Filename: ", dbg_color); print_string(filename, dbg_color); print_string("\n", dbg_color);
    if (file_size_out) *file_size_out = 0;
    return NULL; // Fichier non trouvé
}

// strcmp est déclaré extern et défini dans kernel.c (ou un fichier utilitaire).
// La garde KERNEL_STRCMP_DEFINED est dans kernel.c.
// La garde KERNEL_STRCMP_DEFINED est dans kernel.c.
