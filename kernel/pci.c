#include "pci.h"
#include "ports.h"

#define PCI_ADDR 0xCF8
#define PCI_DATA 0xCFC

static uint32_t address(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset) {
    return 0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) |
           ((uint32_t)func << 8) | (offset & 0xFC);
}

uint32_t pci_read(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset) {
    port_dword_out(PCI_ADDR, address(bus, dev, func, offset));
    return port_dword_in(PCI_DATA);
}

void pci_write(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint32_t value) {
    port_dword_out(PCI_ADDR, address(bus, dev, func, offset));
    port_dword_out(PCI_DATA, value);
}

int pci_find(uint16_t vendor, uint16_t device, pci_device_t* out) {
    for (int bus = 0; bus < 256; bus++) {
        for (int dev = 0; dev < 32; dev++) {
            for (int func = 0; func < 8; func++) {
                uint32_t id = pci_read(bus, dev, func, 0x00);
                if ((id & 0xFFFF) == 0xFFFF) {
                    if (func == 0) break;      // No device in this slot
                    continue;
                }
                if ((id & 0xFFFF) == vendor && (id >> 16) == device) {
                    out->bus = bus;
                    out->dev = dev;
                    out->func = func;
                    out->vendor = vendor;
                    out->device = device;
                    for (int b = 0; b < 6; b++)
                        out->bar[b] = pci_read(bus, dev, func, 0x10 + b * 4);
                    out->irq = pci_read(bus, dev, func, 0x3C) & 0xFF;
                    return 1;
                }
                // Single-function device: don't probe functions 1-7
                if (func == 0 && !(pci_read(bus, dev, 0, 0x0C) & 0x00800000)) break;
            }
        }
    }
    return 0;
}

void pci_enable(const pci_device_t* d) {
    uint32_t cmd = pci_read(d->bus, d->dev, d->func, 0x04);
    cmd |= 0x5;    // bit 0: I/O space, bit 2: bus master
    pci_write(d->bus, d->dev, d->func, 0x04, cmd);
}
