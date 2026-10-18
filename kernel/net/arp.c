#include "net.h"
#include "../util.h"
#include "../timer.h"
#include "../task.h"

#define ARP_ENTRIES 16

typedef struct __attribute__((packed)) {
    uint16_t htype, ptype;
    uint8_t  hlen, plen;
    uint16_t op;
    uint8_t  sha[6];
    uint32_t spa;
    uint8_t  tha[6];
    uint32_t tpa;
} arp_pkt_t;

static struct { uint32_t ip; uint8_t mac[6]; } cache[ARP_ENTRIES];
static int next_slot = 0;
static const uint8_t BROADCAST[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

static void learn(uint32_t ip, const uint8_t mac[6]) {
    if (!ip) return;
    int slot = -1;
    for (int i = 0; i < ARP_ENTRIES; i++)
        if (cache[i].ip == ip) { slot = i; break; }
    if (slot < 0) {                       // Round-robin replacement
        slot = next_slot;
        next_slot = (next_slot + 1) % ARP_ENTRIES;
    }
    cache[slot].ip = ip;
    for (int i = 0; i < 6; i++) cache[slot].mac[i] = mac[i];
}

static void send_arp(uint16_t op, const uint8_t tha[6], uint32_t tpa) {
    arp_pkt_t p;
    p.htype = htons(1);
    p.ptype = htons(ETH_IPV4);
    p.hlen = 6;
    p.plen = 4;
    p.op = htons(op);
    for (int i = 0; i < 6; i++) { p.sha[i] = netif.mac[i]; p.tha[i] = tha[i]; }
    p.spa = netif.ip;
    p.tpa = tpa;
    eth_send(op == 1 ? BROADCAST : tha, ETH_ARP, &p, sizeof(p));
}

void arp_receive(const uint8_t* pkt, uint32_t len) {
    if (len < sizeof(arp_pkt_t)) return;
    const arp_pkt_t* p = (const arp_pkt_t*)pkt;
    if (ntohs(p->htype) != 1 || ntohs(p->ptype) != ETH_IPV4) return;

    learn(p->spa, p->sha);                // Requests teach us the sender too
    if (ntohs(p->op) == 1 && netif.ip && p->tpa == netif.ip)
        send_arp(2, p->sha, p->spa);      // Someone asks for us: reply
}

int arp_lookup(uint32_t ip, uint8_t mac[6]) {
    uint32_t f = net_lock();
    for (int i = 0; i < ARP_ENTRIES; i++) {
        if (cache[i].ip == ip) {
            for (int k = 0; k < 6; k++) mac[k] = cache[i].mac[k];
            net_unlock(f);
            return 1;
        }
    }
    static const uint8_t zero[6] = { 0 };
    send_arp(1, zero, ip);                // Who has `ip`?
    net_unlock(f);
    return 0;
}

int arp_resolve(uint32_t ip, uint8_t mac[6], uint32_t ms) {
    uint32_t deadline = get_tick() + (ms + 9) / 10;
    while (1) {
        if (arp_lookup(ip, mac)) return 1;
        // Wait for the reply in 100 ms steps (re-asking each time)
        for (int i = 0; i < 10; i++) {
            if ((int32_t)(get_tick() - deadline) >= 0) return 0;
            task_sleep(1);
            uint32_t f = net_lock();
            int found = 0;
            for (int k = 0; k < ARP_ENTRIES && !found; k++)
                if (cache[k].ip == ip) {
                    for (int j = 0; j < 6; j++) mac[j] = cache[k].mac[j];
                    found = 1;
                }
            net_unlock(f);
            if (found) return 1;
        }
    }
}

int arp_table(uint32_t* ips, uint8_t (*macs)[6], int max) {
    int n = 0;
    uint32_t f = net_lock();
    for (int i = 0; i < ARP_ENTRIES && n < max; i++) {
        if (!cache[i].ip) continue;
        ips[n] = cache[i].ip;
        for (int k = 0; k < 6; k++) macs[n][k] = cache[i].mac[k];
        n++;
    }
    net_unlock(f);
    return n;
}
