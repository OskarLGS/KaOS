#include "net.h"
#include "rtl8139.h"
#include "../task.h"
#include "../util.h"

netif_t netif;

void eth_receive(const uint8_t* frame, uint32_t len);

// Bottom half of the driver: a kernel task polls the card every tick and
// runs each frame up the stack (with the network lock held).
static void net_task() {
    while (1) {
        uint32_t f = net_lock();
        rtl8139_poll(eth_receive);
        net_unlock(f);
        task_sleep(1);
    }
}

// Configure the interface at boot; if no DHCP server answers, fall back
// to QEMU's user-mode network addresses.
static void dhcp_task() {
    if (dhcp_configure()) return;
    uint32_t f = net_lock();
    netif.ip      = IP4(10, 0, 2, 15);
    netif.mask    = IP4(255, 255, 255, 0);
    netif.gateway = IP4(10, 0, 2, 2);
    netif.dns     = IP4(10, 0, 2, 3);
    net_unlock(f);
}

void net_init() {
    memory_set((uint8_t*)&netif, 0, sizeof(netif));
    if (!rtl8139_init()) return;
    rtl8139_mac(netif.mac);
    netif.up = 1;

    create_task("net", net_task);
    create_task("dhcp", dhcp_task);
}

// ─── Helpers ──────────────────────────────────────────────────────────────────

void ip_to_str(uint32_t ip, char* out) {
    out[0] = '\0';
    for (int i = 0; i < 4; i++) {
        int_to_ascii((ip >> (i * 8)) & 0xFF, out + strlen(out));
        if (i < 3) append(out, '.');
    }
}

int str_to_ip(const char* s, uint32_t* ip) {
    uint32_t r = 0;
    for (int part = 0; part < 4; part++) {
        if (*s < '0' || *s > '9') return 0;
        int v = 0, digits = 0;
        while (*s >= '0' && *s <= '9') {
            v = v * 10 + (*s++ - '0');
            if (++digits > 3 || v > 255) return 0;
        }
        r |= (uint32_t)v << (part * 8);
        if (part < 3 && *s++ != '.') return 0;
    }
    if (*s) return 0;
    *ip = r;
    return 1;
}

void mac_to_str(const uint8_t mac[6], char* out) {
    const char* hx = "0123456789abcdef";
    for (int i = 0; i < 6; i++) {
        out[i * 3]     = hx[mac[i] >> 4];
        out[i * 3 + 1] = hx[mac[i] & 0xF];
        out[i * 3 + 2] = (i < 5) ? ':' : '\0';
    }
}
