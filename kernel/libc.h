#ifndef LIBC_H
#define LIBC_H

#include <stddef.h> // For size_t

int strcmp(const char* s1, const char* s2);
void* memcpy(void* dest, const void* src, size_t n);
void* memset(void* dest, int val, size_t count);
char* itoa(uint32_t value, char* str, int base);

#endif // LIBC_H
