#include "net.h"
#include "rtl8139.h"
#include "../util.h"

#define ETH_HDR 14
#define ETH_MTU 1500

int eth_send(const uint8_t dst[6], uint16_t type, const void* payload, uint32_t len) {
    static uint8_t frame[ETH_HDR + ETH_MTU];
    if (len > ETH_MTU) return -1;

    uint32_t f = net_lock();      // `frame` and the card's TX ring are shared
    for (int i = 0; i < 6; i++) {
        frame[i]     = dst[i];
        frame[6 + i] = netif.mac[i];
    }
    frame[12] = type >> 8;
    frame[13] = type & 0xFF;
    memory_copy((char*)payload, (char*)frame + ETH_HDR, len);
    int r = rtl8139_send(frame, ETH_HDR + len);
    net_unlock(f);
    return r;
}

// Called by the network task for every received frame (lock held)
void eth_receive(const uint8_t* frame, uint32_t len) {
    if (len < ETH_HDR) return;
    uint16_t type = (frame[12] << 8) | frame[13];
    if (type == ETH_ARP)  arp_receive(frame + ETH_HDR, len - ETH_HDR);
    if (type == ETH_IPV4) ip_receive(frame + ETH_HDR, len - ETH_HDR);
}
