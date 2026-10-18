#include "../shell.h"
#include "../screen.h"
#include "../util.h"
#include "../task.h"
#include "../timer.h"
#include "../net/net.h"

// "label value\n" in one atomic print
static void print_field(const char* label, const char* value) {
    char line[64];
    strcpy(line, (char*)label);
    strcpy(line + strlen(line), (char*)value);
    append(line, '\n');
    print_string(line);
}

static void print_ip(const char* label, uint32_t ip) {
    char s[16];
    ip_to_str(ip, s);
    print_field(label, s);
}

static void cmd_ifconfig(char* args) {
    (void)args;
    if (!netif.up) {
        print_string("No network card (start QEMU with -device rtl8139).\n");
        return;
    }
    char mac[18];
    mac_to_str(netif.mac, mac);
    print_field("eth0     RTL8139  mac ", mac);
    print_ip("  ip       ", netif.ip);
    print_ip("  netmask  ", netif.mask);
    print_ip("  gateway  ", netif.gateway);
    print_ip("  dns      ", netif.dns);
    print_string(netif.configured ? "  config   DHCP\n" : "  config   static defaults\n");
}

static void cmd_arp(char* args) {
    (void)args;
    uint32_t ips[16];
    uint8_t macs[16][6];
    int n = arp_table(ips, macs, 16);
    if (n == 0) { print_string("ARP cache is empty.\n"); return; }
    for (int i = 0; i < n; i++) {
        char line[48], s[18];
        ip_to_str(ips[i], line);
        while (strlen(line) < 17) append(line, ' ');
        mac_to_str(macs[i], s);
        strcpy(line + strlen(line), s);
        append(line, '\n');
        print_string(line);
    }
}

// ─── ping (runs as a foreground job: it sleeps between packets) ───────────────

static void ping_job() {
    char* args = shell_job_args();
    while (*args == ' ') args++;
    char* count_str = args;
    while (*count_str && *count_str != ' ') count_str++;
    int count = 4;
    if (*count_str) {
        *count_str++ = '\0';
        int c = ascii_to_int(count_str);
        if (c > 0 && c < 100) count = c;
    }

    uint32_t dst;
    if (!str_to_ip(args, &dst)) {
        print_string("Usage: ping <a.b.c.d> [count]\n");
        task_exit(2, EXIT_NORMAL);
    }

    char ip[16], line[80];
    ip_to_str(dst, ip);
    strcpy(line, "PING ");
    strcpy(line + strlen(line), ip);
    strcpy(line + strlen(line), ": 56 data bytes\n");
    print_string(line);

    int received = 0;
    for (int seq = 1; seq <= count; seq++) {
        uint32_t start = get_tick();
        if (icmp_send_echo(dst, 0x4B41, seq) != 0) {
            print_string("Destination unreachable (no ARP reply / no route)\n");
        } else {
            uint8_t ttl = 0;
            int ok = 0;
            while (!(ok = icmp_echo_reply(seq, &ttl)) && get_tick() - start < 100)
                task_sleep(1);
            if (ok) {
                received++;
                strcpy(line, "64 bytes from ");
                strcpy(line + strlen(line), ip);
                strcpy(line + strlen(line), ": seq=");
                int_to_ascii(seq, line + strlen(line));
                strcpy(line + strlen(line), " ttl=");
                int_to_ascii(ttl, line + strlen(line));
                strcpy(line + strlen(line), " time=");
                int_to_ascii((int)(get_tick() - start) * 10, line + strlen(line));
                strcpy(line + strlen(line), " ms\n");
            } else {
                strcpy(line, "Request timeout for seq=");
                int_to_ascii(seq, line + strlen(line));
                append(line, '\n');
            }
            print_string(line);
        }
        // One packet per second
        while (seq < count && get_tick() - start < 100) task_sleep(1);
    }

    strcpy(line, "--- ");
    int_to_ascii(count, line + strlen(line));
    strcpy(line + strlen(line), " transmitted, ");
    int_to_ascii(received, line + strlen(line));
    strcpy(line + strlen(line), " received ---\n");
    print_string(line);
    task_exit(received ? 0 : 1, EXIT_NORMAL);
}

static void cmd_ping(char* args) {
    if (!netif.up) { print_string("No network card.\n"); return; }
    if (shell_run_job("ping", ping_job, args) < 0)
        print_string("Could not start ping.\n");
}

void init_cmd_net() {
    register_command("ifconfig", "Show the network interface", cmd_ifconfig);
    register_command("arp", "Show the ARP cache", cmd_arp);
    register_command("ping", "Ping a host (ping 10.0.2.2 [count])", cmd_ping);
}
