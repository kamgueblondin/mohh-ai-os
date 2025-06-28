#ifndef LIBC_H
#define LIBC_H

#include <stddef.h> // For size_t
#include <stdint.h> // Pour uint32_t et autres types entiers

int strcmp(const char* s1, const char* s2);
void* memcpy(void* dest, const void* src, size_t n);
void* memset(void* dest, int val, size_t count);
char* itoa(uint32_t value, char* str, int base);
void print_hex(uint32_t n, char color);
void print_string(const char* str, char color); // Already in kernel.c, good to have extern decl

#endif // LIBC_H
