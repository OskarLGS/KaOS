#include "net.h"
#include "../util.h"

typedef struct __attribute__((packed)) {
    uint16_t sport, dport, len, checksum;
} udp_hdr_t;

#define UDP_BINDINGS 8

static struct { uint16_t port; udp_handler_t fn; } bindings[UDP_BINDINGS];

// One's-complement sum of the TCP/UDP pseudo-header (addresses in
// network order, as stored)
uint32_t pseudo_sum(uint32_t src, uint32_t dst, uint8_t proto, uint16_t len) {
    const uint8_t* s = (const uint8_t*)&src;
    const uint8_t* d = (const uint8_t*)&dst;
    return ((s[0] << 8) | s[1]) + ((s[2] << 8) | s[3]) +
           ((d[0] << 8) | d[1]) + ((d[2] << 8) | d[3]) + proto + len;
}

int udp_bind(uint16_t port, udp_handler_t fn) {
    uint32_t f = net_lock();
    int slot = -1;
    for (int i = 0; i < UDP_BINDINGS; i++) {
        if (bindings[i].port == port) { slot = i; break; }
        if (!bindings[i].port && slot < 0) slot = i;
    }
    if (slot >= 0) {
        bindings[slot].port = port;
        bindings[slot].fn = fn;
    }
    net_unlock(f);
    return slot >= 0 ? 0 : -1;
}

void udp_unbind(uint16_t port) {
    uint32_t f = net_lock();
    for (int i = 0; i < UDP_BINDINGS; i++)
        if (bindings[i].port == port) bindings[i].port = 0;
    net_unlock(f);
}

int udp_send(uint32_t dst, uint16_t sport, uint16_t dport,
             const void* data, uint32_t len, uint32_t wait_ms) {
    static uint8_t pkt[1472];
    if (len > sizeof(pkt) - sizeof(udp_hdr_t)) return -1;

    // Resolve the next hop first (may sleep, so not under the lock); the
    // send below then finds it in the ARP cache.
    if (wait_ms) {
        uint8_t mac[6];
        uint32_t hop = ip_next_hop(dst);
        if (hop != IP_BROADCAST && hop && !arp_resolve(hop, mac, wait_ms)) return -1;
    }

    uint32_t f = net_lock();              // `pkt` is shared by all senders
    udp_hdr_t* h = (udp_hdr_t*)pkt;
    uint16_t total = sizeof(udp_hdr_t) + len;
    h->sport = htons(sport);
    h->dport = htons(dport);
    h->len = htons(total);
    h->checksum = 0;
    memory_copy((char*)data, (char*)pkt + sizeof(udp_hdr_t), len);
    h->checksum = ip_checksum(pkt, total, pseudo_sum(netif.ip, dst, IP_UDP, total));
    if (h->checksum == 0) h->checksum = 0xFFFF;   // 0 means "no checksum"
    int r = ip_send(dst, IP_UDP, pkt, total, 0);
    net_unlock(f);
    return r;
}

void udp_receive(uint32_t src, uint32_t dst, const uint8_t* pkt, uint32_t len) {
    if (len < sizeof(udp_hdr_t)) return;
    const udp_hdr_t* h = (const udp_hdr_t*)pkt;
    uint16_t total = ntohs(h->len);
    if (total < sizeof(udp_hdr_t) || total > len) return;
    if (h->checksum &&
        ip_checksum(pkt, total, pseudo_sum(src, dst, IP_UDP, total)) != 0) return;

    uint16_t port = ntohs(h->dport);
    for (int i = 0; i < UDP_BINDINGS; i++) {
        if (bindings[i].port == port && bindings[i].fn) {
            bindings[i].fn(src, ntohs(h->sport),
                           pkt + sizeof(udp_hdr_t), total - sizeof(udp_hdr_t));
            return;
        }
    }
}
