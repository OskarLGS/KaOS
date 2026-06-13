#ifndef UTIL_H
#define UTIL_H

#include <stdint.h>

void memory_copy(char* source, char* dest, int n_bytes);
void memory_set(uint8_t* dest, uint8_t val, uint32_t len);
void int_to_ascii(int n, char str[]);
void reverse(char s[]);
int strlen(char s[]);
void backspace(char s[]);
void append(char s[], char n);
int strcmp(char s1[], char s2[]);
void strcpy(char* dest, char* src);
int strncmp(char s1[], char s2[], int n);
int ascii_to_int(char s[]);

#endif
