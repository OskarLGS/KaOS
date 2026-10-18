#include "net.h"
#include "../util.h"
#include "../memory.h"
#include "../timer.h"
#include "../task.h"

// A small TCP client: a handful of connections, in-order receive only,
// go-back-N retransmission. Enough for HTTP over a LAN or QEMU's NAT.

#define TCP_CONNS  8
#define TCP_LISTENERS 2
#define BACKLOG    4         // Established, not yet accepted
#define RX_SIZE    8192
#define TX_SIZE    4096
#define MSS        1460
#define RTO_TICKS  50        // 500 ms retransmission timeout
#define MAX_RETRY  8

#define FIN 0x01
#define SYN 0x02
#define RST 0x04
#define PSH 0x08
#define ACK 0x10

typedef enum {
    T_FREE, T_SYN_SENT, T_SYN_RCVD, T_ESTABLISHED, T_FIN_WAIT_1, T_FIN_WAIT_2,
    T_CLOSE_WAIT, T_LAST_ACK, T_CLOSED
} tstate_t;

typedef struct __attribute__((packed)) {
    uint16_t sport, dport;
    uint32_t seq, ack;
    uint8_t  off;          // Header length in 32-bit words << 4
    uint8_t  flags;
    uint16_t window, checksum, urgent;
} tcp_hdr_t;

typedef struct {
    tstate_t state;
    uint32_t rip;
    uint16_t rport, lport;
    uint32_t snd_una, snd_nxt;   // Oldest unacked / next to send
    uint32_t snd_wnd;            // Peer's window
    uint32_t rcv_nxt;
    uint8_t* rx;                 // Received, not yet read (ring)
    uint32_t rx_head, rx_len;
    uint8_t* tx;                 // From snd_una: unacked + unsent bytes
    uint32_t tx_len;
    int      fin_out;            // We sent FIN (seq = una + tx_len)
    int      fin_in;             // Peer's FIN arrived
    int      reset;              // Connection refused / reset
    uint32_t last_tx;            // Tick of the last (re)transmission
    int      retries;
    uint32_t last_adv;           // Window we last advertised
    int      owner;              // Task using it: aborted if that task dies
    int      listener;           // Listener id + 1 while waiting for accept
} tconn_t;

// A passive socket: SYNs to `port` become connections queued for accept
typedef struct {
    uint16_t port;               // 0 = free
    int      owner;
    int      queue[BACKLOG];     // Connection ids, oldest first
    int      qn;
} listener_t;

static listener_t listeners[TCP_LISTENERS];

static tconn_t conns[TCP_CONNS];
static uint16_t next_port = 49152;

static int seq_lt(uint32_t a, uint32_t b)  { return (int32_t)(a - b) < 0; }
static int seq_le(uint32_t a, uint32_t b)  { return (int32_t)(a - b) <= 0; }

static uint32_t rx_free(tconn_t* c) { return RX_SIZE - c->rx_len; }

// Build and send one segment (lock held)
static void output(tconn_t* c, uint8_t flags, uint32_t seq, const uint8_t* data, uint32_t len) {
    static uint8_t seg[sizeof(tcp_hdr_t) + 4 + MSS];
    tcp_hdr_t* h = (tcp_hdr_t*)seg;
    uint32_t hlen = sizeof(tcp_hdr_t) + ((flags & SYN) ? 4 : 0);

    h->sport = htons(c->lport);
    h->dport = htons(c->rport);
    h->seq = htonl(seq);
    h->ack = htonl((flags & ACK) ? c->rcv_nxt : 0);
    h->off = (hlen / 4) << 4;
    h->flags = flags;
    uint32_t wnd = rx_free(c);
    h->window = htons(wnd > 0xFFFF ? 0xFFFF : wnd);
    c->last_adv = wnd;
    h->checksum = 0;
    h->urgent = 0;
    if (flags & SYN) {                        // MSS option
        seg[20] = 2; seg[21] = 4; seg[22] = MSS >> 8; seg[23] = MSS & 0xFF;
    }
    if (len) memory_copy((char*)data, (char*)seg + hlen, len);
    h->checksum = ip_checksum(seg, hlen + len,
                              pseudo_sum(netif.ip, c->rip, IP_TCP, hlen + len));
    ip_send(c->rip, IP_TCP, seg, hlen + len, 0);
}

// Send whatever the peer's window allows from snd_nxt on (lock held)
static void push(tconn_t* c) {
    if (c->state != T_ESTABLISHED && c->state != T_CLOSE_WAIT &&
        c->state != T_FIN_WAIT_1 && c->state != T_LAST_ACK) return;

    uint32_t sent = c->snd_nxt - c->snd_una;
    uint32_t data_end = c->tx_len;
    while (sent < data_end) {
        uint32_t room = c->snd_wnd > sent ? c->snd_wnd - sent : 0;
        uint32_t n = data_end - sent;
        if (n > MSS) n = MSS;
        if (n > room) n = room;
        if (n == 0) break;                    // Peer's window is full
        output(c, ACK | PSH, c->snd_nxt, c->tx + sent, n);
        c->snd_nxt += n;
        sent += n;
        c->last_tx = get_tick();
    }
    // FIN goes after all data, once
    if (c->fin_out && sent == data_end && c->snd_nxt == c->snd_una + data_end) {
        output(c, ACK | FIN, c->snd_nxt, 0, 0);
        c->snd_nxt++;
        c->last_tx = get_tick();
    }
}

static void free_conn(tconn_t* c) {
    if (c->rx) kfree((uint32_t)c->rx);
    if (c->tx) kfree((uint32_t)c->tx);
    memory_set((uint8_t*)c, 0, sizeof(*c));
}

// RST for a segment that belongs to no connection
static void send_rst(uint32_t src, const tcp_hdr_t* h, uint32_t seglen) {
    tconn_t tmp;
    memory_set((uint8_t*)&tmp, 0, sizeof(tmp));
    tmp.rip = src;
    tmp.rport = ntohs(h->sport);
    tmp.lport = ntohs(h->dport);
    if (h->flags & ACK) {
        output(&tmp, RST, ntohl(h->ack), 0, 0);
    } else {
        tmp.rcv_nxt = ntohl(h->seq) + seglen;
        output(&tmp, RST | ACK, 0, 0, 0);
    }
}

// A SYN for a listening port: new connection in SYN_RCVD, answer SYN+ACK.
// Returns 1 if a listener took it. Runs in the net task (lock held).
static int passive_open(uint32_t src, uint32_t dst, const tcp_hdr_t* h, uint32_t seq) {
    (void)dst;
    listener_t* l = 0;
    for (int i = 0; i < TCP_LISTENERS; i++)
        if (listeners[i].port && listeners[i].port == ntohs(h->dport)) l = &listeners[i];
    if (!l) return 0;

    int id = -1;
    for (int i = 0; i < TCP_CONNS; i++)
        if (conns[i].state == T_FREE) { id = i; break; }
    if (id < 0) return 0;                       // Full: the caller sends RST
    uint8_t* rx = (uint8_t*)kmalloc(RX_SIZE);
    uint8_t* tx = (uint8_t*)kmalloc(TX_SIZE);
    if (!rx || !tx) {
        if (rx) kfree((uint32_t)rx);
        if (tx) kfree((uint32_t)tx);
        return 0;
    }

    tconn_t* c = &conns[id];
    memory_set((uint8_t*)c, 0, sizeof(*c));
    c->rip = src;
    c->rport = ntohs(h->sport);
    c->lport = l->port;
    c->rx = rx;
    c->tx = tx;
    c->owner = l->owner;
    c->listener = (int)(l - listeners) + 1;
    c->rcv_nxt = seq + 1;
    c->snd_wnd = ntohs(h->window);
    uint32_t iss = get_tick() * 2654435761u + 12345;
    c->snd_una = iss;
    c->snd_nxt = iss + 1;                       // Our SYN
    c->state = T_SYN_RCVD;
    c->last_tx = get_tick();
    output(c, SYN | ACK, iss, 0, 0);
    return 1;
}

void tcp_receive(uint32_t src, uint32_t dst, const uint8_t* pkt, uint32_t len) {
    if (len < sizeof(tcp_hdr_t)) return;
    if (ip_checksum(pkt, len, pseudo_sum(src, dst, IP_TCP, len)) != 0) return;
    const tcp_hdr_t* h = (const tcp_hdr_t*)pkt;
    uint32_t hlen = (h->off >> 4) * 4;
    if (hlen < 20 || hlen > len) return;

    const uint8_t* data = pkt + hlen;
    uint32_t dlen = len - hlen;
    uint32_t seq = ntohl(h->seq), ack = ntohl(h->ack);
    uint8_t fl = h->flags;

    tconn_t* c = 0;
    for (int i = 0; i < TCP_CONNS; i++)
        if (conns[i].state != T_FREE && conns[i].rip == src &&
            conns[i].rport == ntohs(h->sport) && conns[i].lport == ntohs(h->dport))
            c = &conns[i];
    if ((!c || c->state == T_CLOSED) && (fl & SYN) && !(fl & ACK)) {
        if (passive_open(src, dst, h, seq)) return;
    }
    if (!c || c->state == T_CLOSED) {
        if (!(fl & RST)) send_rst(src, h, dlen + ((fl & SYN) ? 1 : 0) + ((fl & FIN) ? 1 : 0));
        return;
    }

    if (fl & RST) {                           // Refused or aborted
        c->reset = 1;
        c->state = T_CLOSED;
        return;
    }

    if (c->state == T_SYN_RCVD) {
        if (fl & SYN) {                       // Our SYN+ACK was lost: again
            output(c, SYN | ACK, c->snd_una, 0, 0);
            return;
        }
        if (!(fl & ACK) || ack != c->snd_nxt) return;
        // Handshake done: hand it to the listener's accept queue
        listener_t* l = c->listener ? &listeners[c->listener - 1] : 0;
        if (!l || !l->port || l->qn >= BACKLOG) {
            output(c, RST, c->snd_nxt, 0, 0);
            free_conn(c);
            return;
        }
        c->snd_una = ack;
        c->snd_wnd = ntohs(h->window);
        c->state = T_ESTABLISHED;
        c->retries = 0;
        l->queue[l->qn++] = (int)(c - conns);
        // Fall through: the ACK may already carry data
    }

    if (c->state == T_SYN_SENT) {
        if ((fl & (SYN | ACK)) == (SYN | ACK) && ack == c->snd_nxt) {
            c->rcv_nxt = seq + 1;
            c->snd_una = ack;
            c->snd_wnd = ntohs(h->window);
            c->state = T_ESTABLISHED;
            c->retries = 0;
            output(c, ACK, c->snd_nxt, 0, 0);
            push(c);
        }
        return;
    }

    // ACK: drop what the peer has confirmed from the send buffer
    if ((fl & ACK) && seq_lt(c->snd_una, ack) && seq_le(ack, c->snd_nxt)) {
        uint32_t acked = ack - c->snd_una;
        uint32_t data_acked = acked > c->tx_len ? c->tx_len : acked;
        memory_copy((char*)c->tx + data_acked, (char*)c->tx, c->tx_len - data_acked);
        c->tx_len -= data_acked;
        c->snd_una = ack;
        c->retries = 0;
        c->last_tx = get_tick();
        int fin_acked = c->fin_out && c->snd_una == c->snd_nxt && c->tx_len == 0;
        if (fin_acked && c->state == T_FIN_WAIT_1) c->state = T_FIN_WAIT_2;
        if (fin_acked && c->state == T_LAST_ACK)   c->state = T_CLOSED;
    }
    if (fl & ACK) c->snd_wnd = ntohs(h->window);

    // Data, in order only; anything else gets a duplicate ACK
    int need_ack = 0;
    if (dlen || (fl & FIN)) {
        if (seq == c->rcv_nxt) {
            uint32_t n = dlen < rx_free(c) ? dlen : rx_free(c);
            for (uint32_t i = 0; i < n; i++)
                c->rx[(c->rx_head + c->rx_len + i) % RX_SIZE] = data[i];
            c->rx_len += n;
            c->rcv_nxt += n;
            if ((fl & FIN) && n == dlen) {
                c->rcv_nxt++;
                c->fin_in = 1;
                if (c->state == T_ESTABLISHED) c->state = T_CLOSE_WAIT;
                if (c->state == T_FIN_WAIT_1 || c->state == T_FIN_WAIT_2) c->state = T_CLOSED;
            }
        }
        need_ack = 1;
    }
    if (need_ack) output(c, ACK, c->snd_nxt, 0, 0);
    push(c);
}

// Called by the network task every tick: retransmissions (lock held)
void tcp_tick() {
    uint32_t now = get_tick();
    // Listeners whose process died: stop listening (their pending
    // connections share that owner and are freed just below)
    for (int i = 0; i < TCP_LISTENERS; i++)
        if (listeners[i].port && !task_alive(listeners[i].owner))
            memory_set((uint8_t*)&listeners[i], 0, sizeof(listener_t));

    for (int i = 0; i < TCP_CONNS; i++) {
        tconn_t* c = &conns[i];
        if (c->state == T_FREE) continue;
        // Its task was killed (Ctrl+C) without closing: abort and free it
        if (!task_alive(c->owner)) {
            if (c->state != T_CLOSED) output(c, RST | ACK, c->snd_nxt, 0, 0);
            free_conn(c);
            continue;
        }
        if (c->state == T_CLOSED) continue;
        int pending = (c->state == T_SYN_SENT) || (c->state == T_SYN_RCVD) ||
                      c->snd_una != c->snd_nxt;
        if (!pending || now - c->last_tx < (uint32_t)(RTO_TICKS << (c->retries > 3 ? 3 : c->retries)))
            continue;
        if (++c->retries > MAX_RETRY) {       // Give up
            c->reset = 1;
            c->state = T_CLOSED;
            continue;
        }
        c->last_tx = now;
        if (c->state == T_SYN_SENT) {
            output(c, SYN, c->snd_una, 0, 0);
        } else if (c->state == T_SYN_RCVD) {
            output(c, SYN | ACK, c->snd_una, 0, 0);
        } else {
            c->snd_nxt = c->snd_una;          // Go back N
            push(c);
        }
    }
}

// ─── Socket-like API (task context) ───────────────────────────────────────────

int tcp_connect(uint32_t ip, uint16_t port, uint32_t timeout_ms) {
    uint8_t mac[6];
    uint32_t hop = ip_next_hop(ip);
    if (!hop || !arp_resolve(hop, mac, 1000)) return -1;

    uint8_t* rx = (uint8_t*)kmalloc(RX_SIZE);
    uint8_t* tx = (uint8_t*)kmalloc(TX_SIZE);
    if (!rx || !tx) {
        if (rx) kfree((uint32_t)rx);
        if (tx) kfree((uint32_t)tx);
        return -1;
    }

    uint32_t f = net_lock();
    int id = -1;
    for (int i = 0; i < TCP_CONNS; i++)
        if (conns[i].state == T_FREE) { id = i; break; }
    if (id < 0) {
        net_unlock(f);
        kfree((uint32_t)rx);
        kfree((uint32_t)tx);
        return -1;
    }
    tconn_t* c = &conns[id];
    memory_set((uint8_t*)c, 0, sizeof(*c));
    c->rip = ip;
    c->rport = port;
    c->lport = next_port++;
    if (next_port == 0) next_port = 49152;
    c->rx = rx;
    c->tx = tx;
    c->owner = get_current_task_id();
    uint32_t iss = get_tick() * 2654435761u;
    c->snd_una = iss;
    c->snd_nxt = iss + 1;                      // SYN takes one number
    c->state = T_SYN_SENT;
    c->last_tx = get_tick();
    output(c, SYN, iss, 0, 0);
    net_unlock(f);

    uint32_t start = get_tick();
    while (c->state == T_SYN_SENT && get_tick() - start < (timeout_ms + 9) / 10)
        task_sleep(1);

    if (c->state != T_ESTABLISHED) {
        int refused = c->reset;
        f = net_lock();
        free_conn(c);
        net_unlock(f);
        return refused ? -2 : -1;
    }
    return id;
}

int tcp_send(int id, const void* data, uint32_t len) {
    tconn_t* c = &conns[id];
    const uint8_t* p = (const uint8_t*)data;
    uint32_t done = 0;
    while (done < len) {
        uint32_t f = net_lock();
        if (c->state != T_ESTABLISHED && c->state != T_CLOSE_WAIT) {
            net_unlock(f);
            return done ? (int)done : -1;
        }
        uint32_t n = TX_SIZE - c->tx_len;
        if (n > len - done) n = len - done;
        memory_copy((char*)p + done, (char*)c->tx + c->tx_len, n);
        c->tx_len += n;
        done += n;
        push(c);
        net_unlock(f);
        if (done < len) task_sleep(1);         // Buffer full: wait for ACKs
    }
    return (int)done;
}

int tcp_recv(int id, void* buf, uint32_t max, uint32_t timeout_ms) {
    tconn_t* c = &conns[id];
    uint32_t start = get_tick();
    while (1) {
        uint32_t f = net_lock();
        if (c->rx_len) {
            uint32_t n = c->rx_len < max ? c->rx_len : max;
            for (uint32_t i = 0; i < n; i++)
                ((uint8_t*)buf)[i] = c->rx[(c->rx_head + i) % RX_SIZE];
            c->rx_head = (c->rx_head + n) % RX_SIZE;
            c->rx_len -= n;
            // Window update once a useful amount of space opened up again
            if (c->last_adv < MSS && rx_free(c) >= MSS &&
                (c->state == T_ESTABLISHED || c->state == T_FIN_WAIT_1 || c->state == T_FIN_WAIT_2))
                output(c, ACK, c->snd_nxt, 0, 0);
            net_unlock(f);
            return (int)n;
        }
        int done = c->fin_in || c->state == T_CLOSED;
        int reset = c->reset;
        net_unlock(f);
        if (reset) return -1;
        if (done) return 0;                    // Peer closed: end of stream
        if (get_tick() - start >= (timeout_ms + 9) / 10) return -1;
        task_sleep(1);
    }
}

void tcp_close(int id) {
    tconn_t* c = &conns[id];
    uint32_t f = net_lock();
    if (c->state == T_ESTABLISHED) c->state = T_FIN_WAIT_1;
    else if (c->state == T_CLOSE_WAIT) c->state = T_LAST_ACK;
    if (c->state == T_FIN_WAIT_1 || c->state == T_LAST_ACK) {
        c->fin_out = 1;
        push(c);
    }
    net_unlock(f);

    // Give the FIN a moment to be acknowledged, then forget the connection
    uint32_t start = get_tick();
    while (c->state != T_CLOSED && c->state != T_FIN_WAIT_2 && get_tick() - start < 200)
        task_sleep(1);

    f = net_lock();
    free_conn(c);
    net_unlock(f);
}

int tcp_listen(uint16_t port) {
    uint32_t f = net_lock();
    int id = -1;
    for (int i = 0; i < TCP_LISTENERS; i++) {
        if (listeners[i].port == port) { net_unlock(f); return -2; }   // In use
        if (!listeners[i].port && id < 0) id = i;
    }
    for (int i = 0; i < TCP_CONNS && id >= 0; i++)
        if (conns[i].state != T_FREE && conns[i].lport == port && !conns[i].listener) {
            net_unlock(f);
            return -2;
        }
    if (id >= 0) {
        memory_set((uint8_t*)&listeners[id], 0, sizeof(listener_t));
        listeners[id].port = port;
        listeners[id].owner = get_current_task_id();
    }
    net_unlock(f);
    return id;
}

int tcp_accept(int lid, uint32_t timeout_ms) {
    listener_t* l = &listeners[lid];
    uint32_t start = get_tick();
    while (1) {
        uint32_t f = net_lock();
        if (!l->port) { net_unlock(f); return -1; }
        if (l->qn) {
            int c = l->queue[0];
            for (int i = 1; i < l->qn; i++) l->queue[i - 1] = l->queue[i];
            l->qn--;
            conns[c].listener = 0;
            conns[c].owner = get_current_task_id();
            net_unlock(f);
            return c;
        }
        net_unlock(f);
        if (timeout_ms && get_tick() - start >= (timeout_ms + 9) / 10) return -1;
        task_sleep(1);
    }
}

void tcp_unlisten(int lid) {
    uint32_t f = net_lock();
    listener_t* l = &listeners[lid];
    for (int i = 0; i < l->qn; i++) {           // Never accepted: abort
        tconn_t* c = &conns[l->queue[i]];
        output(c, RST, c->snd_nxt, 0, 0);
        free_conn(c);
    }
    for (int i = 0; i < TCP_CONNS; i++)         // Mid-handshake ones too
        if (conns[i].state != T_FREE && conns[i].listener == lid + 1) free_conn(&conns[i]);
    memory_set((uint8_t*)l, 0, sizeof(listener_t));
    net_unlock(f);
}
