#include "mouse.h"
#include "isr.h"
#include "ports.h"

// Screen bounds (VBE 800x600)
#define SCREEN_W 800
#define SCREEN_H 600

// volatile: updated by the IRQ12 handler, polled by the desktop loop
static volatile int mouse_x = 400;
static volatile int mouse_y = 300;
static volatile uint8_t mouse_buttons = 0;

// Software sensitivity multiplier (PS/2 deltas feel sluggish at 1:1)
#define MOUSE_SPEED 2

// 3-byte packet state machine
static uint8_t mouse_cycle = 0;
static uint8_t mouse_byte[3];

// ─── PS/2 helpers ────────────────────────────────────────────────────────────

static void mouse_wait_write() {
    int timeout = 100000;
    while (timeout-- && (port_byte_in(0x64) & 0x02));
}

static void mouse_wait_read() {
    int timeout = 100000;
    while (timeout-- && !(port_byte_in(0x64) & 0x01));
}

static void mouse_write(uint8_t data) {
    mouse_wait_write();
    port_byte_out(0x64, 0xD4);  // Tell controller: next byte goes to mouse
    mouse_wait_write();
    port_byte_out(0x60, data);
}

static uint8_t mouse_read() {
    mouse_wait_read();
    return port_byte_in(0x60);
}

// ─── IRQ12 handler ───────────────────────────────────────────────────────────

static void mouse_irq_handler(struct registers* regs) {
    (void)regs;

    // Drain everything the controller has for us. Packets can pile up while
    // the kernel is busy redrawing; reading a single byte per IRQ loses data
    // and desyncs the 3-byte packets (movement then looks inverted/erratic).
    while (1) {
        uint8_t status = port_byte_in(0x64);
        if (!(status & 0x01)) break;  // Output buffer empty
        if (!(status & 0x20)) break;  // Byte is keyboard data — IRQ1's job

        uint8_t data = port_byte_in(0x60);

        switch (mouse_cycle) {
            case 0:
                // Byte 0: flags — bit 3 must always be 1 (sync bit).
                // If clear we are mid-packet after a lost byte: drop and resync.
                if (!(data & 0x08)) break;
                mouse_byte[0] = data;
                mouse_cycle = 1;
                break;
            case 1:
                mouse_byte[1] = data;
                mouse_cycle = 2;
                break;
            case 2: {
                mouse_byte[2] = data;
                mouse_cycle = 0;

                // Overflow bits set → deltas are garbage, discard the packet
                if (mouse_byte[0] & 0xC0) break;

                mouse_buttons = mouse_byte[0] & 0x07;

                // Deltas are 8-bit two's complement (sign-extend via int8_t)
                int dx =  (int)(int8_t)mouse_byte[1];
                int dy = -(int)(int8_t)mouse_byte[2]; // PS/2 Y positive = up

                mouse_x += dx * MOUSE_SPEED;
                mouse_y += dy * MOUSE_SPEED;

                if (mouse_x < 0)          mouse_x = 0;
                if (mouse_x >= SCREEN_W)  mouse_x = SCREEN_W - 1;
                if (mouse_y < 0)          mouse_y = 0;
                if (mouse_y >= SCREEN_H)  mouse_y = SCREEN_H - 1;
                break;
            }
        }
    }
}

// ─── Init ────────────────────────────────────────────────────────────────────

void init_mouse() {
    // 1. Enable auxiliary PS/2 device
    mouse_wait_write();
    port_byte_out(0x64, 0xA8);

    // 2. Enable IRQ12: read compaq status byte, set bit 1, write back
    mouse_wait_write();
    port_byte_out(0x64, 0x20);
    mouse_wait_read();
    uint8_t status = (port_byte_in(0x60) | 0x02) & ~0x20;
    mouse_wait_write();
    port_byte_out(0x64, 0x60);
    mouse_wait_write();
    port_byte_out(0x60, status);

    // 3. Send "Set Defaults" command to mouse
    mouse_write(0xF6);
    mouse_read(); // ACK

    // 4. Enable data reporting
    mouse_write(0xF4);
    mouse_read(); // ACK

    // 5. Flush stale bytes so the packet state machine starts in sync
    while (port_byte_in(0x64) & 0x01) port_byte_in(0x60);

    // 6. Hook IRQ12
    register_interrupt_handler(12, mouse_irq_handler);
}

// ─── Accessors ───────────────────────────────────────────────────────────────

int mouse_get_x()            { return mouse_x; }
int mouse_get_y()            { return mouse_y; }
uint8_t mouse_get_buttons()  { return mouse_buttons; }
