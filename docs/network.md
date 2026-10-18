# Networking (kernel/net/)

A small IPv4 stack over an RTL8139 card, built for QEMU's user-mode
network (`-netdev user,id=n0 -device rtl8139,netdev=n0`), where the
guest is 10.0.2.15, the gateway 10.0.2.2 (also the host's 127.0.0.1)
and the DNS server 10.0.2.3.

```
 ping / host / fetch / dhcp     (kernel jobs: kernel/commands/cmd_net.c)
 ─────────────────────────────
 ICMP      UDP ── DHCP, DNS      TCP
 ─────────────────────────────
 IPv4 (ipv4.c)     ARP (arp.c)
 ─────────────────────────────
 Ethernet (ethernet.c)
 ─────────────────────────────
 RTL8139 driver (rtl8139.c) ── PCI (kernel/pci.c)
```

## Driver and the network task

| Piece | Details |
|-------|---------|
| PCI | Configuration mechanism #1 (ports 0xCF8/0xCFC); `pci_find(0x10EC, 0x8139)`, then I/O + bus-master enable |
| Receive | 8 KB DMA ring in WRAP mode (+1.5 KB slack) in the identity-mapped heap; `CAPR` tracks what was read |
| Transmit | Four descriptors, each with a 1536-byte buffer, used round-robin; frames padded to 60 bytes |
| Interrupts | None: a `net` kernel task polls the card every tick (10 ms), runs each frame up the stack, then drives TCP timers |

All protocol state is protected by `net_lock()` (interrupts off — it
nests, and is never held across a sleep). Anything that may wait for an
ARP reply resolves it first, unlocked, and then sends under the lock.

## Protocols

| Layer | Implemented |
|-------|-------------|
| ARP | 16-entry cache, answers requests for our IP, learns from requests, `arp_resolve` with timeout |
| IPv4 | Header checksum, next hop by subnet/gateway, broadcast; no fragmentation (DF set, fragments dropped) |
| ICMP | Echo replies; echo requests for `ping` |
| UDP | Port bindings with handlers (run in the net task), pseudo-header checksum |
| DHCP | DISCOVER → OFFER → REQUEST → ACK at boot (4 tries), broadcast flag; sets ip/mask/gateway/dns. Falls back to QEMU's addresses. No lease renewal |
| DNS | A-record queries to the configured server; skips CNAMEs, follows compressed names |
| TCP | Client side: active open with MSS option, in-order receive into an 8 KB ring (out-of-order dropped, duplicate ACK), window updates, send buffer within the peer window, go-back-N retransmission (500 ms RTO with backoff, 8 tries), FIN both ways, RST for unknown connections. 4 connections. A connection whose task dies is aborted with RST |

## Commands

| Command | |
|---------|-|
| `ifconfig` | Show MAC, ip, mask, gateway, dns; `ifconfig dns 10.0.2.2` sets one field |
| `dhcp` | Ask for an address again |
| `arp` | ARP cache |
| `ping <host> [count]` | ICMP echo, one per second |
| `host <name>` | DNS lookup |
| `fetch [http://]host[:port][/path] [> file]` | HTTP/1.0 GET; prints the body (or saves it) and the status line |

`ping`, `host`, `dhcp` and `fetch` run as **foreground kernel jobs**
(`shell_run_job`): the prompt waits, output streams (the desktop stays
live), Ctrl+C stops them.

## Testing against the host

QEMU forwards 10.0.2.2 to the host's loopback, so local servers work:

```bash
python3 -m http.server 8080 --bind 127.0.0.1   # then in KaOS:
fetch 10.0.2.2:8080/file.txt > disk/file.txt
```

Name lookups need a DNS server; if the host has none, run a small one
on 127.0.0.1:53 and point KaOS at it with `ifconfig dns 10.0.2.2`.
