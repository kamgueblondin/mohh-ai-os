#ifndef INITRD_H
#define INITRD_H

#include <stdint.h>

// Structure d'un header TAR (USTAR format)
// Voir: https://www.gnu.org/software/tar/manual/html_node/Standard.html
typedef struct {
    char name[100];         // Nom du fichier
    char mode[8];           // Permissions
    char uid[8];            // User ID du propriétaire
    char gid[8];            // Group ID du propriétaire
    char size[12];          // Taille en octets (octal ASCII)
    char mtime[12];         // Date de dernière modification (octal ASCII Unix timestamp)
    char chksum[8];         // Somme de contrôle du header (octal ASCII)
    char typeflag;          // Type de fichier (voir ci-dessous)
    char linkname[100];     // Nom du fichier lié (si typeflag est un lien)
    char magic[6];          // Doit être "ustar\0" pour USTAR
    char version[2];        // Version de USTAR (doit être "00")
    char uname[32];         // Nom du propriétaire (ASCII)
    char gname[32];         // Nom du groupe (ASCII)
    char devmajor[8];       // Numéro majeur du périphérique (si fichier spécial)
    char devminor[8];       // Numéro mineur du périphérique (si fichier spécial)
    char prefix[155];       // Préfixe du nom de fichier (pour les noms > 100 chars)
    char padding[12];       // Pour aligner la structure sur 512 octets si prefix est utilisé.
                            // Si prefix n'est pas utilisé, les 155+12 octets peuvent être pour le nom.
} tar_header_t;

// Valeurs pour typeflag
#define TAR_TYPEFLAG_NORMAL_FILE    '0' // ou '\0' pour compatibilité plus ancienne
#define TAR_TYPEFLAG_HARD_LINK      '1'
#define TAR_TYPEFLAG_SYMLINK        '2'
#define TAR_TYPEFLAG_CHAR_SPECIAL   '3'
#define TAR_TYPEFLAG_BLOCK_SPECIAL  '4'
#define TAR_TYPEFLAG_DIRECTORY      '5'
#define TAR_TYPEFLAG_FIFO           '6'
#define TAR_TYPEFLAG_CONTIGUOUS     '7'
// POSIX.1-2001 ajoute d'autres types, mais 'g' (global extended header) et 'x' (extended header) sont importants
// pour Pax Interchange Format. USTAR ne les gère pas directement dans ce champ.

// Initialise le système de fichiers initrd à partir de l'adresse mémoire donnée.
// `location` est l'adresse physique où l'archive TAR de l'initrd a été chargée.
void initrd_init(uint32_t location);

// Liste tous les fichiers (et répertoires) trouvés dans l'initrd.
// Nécessite une fonction print_string(const char* str, char color) et current_color extern.
void initrd_list_files();

// Lit le contenu d'un fichier depuis l'initrd.
// Retourne un pointeur vers le contenu du fichier en mémoire, ou NULL si non trouvé.
// L'appelant ne doit PAS libérer ce pointeur. Le contenu est directement dans l'initrd.
// Pour une utilisation plus sûre, on pourrait copier le contenu dans un buffer alloué.
char* initrd_read_file(const char* filename, uint32_t* file_size_out);

#endif // INITRD_H
