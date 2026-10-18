// A tiny web server in user space: exec disk/httpd.elf [port] [root] &
// Serves files below `root` (default disk/www) over HTTP/1.0, one client
// at a time, with the permissions of the user who started it. Ports
// below 1024 need root. Reach it from the host with QEMU's hostfwd, e.g.
//   -netdev user,id=n0,hostfwd=tcp::8080-:80   ->  http://localhost:8080/
#include "syscall.h"

static int slen(const char* s) { int n = 0; while (s[n]) n++; return n; }
static void say(const char* s) { sys_write(s, slen(s)); }

static void num(int v, char* out) {
    char b[12];
    int i = 11;
    b[i] = 0;
    if (v == 0) b[--i] = '0';
    while (v > 0) { b[--i] = '0' + v % 10; v /= 10; }
    int k = 0;
    while (b[i]) out[k++] = b[i++];
    out[k] = 0;
}

static void send_str(int c, const char* s) { sys_write_fd(c, s, slen(s)); }

static void reply(int c, const char* status, const char* body) {
    send_str(c, "HTTP/1.0 ");
    send_str(c, status);
    send_str(c, "\r\nContent-Type: text/plain\r\nServer: KaOS-httpd\r\n\r\n");
    send_str(c, body);
}

static const char* type_of(const char* path) {
    int n = slen(path);
    if (n > 5 && path[n-5] == '.' && path[n-4] == 'h' && path[n-3] == 't') return "text/html";
    if (n > 4 && path[n-4] == '.' && path[n-3] == 't' && path[n-2] == 'x') return "text/plain";
    return "application/octet-stream";
}

static void serve(int c, const char* root) {
    // Request line: "GET /path HTTP/1.x"
    static char req[512];
    int n = 0, r;
    while (n < 511 && (r = sys_read_fd(c, req + n, 511 - n)) > 0) {
        n += r;
        req[n] = 0;
        int done = 0;
        for (int i = 0; i + 3 < n; i++)
            if (req[i] == '\r' && req[i+1] == '\n' && req[i+2] == '\r' && req[i+3] == '\n') done = 1;
        if (done) break;
    }
    req[n] = 0;
    if (n < 5 || req[0] != 'G' || req[1] != 'E' || req[2] != 'T' || req[3] != ' ') {
        reply(c, "400 Bad Request", "Only GET is supported.\n");
        return;
    }

    static char path[200];
    int p = slen(root);
    for (int i = 0; i < p; i++) path[i] = root[i];
    const char* u = req + 4;
    if (*u != '/') { reply(c, "400 Bad Request", "Bad path.\n"); return; }
    while (*u && *u != ' ' && *u != '?' && p < 180) {
        if (u[0] == '.' && u[1] == '.') { reply(c, "403 Forbidden", "No '..' here.\n"); return; }
        path[p++] = *u++;
    }
    path[p] = 0;
    if (path[p - 1] == '/') {                    // Directory: its index.html
        const char* idx = "index.html";
        for (int i = 0; idx[i]; i++) path[p++] = idx[i];
        path[p] = 0;
    }

    int f = sys_open(path, O_RDONLY);
    if (f < 0) {
        reply(c, "404 Not Found", "Not found.\n");
        say("httpd: 404 "); say(path); say("\n");
        return;
    }
    send_str(c, "HTTP/1.0 200 OK\r\nContent-Type: ");
    send_str(c, type_of(path));
    send_str(c, "\r\nServer: KaOS-httpd\r\n\r\n");
    static char buf[1024];
    int total = 0;
    while ((r = sys_read_fd(f, buf, sizeof(buf))) > 0) {
        if (sys_write_fd(c, buf, r) != r) break;
        total += r;
    }
    sys_close(f);
    char t[12];
    num(total, t);
    say("httpd: 200 "); say(path); say(" ("); say(t); say(" bytes)\n");
}

int main(int argc, char** argv) {
    int port = 80;
    if (argc > 1) {
        port = 0;
        for (const char* d = argv[1]; *d >= '0' && *d <= '9'; d++) port = port * 10 + (*d - '0');
    }
    const char* root = argc > 2 ? argv[2] : "disk/www";

    int l = sys_listen(port);
    if (l == -2) { say("httpd: port in use\n"); return 1; }
    if (l < 0)   { say("httpd: cannot listen (ports < 1024 need root)\n"); return 1; }
    char t[12];
    num(port, t);
    say("httpd: serving "); say(root); say(" on port "); say(t); say("\n");

    while (1) {
        int c = sys_accept(l);
        if (c < 0) break;
        serve(c, root);
        sys_close(c);
    }
    sys_close(l);
    return 0;
}
