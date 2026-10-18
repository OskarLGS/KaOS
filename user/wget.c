// HTTP client in user space: exec disk/wget.elf host[:port]/path [file]
// Uses sockets (SYS_RESOLVE + SYS_CONNECT, then read/write on the fd)
// and saves the body to `file` (default: print it). Like the shell's
// fetch, but running in Ring 3 with the user's permissions.
#include "syscall.h"

static int slen(const char* s) { int n = 0; while (s[n]) n++; return n; }
static void say(const char* s) { sys_write(s, slen(s)); }

static void say_num(int v) {
    char b[12];
    int i = 11;
    b[i] = 0;
    if (v == 0) b[--i] = '0';
    while (v > 0) { b[--i] = '0' + v % 10; v /= 10; }
    say(b + i);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        say("Usage: wget.elf host[:port]/path [file]\n");
        return 2;
    }

    // host[:port][/path]
    char host[64], path[128];
    const char* u = argv[1];
    if (u[0] == 'h' && u[1] == 't' && u[2] == 't' && u[3] == 'p' && u[4] == ':' &&
        u[5] == '/' && u[6] == '/') u += 7;
    int h = 0;
    while (*u && *u != ':' && *u != '/' && h < 63) host[h++] = *u++;
    host[h] = 0;
    int port = 80;
    if (*u == ':') {
        port = 0;
        u++;
        while (*u >= '0' && *u <= '9') port = port * 10 + (*u++ - '0');
    }
    int p = 0;
    if (*u != '/') path[p++] = '/';
    while (*u && p < 127) path[p++] = *u++;
    path[p] = 0;

    unsigned int ip;
    if (sys_resolve(host, &ip) != 0) { say("wget: unknown host\n"); return 1; }
    int s = sys_connect(ip, port);
    if (s == -2) { say("wget: connection refused\n"); return 1; }
    if (s < 0)   { say("wget: could not connect\n"); return 1; }

    int out = -1;
    if (argc > 2) {
        out = sys_open(argv[2], O_WRONLY | O_CREAT);
        if (out < 0) {
            say("wget: cannot write ");
            say(argv[2]);
            say(" (permission denied?)\n");
            sys_close(s);
            return 1;
        }
    }

    char req[256];
    int n = 0;
    const char* parts[] = { "GET ", path, " HTTP/1.0\r\nHost: ", host,
                            "\r\nUser-Agent: KaOS-wget\r\nConnection: close\r\n\r\n" };
    for (int i = 0; i < 5; i++)
        for (const char* c = parts[i]; *c && n < 255; c++) req[n++] = *c;
    sys_write_fd(s, req, n);

    // Skip the headers (up to the blank line), keep the body
    char buf[512];
    int state = 0, body = 0, r;
    while ((r = sys_read_fd(s, buf, sizeof(buf))) > 0) {
        int i = 0;
        while (state < 4 && i < r) {
            char c = buf[i++];
            state = (c == "\r\n\r\n"[state]) ? state + 1 : (c == '\r' ? 1 : 0);
        }
        if (i < r) {
            if (out >= 0) sys_write_fd(out, buf + i, r - i);
            else sys_write(buf + i, r - i);
            body += r - i;
        }
    }
    sys_close(s);
    if (out >= 0) sys_close(out);

    say("\nwget: ");
    say_num(body);
    say(r < 0 ? " bytes (connection error)\n" : " bytes\n");
    return r < 0 ? 1 : 0;
}
