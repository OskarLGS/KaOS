#ifndef PCI_H
#define PCI_H

#include <stdint.h>

// A PCI function found by pci_find (configuration mechanism #1, ports
// 0xCF8/0xCFC).
typedef struct {
    uint8_t  bus, dev, func;
    uint16_t vendor, device;
    uint32_t bar[6];
    uint8_t  irq;       // Interrupt Line (PIC IRQ number the BIOS assigned)
} pci_device_t;

uint32_t pci_read(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset);
void     pci_write(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint32_t value);

// Find the first function with this vendor/device id. Returns 1 if found.
int  pci_find(uint16_t vendor, uint16_t device, pci_device_t* out);
// Enable I/O space decoding and bus mastering (needed for DMA).
void pci_enable(const pci_device_t* d);

#endif
