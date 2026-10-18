#ifndef NET_H
#define NET_H

#include <stdint.h>

// ─── Byte order (network = big endian) ────────────────────────────────────────
static inline uint16_t htons(uint16_t v) { return (uint16_t)((v << 8) | (v >> 8)); }
static inline uint16_t ntohs(uint16_t v) { return htons(v); }
static inline uint32_t htonl(uint32_t v) {
    return (v << 24) | ((v & 0xFF00) << 8) | ((v >> 8) & 0xFF00) | (v >> 24);
}
static inline uint32_t ntohl(uint32_t v) { return htonl(v); }

// IPv4 addresses are kept in network byte order everywhere (as on the wire)
#define IP4(a, b, c, d) ((uint32_t)(a) | ((uint32_t)(b) << 8) | \
                         ((uint32_t)(c) << 16) | ((uint32_t)(d) << 24))
#define IP_BROADCAST 0xFFFFFFFFu

// ─── Interface configuration ──────────────────────────────────────────────────
typedef struct {
    int      up;           // A card was found and initialised
    uint8_t  mac[6];
    uint32_t ip, mask, gateway, dns;   // 0 until DHCP (or set by hand)
    int      configured;   // DHCP finished
} netif_t;

extern netif_t netif;

// ─── Locking ──────────────────────────────────────────────────────────────────
// Packet processing (network task) and senders (ping/fetch jobs) share
// protocol state and the card's transmit ring: every entry point runs
// under this lock. It only disables interrupts, so it nests and must
// never be held across a sleep.
static inline uint32_t net_lock() {
    uint32_t eflags;
    __asm__ __volatile__("pushf; pop %0; cli" : "=r"(eflags) :: "memory");
    return eflags;
}
static inline void net_unlock(uint32_t eflags) {
    if (eflags & 0x200) __asm__ __volatile__("sti" ::: "memory");
}

// ─── Layers ───────────────────────────────────────────────────────────────────
#define ETH_ARP  0x0806
#define ETH_IPV4 0x0800
#define IP_ICMP  1
#define IP_TCP   6
#define IP_UDP   17

void net_init();               // Find the card, start the network task

// Ethernet
int  eth_send(const uint8_t dst[6], uint16_t type, const void* payload, uint32_t len);

// ARP
void arp_receive(const uint8_t* pkt, uint32_t len);
// MAC for `ip` if cached (1), else sends a request and returns 0.
int  arp_lookup(uint32_t ip, uint8_t mac[6]);
// Like arp_lookup, but waits (task context, unlocked) up to `ms`.
int  arp_resolve(uint32_t ip, uint8_t mac[6], uint32_t ms);
int  arp_table(uint32_t* ips, uint8_t (*macs)[6], int max);

// IPv4
uint16_t ip_checksum(const void* data, uint32_t len, uint32_t initial);
void ip_receive(const uint8_t* pkt, uint32_t len);
// Send one datagram; resolves the next hop with ARP (caller must not
// hold the lock if `wait_ms` > 0). Returns 0 on success.
int  ip_send(uint32_t dst, uint8_t proto, const void* payload, uint32_t len, uint32_t wait_ms);
// Next hop for `dst` (itself on the local subnet, else the gateway)
uint32_t ip_next_hop(uint32_t dst);

// ICMP
void icmp_receive(uint32_t src, const uint8_t* pkt, uint32_t len, uint8_t ttl);
int  icmp_send_echo(uint32_t dst, uint16_t id, uint16_t seq);
// 1 once the reply for `seq` arrived (fills its ttl); cleared on send.
int  icmp_echo_reply(uint16_t seq, uint8_t* ttl);

// UDP
typedef void (*udp_handler_t)(uint32_t src, uint16_t sport, const uint8_t* data, uint32_t len);
int  udp_bind(uint16_t port, udp_handler_t fn);   // Handler runs in the net task
void udp_unbind(uint16_t port);
int  udp_send(uint32_t dst, uint16_t sport, uint16_t dport,
              const void* data, uint32_t len, uint32_t wait_ms);
uint32_t pseudo_sum(uint32_t src, uint32_t dst, uint8_t proto, uint16_t len);

// DHCP: configure netif. Runs in task context; returns 1 on success.
int  dhcp_configure();

// DNS: resolve `name` (or a dotted IP) to an address. Task context.
int  dns_resolve(const char* name, uint32_t* ip);

// Helpers
void ip_to_str(uint32_t ip, char* out);        // "10.0.2.15" (16 bytes)
int  str_to_ip(const char* s, uint32_t* ip);   // 1 if it parsed
void mac_to_str(const uint8_t mac[6], char* out); // 18 bytes

#endif
