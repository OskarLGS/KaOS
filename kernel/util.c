#include "util.h"

void memory_copy(char* source, char* dest, int n_bytes) {
    if (n_bytes <= 0) return;
    // x86 string ops: copy 4 bytes at a time, then the remainder.
    // ~30x faster than a byte loop at -O0; swap_buffers moves 1.9MB/frame.
    uint32_t dwords = (uint32_t)n_bytes >> 2;
    uint32_t rest   = (uint32_t)n_bytes & 3;
    __asm__ __volatile__("cld; rep movsl"
                         : "+S"(source), "+D"(dest), "+c"(dwords)
                         : : "memory");
    __asm__ __volatile__("rep movsb"
                         : "+S"(source), "+D"(dest), "+c"(rest)
                         : : "memory");
}

void memory_set(uint8_t* dest, uint8_t val, uint32_t len) {
    __asm__ __volatile__("cld; rep stosb"
                         : "+D"(dest), "+c"(len)
                         : "a"(val) : "memory");
}

void int_to_ascii(int n, char str[]) {
    int i, sign;
    if ((sign = n) < 0) n = -n;
    i = 0;
    do {
        str[i++] = n % 10 + '0';
    } while ((n /= 10) > 0);

    if (sign < 0) str[i++] = '-';
    str[i] = '\0';

    reverse(str);
}

void reverse(char s[]) {
    int i, j;
    char c;
    for (i = 0, j = strlen(s)-1; i < j; i++, j--) {
        c = s[i];
        s[i] = s[j];
        s[j] = c;
    }
}

int strlen(char s[]) {
    int i = 0;
    while (s[i] != '\0') ++i;
    return i;
}

void append(char s[], char n) {
    int len = strlen(s);
    s[len] = n;
    s[len+1] = '\0';
}

void backspace(char s[]) {
    int len = strlen(s);
    if (len > 0) {
        s[len-1] = '\0';
    }
}

int strcmp(char s1[], char s2[]) {
    int i;
    for (i = 0; s1[i] == s2[i]; i++) {
        if (s1[i] == '\0') return 0;
    }
    return s1[i] - s2[i];
}

int strncmp(char s1[], char s2[], int n) {
    for (int i = 0; i < n; i++) {
        if (s1[i] != s2[i]) return s1[i] - s2[i];
        if (s1[i] == '\0') return 0;
    }
    return 0;
}

int ascii_to_int(char s[]) {
    int res = 0;
    for (int i = 0; s[i] != '\0'; ++i) {
        if (s[i] >= '0' && s[i] <= '9') {
            res = res * 10 + s[i] - '0';
        }
    }
    return res;
}
void strcpy(char* dest, char* src) {
    int i = 0;
    while (src[i] != '\0') {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}
