#include "net.h"
#include "../util.h"
#include "../timer.h"
#include "../task.h"

#define DHCP_CLIENT 68
#define DHCP_SERVER 67
#define DHCP_MAGIC  0x63825363

#define DHCPDISCOVER 1
#define DHCPOFFER    2
#define DHCPREQUEST  3
#define DHCPACK      5
#define DHCPNAK      6

typedef struct __attribute__((packed)) {
    uint8_t  op, htype, hlen, hops;
    uint32_t xid;
    uint16_t secs, flags;
    uint32_t ciaddr, yiaddr, siaddr, giaddr;
    uint8_t  chaddr[16];
    uint8_t  sname[64];
    uint8_t  file[128];
    uint32_t magic;
    uint8_t  options[64];
} dhcp_msg_t;

// What the last OFFER/ACK said (written by the handler in the net task)
static volatile int      got_type = 0;
static volatile uint32_t got_xid, got_ip, got_server, got_mask, got_router, got_dns;
static uint32_t xid_seed = 0x4B614F53;   // "KaOS"

static void dhcp_handler(uint32_t src, uint16_t sport, const uint8_t* data, uint32_t len) {
    (void)src;
    if (sport != DHCP_SERVER || len < 240) return;
    const dhcp_msg_t* m = (const dhcp_msg_t*)data;
    if (m->op != 2 || ntohl(m->magic) != DHCP_MAGIC) return;
    for (int i = 0; i < 6; i++) if (m->chaddr[i] != netif.mac[i]) return;

    int type = 0;
    uint32_t mask = 0, router = 0, dns = 0, server = 0;
    const uint8_t* o = data + 240;
    const uint8_t* end = data + len;
    while (o < end && *o != 255) {
        if (*o == 0) { o++; continue; }               // Pad
        if (o + 2 > end || o + 2 + o[1] > end) break;
        uint8_t code = o[0], olen = o[1];
        const uint8_t* v = o + 2;
        if (code == 53 && olen >= 1) type = v[0];
        if (code == 1  && olen >= 4) memory_copy((char*)v, (char*)&mask, 4);
        if (code == 3  && olen >= 4) memory_copy((char*)v, (char*)&router, 4);
        if (code == 6  && olen >= 4) memory_copy((char*)v, (char*)&dns, 4);
        if (code == 54 && olen >= 4) memory_copy((char*)v, (char*)&server, 4);
        o += 2 + olen;
    }
    got_xid = m->xid;
    got_ip = m->yiaddr;
    got_server = server;
    got_mask = mask;
    got_router = router;
    got_dns = dns;
    got_type = type;          // Last: the waiter polls this
}

static int send_msg(uint8_t type, uint32_t xid, uint32_t req_ip, uint32_t server) {
    static dhcp_msg_t m;
    memory_set((uint8_t*)&m, 0, sizeof(m));
    m.op = 1;                  // BOOTREQUEST
    m.htype = 1;
    m.hlen = 6;
    m.xid = xid;
    m.flags = htons(0x8000);   // Ask for broadcast replies (we have no IP yet)
    for (int i = 0; i < 6; i++) m.chaddr[i] = netif.mac[i];
    m.magic = htonl(DHCP_MAGIC);

    uint8_t* o = m.options;
    *o++ = 53; *o++ = 1; *o++ = type;
    if (req_ip) { *o++ = 50; *o++ = 4; memory_copy((char*)&req_ip, (char*)o, 4); o += 4; }
    if (server) { *o++ = 54; *o++ = 4; memory_copy((char*)&server, (char*)o, 4); o += 4; }
    *o++ = 55; *o++ = 3; *o++ = 1; *o++ = 3; *o++ = 6;   // Want mask, router, DNS
    *o++ = 255;
    return udp_send(IP_BROADCAST, DHCP_CLIENT, DHCP_SERVER, &m, sizeof(m), 0);
}

// Wait up to `ticks` for a reply of `type` to transaction `xid`
static int wait_reply(int type, uint32_t xid, uint32_t ticks) {
    uint32_t start = get_tick();
    while (get_tick() - start < ticks) {
        if (got_type == type && got_xid == xid) return 1;
        if (got_type == DHCPNAK && got_xid == xid) return 0;
        task_sleep(1);
    }
    return 0;
}

int dhcp_configure() {
    if (!netif.up) return 0;
    if (udp_bind(DHCP_CLIENT, dhcp_handler) != 0) return 0;

    // While unconfigured we send from 0.0.0.0 and accept any IP datagram
    uint32_t f = net_lock();
    netif.ip = 0;
    netif.configured = 0;
    net_unlock(f);

    int ok = 0;
    for (int attempt = 0; attempt < 4 && !ok; attempt++) {
        uint32_t xid = htonl(xid_seed + get_tick() + attempt);
        got_type = 0;
        send_msg(DHCPDISCOVER, xid, 0, 0);
        if (!wait_reply(DHCPOFFER, xid, 200)) continue;

        uint32_t offered = got_ip, server = got_server;
        got_type = 0;
        send_msg(DHCPREQUEST, xid, offered, server);
        if (!wait_reply(DHCPACK, xid, 200)) continue;

        f = net_lock();
        netif.ip      = got_ip;
        netif.mask    = got_mask ? got_mask : IP4(255, 255, 255, 0);
        netif.gateway = got_router;
        netif.dns     = got_dns;
        netif.configured = 1;
        net_unlock(f);
        ok = 1;
    }
    udp_unbind(DHCP_CLIENT);
    return ok;
}
