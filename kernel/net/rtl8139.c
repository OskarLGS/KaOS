#include "rtl8139.h"
#include "../pci.h"
#include "../ports.h"
#include "../memory.h"
#include "../util.h"

// Registers (offsets from the I/O base)
#define REG_MAC     0x00
#define REG_TSD0    0x10   // Transmit status of descriptor 0..3 (+4 each)
#define REG_TSAD0   0x20   // Transmit buffer address 0..3 (+4 each)
#define REG_RBSTART 0x30   // Receive buffer physical address
#define REG_CMD     0x37
#define REG_CAPR    0x38   // Where we have read up to (minus 16)
#define REG_IMR     0x3C
#define REG_ISR     0x3E
#define REG_TCR     0x40
#define REG_RCR     0x44
#define REG_CONFIG1 0x52

#define CMD_BUFE  0x01     // Receive buffer empty
#define CMD_TE    0x04
#define CMD_RE    0x08
#define CMD_RESET 0x10

#define TSD_OWN   (1 << 13) // Descriptor handed back to the driver
#define TSD_TOK   (1 << 15)

#define RX_LEN    8192      // Ring size (RCR RBLEN = 00)
#define RX_ALLOC  (RX_LEN + 16 + 1536)  // WRAP mode may run past the end
#define TX_SLOTS  4
#define TX_LEN    1536

static uint16_t io = 0;
static uint8_t* rx_buf;
static uint32_t rx_off = 0;
static uint8_t* tx_buf[TX_SLOTS];
static int      tx_next = 0;
static uint8_t  mac[6];

// The kernel heap is identity-mapped, so a heap address is its physical
// address — what the card's DMA engine needs.
static uint8_t* dma_alloc(uint32_t size) {
    uint8_t* p = (uint8_t*)kmalloc(size);
    if (p) memory_set(p, 0, size);
    return p;
}

int rtl8139_init() {
    pci_device_t d;
    if (!pci_find(0x10EC, 0x8139, &d)) return 0;
    if (!(d.bar[0] & 1)) return 0;            // BAR0 must be I/O space
    io = d.bar[0] & ~3u;
    pci_enable(&d);

    rx_buf = dma_alloc(RX_ALLOC);
    for (int i = 0; i < TX_SLOTS; i++) tx_buf[i] = dma_alloc(TX_LEN);
    if (!rx_buf || !tx_buf[TX_SLOTS - 1]) return 0;

    port_byte_out(io + REG_CONFIG1, 0x00);   // Power on (LWAKE + LWPTN low)

    port_byte_out(io + REG_CMD, CMD_RESET);  // Software reset
    for (int i = 0; i < 100000 && (port_byte_in(io + REG_CMD) & CMD_RESET); i++) ;

    for (int i = 0; i < 6; i++) mac[i] = port_byte_in(io + REG_MAC + i);

    port_dword_out(io + REG_RBSTART, (uint32_t)rx_buf);
    port_word_out(io + REG_IMR, 0x0000);     // Polled: no interrupts
    // Accept broadcast, multicast, our MAC, all physical; WRAP (bit 7):
    // a frame at the end of the ring continues past it instead of wrapping
    port_dword_out(io + REG_RCR, 0x0F | (1 << 7));
    port_byte_out(io + REG_CMD, CMD_RE | CMD_TE);
    for (int i = 0; i < TX_SLOTS; i++)
        port_dword_out(io + REG_TSAD0 + i * 4, (uint32_t)tx_buf[i]);
    rx_off = 0;
    return 1;
}

void rtl8139_mac(uint8_t out[6]) {
    for (int i = 0; i < 6; i++) out[i] = mac[i];
}

int rtl8139_send(const void* frame, uint32_t len) {
    if (!io || len > TX_LEN) return -1;

    // Wait until the card has finished with this descriptor (OWN set once
    // the frame was copied out). A fresh card reports OWN on all four.
    uint32_t reg = io + REG_TSD0 + tx_next * 4;
    for (int i = 0; i < 100000; i++) {
        uint32_t st = port_dword_in(reg);
        if ((st & TSD_OWN) || st == 0) break;
    }

    memory_copy((char*)frame, (char*)tx_buf[tx_next], len);
    if (len < 60) {                           // Ethernet minimum frame
        memory_set(tx_buf[tx_next] + len, 0, 60 - len);
        len = 60;
    }
    port_dword_out(reg, len);                 // Size, OWN=0: transmit
    tx_next = (tx_next + 1) % TX_SLOTS;
    return 0;
}

int rtl8139_poll(void (*rx)(const uint8_t* frame, uint32_t len)) {
    if (!io) return 0;
    int n = 0;
    while (!(port_byte_in(io + REG_CMD) & CMD_BUFE) && n < 32) {
        uint8_t* p = rx_buf + rx_off;
        uint16_t status = p[0] | (p[1] << 8);
        uint16_t len    = p[2] | (p[3] << 8);   // Includes the 4-byte CRC

        if (!(status & 1) || len < 64 || len > 1522) {
            // Corrupt header: the ring position is lost; restart receiving
            port_byte_out(io + REG_CMD, CMD_TE);
            port_byte_out(io + REG_CMD, CMD_RE | CMD_TE);
            port_dword_out(io + REG_RBSTART, (uint32_t)rx_buf);
            rx_off = 0;
            port_word_out(io + REG_CAPR, (uint16_t)(rx_off - 16));
            break;
        }
        rx(p + 4, len - 4);
        n++;

        rx_off = (rx_off + len + 4 + 3) & ~3u;
        rx_off %= RX_LEN;
        port_word_out(io + REG_CAPR, (uint16_t)(rx_off - 16));
    }
    port_word_out(io + REG_ISR, 0xFFFF);        // Acknowledge everything
    return n;
}
