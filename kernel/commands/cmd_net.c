#include "../shell.h"
#include "../screen.h"
#include "../util.h"
#include "../task.h"
#include "../timer.h"
#include "../net/net.h"
#include "../vfs.h"
#include "../users.h"

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

// "ifconfig ip|mask|gateway|dns <a.b.c.d>": set one field by hand
static int ifconfig_set(char* args) {
    while (*args == ' ') args++;
    char* val = args;
    while (*val && *val != ' ') val++;
    if (!*val) return 0;
    *val++ = '\0';
    while (*val == ' ') val++;

    uint32_t* field = 0;
    if (strcmp(args, "ip") == 0) field = &netif.ip;
    else if (strcmp(args, "mask") == 0) field = &netif.mask;
    else if (strcmp(args, "gateway") == 0) field = &netif.gateway;
    else if (strcmp(args, "dns") == 0) field = &netif.dns;
    uint32_t ip;
    if (!field || !str_to_ip(val, &ip)) {
        print_string("Usage: ifconfig [ip|mask|gateway|dns a.b.c.d]\n");
        return 1;
    }
    uint32_t f = net_lock();
    *field = ip;
    net_unlock(f);
    print_string("Set.\n");
    return 1;
}

static void cmd_ifconfig(char* args) {
    if (!netif.up) {
        print_string("No network card (start QEMU with -device rtl8139).\n");
        return;
    }
    if (ifconfig_set(args)) return;
    char mac[18];
    mac_to_str(netif.mac, mac);
    print_field("eth0     RTL8139  mac ", mac);
    print_ip("  ip       ", netif.ip);
    print_ip("  netmask  ", netif.mask);
    print_ip("  gateway  ", netif.gateway);
    print_ip("  dns      ", netif.dns);
    print_string(netif.configured ? "  config   DHCP\n"
                 : netif.ip ? "  config   static (no DHCP answer)\n"
                 : "  config   waiting for DHCP...\n");
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
    if (!*args) {
        print_string("Usage: ping <host> [count]\n");
        task_exit(2, EXIT_NORMAL);
    }
    if (!dns_resolve(args, &dst)) {
        print_string("ping: unknown host\n");
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

static void dhcp_job() {
    print_string("Asking for an address (DHCP)...\n");
    if (!dhcp_configure()) {
        print_string("No DHCP answer; keeping the old settings.\n");
        task_exit(1, EXIT_NORMAL);
    }
    char s[16], line[48];
    ip_to_str(netif.ip, s);
    strcpy(line, "Configured: ");
    strcpy(line + strlen(line), s);
    append(line, '\n');
    print_string(line);
}

static void cmd_dhcp(char* args) {
    (void)args;
    if (!netif.up) { print_string("No network card.\n"); return; }
    shell_run_job("dhcp", dhcp_job, "");
}

static void host_job() {
    char* name = shell_job_args();
    while (*name == ' ') name++;
    uint32_t ip;
    if (!*name) { print_string("Usage: host <name>\n"); task_exit(2, EXIT_NORMAL); }
    if (!dns_resolve(name, &ip)) {
        print_string("Host not found (or no DNS answer).\n");
        task_exit(1, EXIT_NORMAL);
    }
    char s[16], line[160];
    ip_to_str(ip, s);
    strcpy(line, name);
    strcpy(line + strlen(line), " has address ");
    strcpy(line + strlen(line), s);
    append(line, '\n');
    print_string(line);
}

static void cmd_host(char* args) {
    if (!netif.up) { print_string("No network card.\n"); return; }
    shell_run_job("host", host_job, args);
}

// ─── fetch: a minimal HTTP/1.0 client ─────────────────────────────────────────

// Print `n` bytes of body text, dropping carriage returns
static void print_body(const char* s, int n) {
    char out[257];
    int k = 0;
    for (int i = 0; i < n; i++) {
        if (s[i] == '\r') continue;
        out[k++] = s[i];
        if (k == 256) { out[k] = '\0'; print_string(out); k = 0; }
    }
    out[k] = '\0';
    print_string(out);
}

static void fetch_job() {
    char* a = shell_job_args();
    while (*a == ' ') a++;

    // Optional "> path": save the body instead of printing it
    char* save = 0;
    for (char* p = a; *p; p++) {
        if (*p == '>') {
            *p = '\0';
            save = p + 1;
            while (*save == ' ') save++;
            break;
        }
    }
    char* end = a + strlen(a);
    while (end > a && end[-1] == ' ') *--end = '\0';
    if (!*a || (save && !*save)) {
        print_string("Usage: fetch [http://]host[:port][/path] [> file]\n");
        task_exit(2, EXIT_NORMAL);
    }
    if (save && !perm_write(current_uid(), save)) {
        print_string("fetch: permission denied for that file\n");
        task_exit(1, EXIT_NORMAL);
    }

    // URL -> host, port, path
    if (strncmp(a, "http://", 7) == 0) a += 7;
    char host[96], path[160];
    int hn = 0;
    while (*a && *a != '/' && *a != ':' && hn < 95) host[hn++] = *a++;
    host[hn] = '\0';
    uint16_t port = 80;
    if (*a == ':') {
        a++;
        int p = 0;
        while (*a >= '0' && *a <= '9') p = p * 10 + (*a++ - '0');
        if (p > 0 && p < 65536) port = (uint16_t)p;
    }
    if (*a == '/') { int i = 0; while (*a && i < 159) path[i++] = *a++; path[i] = '\0'; }
    else strcpy(path, "/");

    uint32_t ip;
    if (!dns_resolve(host, &ip)) {
        print_string("fetch: unknown host\n");
        task_exit(1, EXIT_NORMAL);
    }
    int c = tcp_connect(ip, port, 5000);
    if (c == -2) { print_string("fetch: connection refused\n"); task_exit(1, EXIT_NORMAL); }
    if (c < 0)   { print_string("fetch: could not connect\n"); task_exit(1, EXIT_NORMAL); }

    static char req[400];
    strcpy(req, "GET ");
    strcpy(req + strlen(req), path);
    strcpy(req + strlen(req), " HTTP/1.0\r\nHost: ");
    strcpy(req + strlen(req), host);
    strcpy(req + strlen(req), "\r\nUser-Agent: KaOS-fetch/1.0\r\nConnection: close\r\n\r\n");
    if (tcp_send(c, req, strlen(req)) < 0) {
        print_string("fetch: send failed\n");
        tcp_close(c);
        task_exit(1, EXIT_NORMAL);
    }

    // Destination file for "> path"
    fs_node_t* out = 0;
    if (save) {
        out = vfs_resolve(save);
        if (!out) {
            char leaf[VFS_PATH_MAX];
            fs_node_t* dir = vfs_resolve_parent(save, leaf);
            if (dir && create_fs(dir, leaf) == 0) out = finddir_fs(dir, leaf);
            vfs_release(dir);
        }
        if (!out || (out->flags & 0x7) != FS_FILE) {
            print_string("fetch: cannot write that file\n");
            vfs_release(out);
            tcp_close(c);
            task_exit(1, EXIT_NORMAL);
        }
    }

    // Read the response; split the headers off the body
    static char buf[512];
    static char status[80];
    int in_headers = 1, st_len = 0, matched = 0, status_done = 0;
    uint32_t body = 0;
    int n, failed = 0;
    while ((n = tcp_recv(c, buf, sizeof(buf), 10000)) > 0) {
        int i = 0;
        while (in_headers && i < n) {
            char ch = buf[i++];
            if (!status_done) {
                if (ch == '\r' || ch == '\n') status_done = 1;
                else if (st_len < 79) status[st_len++] = ch;
            }
            // End of headers: \r\n\r\n
            const char* pat = "\r\n\r\n";
            matched = (ch == pat[matched]) ? matched + 1 : (ch == '\r' ? 1 : 0);
            if (matched == 4) in_headers = 0;
        }
        if (i < n) {
            if (out && write_fs(out, body, n - i, (uint8_t*)buf + i) != (uint32_t)(n - i)) {
                print_string("fetch: writing the file failed\n");
                failed = 1;
                break;
            }
            if (!out) print_body(buf + i, n - i);
            body += n - i;
        }
    }
    if (n < 0) failed = 1;
    tcp_close(c);
    vfs_release(out);

    status[st_len] = '\0';
    char line[140];
    strcpy(line, "\n[");
    strcpy(line + strlen(line), st_len ? status : "no HTTP response");
    strcpy(line + strlen(line), ", ");
    int_to_ascii((int)body, line + strlen(line));
    strcpy(line + strlen(line), failed ? " bytes, connection lost]\n" : " bytes]\n");
    print_string(line);
    task_exit(failed || !st_len ? 1 : 0, EXIT_NORMAL);
}

static void cmd_fetch(char* args) {
    if (!netif.up) { print_string("No network card.\n"); return; }
    shell_run_job("fetch", fetch_job, args);
}

void init_cmd_net() {
    register_command("fetch", "HTTP GET (fetch 10.0.2.2:8080/f.txt [> disk/f])", cmd_fetch);
    register_command("dhcp", "Renew the address with DHCP", cmd_dhcp);
    register_command("host", "Resolve a name with DNS (host example.com)", cmd_host);
    register_command("ifconfig", "Show/set the interface (ifconfig dns 10.0.2.3)", cmd_ifconfig);
    register_command("arp", "Show the ARP cache", cmd_arp);
    register_command("ping", "Ping a host (ping 10.0.2.2 [count])", cmd_ping);
}
