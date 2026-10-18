#include "ports.h"

uint8_t port_byte_in(uint16_t port) {
    uint8_t result;
    __asm__ __volatile__("inb %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

void port_byte_out(uint16_t port, uint8_t data) {
    __asm__ __volatile__("outb %0, %1" : : "a"(data), "Nd"(port));
}

uint16_t port_word_in(uint16_t port) {
    uint16_t result;
    __asm__ __volatile__("inw %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

void port_word_out(uint16_t port, uint16_t data) {
    __asm__ __volatile__("outw %0, %1" : : "a"(data), "Nd"(port));
}

uint32_t port_dword_in(uint16_t port) {
    uint32_t result;
    __asm__ __volatile__("inl %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

void port_dword_out(uint16_t port, uint32_t data) {
    __asm__ __volatile__("outl %0, %1" : : "a"(data), "Nd"(port));
}
