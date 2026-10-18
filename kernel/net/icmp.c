#include "net.h"
#include "../util.h"

typedef struct __attribute__((packed)) {
    uint8_t  type, code;
    uint16_t checksum;
    uint16_t id, seq;
} icmp_echo_t;

#define ECHO_DATA 56     // Classic ping payload: 64-byte ICMP message

// Replies to our pings, by sequence number (ring of the last 64)
static volatile uint8_t reply_seen[64];
static volatile uint8_t reply_ttl[64];

void icmp_receive(uint32_t src, const uint8_t* pkt, uint32_t len, uint8_t ttl) {
    if (len < sizeof(icmp_echo_t) || ip_checksum(pkt, len, 0) != 0) return;
    const icmp_echo_t* e = (const icmp_echo_t*)pkt;

    if (e->type == 8 && e->code == 0 && netif.ip) {
        // Echo request: send the same message back as a reply
        static uint8_t out[1480];
        if (len > sizeof(out)) return;
        memory_copy((char*)pkt, (char*)out, len);
        icmp_echo_t* r = (icmp_echo_t*)out;
        r->type = 0;
        r->checksum = 0;
        r->checksum = ip_checksum(out, len, 0);
        ip_send(src, IP_ICMP, out, len, 0);
    } else if (e->type == 0) {
        uint16_t seq = ntohs(e->seq);
        reply_ttl[seq % 64] = ttl;
        reply_seen[seq % 64] = 1;
    }
}

int icmp_send_echo(uint32_t dst, uint16_t id, uint16_t seq) {
    uint8_t msg[sizeof(icmp_echo_t) + ECHO_DATA];
    icmp_echo_t* e = (icmp_echo_t*)msg;
    e->type = 8;
    e->code = 0;
    e->checksum = 0;
    e->id = htons(id);
    e->seq = htons(seq);
    for (int i = 0; i < ECHO_DATA; i++) msg[sizeof(icmp_echo_t) + i] = (uint8_t)i;
    e->checksum = ip_checksum(msg, sizeof(msg), 0);

    reply_seen[seq % 64] = 0;
    return ip_send(dst, IP_ICMP, msg, sizeof(msg), 1000);
}

int icmp_echo_reply(uint16_t seq, uint8_t* ttl) {
    if (!reply_seen[seq % 64]) return 0;
    *ttl = reply_ttl[seq % 64];
    return 1;
}
