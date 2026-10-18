#ifndef RTL8139_H
#define RTL8139_H

#include <stdint.h>

// Realtek RTL8139 (QEMU: -device rtl8139). Polled, no interrupts: the
// network task calls rtl8139_poll every tick.
int  rtl8139_init();                       // 1 if a card was found
void rtl8139_mac(uint8_t mac[6]);
// Queue one Ethernet frame (padded to 60 bytes). 0 on success.
int  rtl8139_send(const void* frame, uint32_t len);
// Hand every received frame to `rx`. Returns the number of frames.
int  rtl8139_poll(void (*rx)(const uint8_t* frame, uint32_t len));

#endif
