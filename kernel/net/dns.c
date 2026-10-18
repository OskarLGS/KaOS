#include "net.h"
#include "../util.h"
#include "../timer.h"
#include "../task.h"

#define DNS_PORT  53
#define DNS_LOCAL 5353     // Our source port for queries

static volatile int      answered = 0;
static volatile uint32_t answer_ip = 0;
static volatile uint16_t want_id = 0;

static void dns_handler(uint32_t src, uint16_t sport, const uint8_t* d, uint32_t len) {
    (void)src;
    if (sport != DNS_PORT || len < 12) return;
    uint16_t id = (d[0] << 8) | d[1];
    if (id != want_id) return;
    int rcode = d[3] & 0x0F;
    int qd = (d[4] << 8) | d[5];
    int an = (d[6] << 8) | d[7];
    if (rcode != 0 || an == 0) { answer_ip = 0; answered = 1; return; }

    // Skip a (possibly compressed) name starting at p
    uint32_t p = 12;
    #define SKIP_NAME() do { \
        while (p < len) { \
            if ((d[p] & 0xC0) == 0xC0) { p += 2; break; } \
            if (d[p] == 0) { p++; break; } \
            p += d[p] + 1; \
        } } while (0)

    for (int i = 0; i < qd && p < len; i++) { SKIP_NAME(); p += 4; }
    for (int i = 0; i < an && p + 10 <= len; i++) {
        SKIP_NAME();
        if (p + 10 > len) break;
        uint16_t type  = (d[p] << 8) | d[p + 1];
        uint16_t rdlen = (d[p + 8] << 8) | d[p + 9];
        p += 10;
        if (p + rdlen > len) break;
        if (type == 1 && rdlen == 4) {             // A record (skip CNAMEs)
            uint32_t ip;
            memory_copy((char*)d + p, (char*)&ip, 4);
            answer_ip = ip;
            answered = 1;
            return;
        }
        p += rdlen;
    }
    #undef SKIP_NAME
    answer_ip = 0;
    answered = 1;
}

int dns_resolve(const char* name, uint32_t* ip) {
    if (str_to_ip(name, ip)) return 1;            // Already an address
    if (!netif.up || !netif.dns) return 0;

    // Query: header + QNAME (labels) + QTYPE A + QCLASS IN
    static uint8_t q[300];
    uint32_t n = 0;
    uint16_t id = (uint16_t)(get_tick() * 31 + 7);
    q[n++] = id >> 8; q[n++] = id & 0xFF;
    q[n++] = 0x01; q[n++] = 0x00;                 // Recursion desired
    q[n++] = 0; q[n++] = 1;                       // One question
    for (int i = 0; i < 6; i++) q[n++] = 0;
    const char* s = name;
    while (*s) {
        const char* dot = s;
        while (*dot && *dot != '.') dot++;
        int l = dot - s;
        if (l == 0 || l > 63 || n + l + 6 > sizeof(q)) return 0;
        q[n++] = l;
        for (int i = 0; i < l; i++) q[n++] = s[i];
        s = *dot ? dot + 1 : dot;
    }
    q[n++] = 0;
    q[n++] = 0; q[n++] = 1;                       // Type A
    q[n++] = 0; q[n++] = 1;                       // Class IN

    if (udp_bind(DNS_LOCAL, dns_handler) != 0) return 0;
    int ok = 0;
    for (int attempt = 0; attempt < 3 && !ok; attempt++) {
        want_id = id;
        answered = 0;
        if (udp_send(netif.dns, DNS_LOCAL, DNS_PORT, q, n, 1000) != 0) continue;
        uint32_t start = get_tick();
        while (!answered && get_tick() - start < 200) task_sleep(1);
        if (answered) {
            if (answer_ip) { *ip = answer_ip; ok = 1; }
            else break;                           // NXDOMAIN / no A record
        }
    }
    udp_unbind(DNS_LOCAL);
    return ok;
}
