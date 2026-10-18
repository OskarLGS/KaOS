#include "net.h"
#include "../util.h"

typedef struct __attribute__((packed)) {
    uint8_t  ver_ihl;      // 0x45: IPv4, 5 x 32-bit header words
    uint8_t  tos;
    uint16_t total_len;
    uint16_t id;
    uint16_t frag;         // Flags + fragment offset
    uint8_t  ttl;
    uint8_t  proto;
    uint16_t checksum;
    uint32_t src, dst;
} ip_hdr_t;

static uint16_t next_id = 1;
static const uint8_t BROADCAST[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

// Internet checksum (RFC 1071). `initial` lets TCP/UDP add a pseudo-header.
uint16_t ip_checksum(const void* data, uint32_t len, uint32_t initial) {
    const uint8_t* p = (const uint8_t*)data;
    uint32_t sum = initial;
    while (len > 1) { sum += (p[0] << 8) | p[1]; p += 2; len -= 2; }
    if (len) sum += p[0] << 8;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return htons((uint16_t)~sum);
}

uint32_t ip_next_hop(uint32_t dst) {
    if (dst == IP_BROADCAST) return dst;
    if ((dst & netif.mask) == (netif.ip & netif.mask)) return dst;
    return netif.gateway;
}

int ip_send(uint32_t dst, uint8_t proto, const void* payload, uint32_t len, uint32_t wait_ms) {
    static uint8_t pkt[1500];
    if (len > sizeof(pkt) - sizeof(ip_hdr_t)) return -1;

    uint8_t mac[6];
    uint32_t hop = ip_next_hop(dst);
    if (hop == IP_BROADCAST) {
        for (int i = 0; i < 6; i++) mac[i] = BROADCAST[i];
    } else if (!hop) {
        return -1;                                 // No route (no gateway)
    } else if (wait_ms ? !arp_resolve(hop, mac, wait_ms) : !arp_lookup(hop, mac)) {
        return -1;                                 // Next hop unknown (yet)
    }

    uint32_t f = net_lock();
    ip_hdr_t* h = (ip_hdr_t*)pkt;
    h->ver_ihl = 0x45;
    h->tos = 0;
    h->total_len = htons(sizeof(ip_hdr_t) + len);
    h->id = htons(next_id++);
    h->frag = htons(0x4000);                       // Don't fragment
    h->ttl = 64;
    h->proto = proto;
    h->checksum = 0;
    h->src = netif.ip;
    h->dst = dst;
    h->checksum = ip_checksum(h, sizeof(ip_hdr_t), 0);
    memory_copy((char*)payload, (char*)pkt + sizeof(ip_hdr_t), len);
    int r = eth_send(mac, ETH_IPV4, pkt, sizeof(ip_hdr_t) + len);
    net_unlock(f);
    return r;
}

void udp_receive(uint32_t src, uint32_t dst, const uint8_t* pkt, uint32_t len);
void tcp_receive(uint32_t src, uint32_t dst, const uint8_t* pkt, uint32_t len);

void ip_receive(const uint8_t* pkt, uint32_t len) {
    if (len < sizeof(ip_hdr_t)) return;
    const ip_hdr_t* h = (const ip_hdr_t*)pkt;
    if ((h->ver_ihl >> 4) != 4) return;
    uint32_t hlen = (h->ver_ihl & 0xF) * 4;
    uint32_t total = ntohs(h->total_len);
    if (hlen < 20 || total < hlen || total > len) return;
    if (ip_checksum(h, hlen, 0) != 0) return;            // Corrupt header
    if (ntohs(h->frag) & 0x3FFF) return;                 // Fragments: unsupported

    // Ours, broadcast, or anything while unconfigured (DHCP offers)
    if (netif.ip && h->dst != netif.ip && h->dst != IP_BROADCAST &&
        h->dst != (netif.ip | ~netif.mask)) return;

    const uint8_t* body = pkt + hlen;
    uint32_t blen = total - hlen;
    if (h->proto == IP_ICMP) icmp_receive(h->src, body, blen, h->ttl);
    if (h->proto == IP_UDP)  udp_receive(h->src, h->dst, body, blen);
    if (h->proto == IP_TCP)  tcp_receive(h->src, h->dst, body, blen);
}
