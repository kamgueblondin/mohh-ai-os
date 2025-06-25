#ifndef ELF_H
#define ELF_H

#include <stdint.h>

// Basic ELF types
typedef uint32_t Elf32_Addr;  // Unsigned program address
typedef uint16_t Elf32_Half;  // Unsigned medium integer
typedef uint32_t Elf32_Off;   // Unsigned file offset
typedef int32_t  Elf32_Sword; // Signed integer
typedef uint32_t Elf32_Word;  // Unsigned integer

// ELF Header
#define EI_NIDENT 16

typedef struct {
    unsigned char e_ident[EI_NIDENT]; // ELF identification
    Elf32_Half    e_type;             // Object file type
    Elf32_Half    e_machine;          // Machine type
    Elf32_Word    e_version;          // Object file version
    Elf32_Addr    e_entry;            // Entry point address
    Elf32_Off     e_phoff;            // Program header offset
    Elf32_Off     e_shoff;            // Section header offset
    Elf32_Word    e_flags;            // Processor-specific flags
    Elf32_Half    e_ehsize;           // ELF header size
    Elf32_Half    e_phentsize;        // Size of program header entry
    Elf32_Half    e_phnum;            // Number of program header entries
    Elf32_Half    e_shentsize;        // Size of section header entry
    Elf32_Half    e_shnum;            // Number of section header entries
    Elf32_Half    e_shstrndx;         // Section name string table index
} Elf32_Ehdr;

// e_ident[] indices
#define EI_MAG0       0  // File identification
#define EI_MAG1       1  // File identification
#define EI_MAG2       2  // File identification
#define EI_MAG3       3  // File identification
#define EI_CLASS      4  // File class
#define EI_DATA       5  // Data encoding
#define EI_VERSION    6  // File version
#define EI_OSABI      7  // OS/ABI identification
#define EI_ABIVERSION 8  // ABI version
#define EI_PAD        9  // Start of padding bytes

// e_ident[EI_MAG0..EI_MAG3]
#define ELFMAG0 0x7f // ELF magic number
#define ELFMAG1 'E'
#define ELFMAG2 'L'
#define ELFMAG3 'F'

// e_ident[EI_CLASS]
#define ELFCLASSNONE 0 // Invalid class
#define ELFCLASS32   1 // 32-bit objects
#define ELFCLASS64   2 // 64-bit objects (not used in this project)

// e_ident[EI_DATA]
#define ELFDATANONE 0 // Invalid data encoding
#define ELFDATA2LSB 1 // 2's complement, little-endian
#define ELFDATA2MSB 2 // 2's complement, big-endian

// e_type
#define ET_NONE   0      // No file type
#define ET_REL    1      // Relocatable file
#define ET_EXEC   2      // Executable file
#define ET_DYN    3      // Shared object file
#define ET_CORE   4      // Core file

// e_machine (architecture)
#define EM_NONE  0  // No machine
#define EM_386   3  // Intel 80386

// Program Header
typedef struct {
    Elf32_Word p_type;   // Type of segment
    Elf32_Off  p_offset; // Offset in file
    Elf32_Addr p_vaddr;  // Virtual address in memory
    Elf32_Addr p_paddr;  // Physical address (if relevant)
    Elf32_Word p_filesz; // Size of segment in file
    Elf32_Word p_memsz;  // Size of segment in memory
    Elf32_Word p_flags;  // Segment flags
    Elf32_Word p_align;  // Alignment of segment
} Elf32_Phdr;

// p_type (segment type)
#define PT_NULL    0          // Unused segment
#define PT_LOAD    1          // Loadable segment
#define PT_DYNAMIC 2          // Dynamic linking information
#define PT_INTERP  3          // Interpreter pathname
#define PT_NOTE    4          // Auxiliary information
#define PT_SHLIB   5          // Reserved
#define PT_PHDR    6          // Program header table

// p_flags (segment flags)
#define PF_X 1 // Execute
#define PF_W 2 // Write
#define PF_R 4 // Read

// Function to load an ELF executable
// Returns the entry point of the program, or 0 on error
uint32_t elf_load(uint8_t* elf_data);

#endif // ELF_H
