#include "users.h"
#include "sha256.h"
#include "vfs.h"
#include "util.h"
#include "rtc.h"
#include "timer.h"

#define HASH_ROUNDS 1000          // Slows down guessing; ~instant for one login
#define PASSWD_MAX  2048

typedef struct {
    char     name[USER_NAME_MAX];
    uint32_t uid;
    char     salt[17];            // 16 hex chars
    char     hash[65];            // 64 hex chars
} account_t;

static account_t accounts[MAX_USERS];
static int count = 0;
static int disabled = 0;

// ─── Hashing ──────────────────────────────────────────────────────────────────

static void to_hex(const uint8_t* in, int n, char* out) {
    const char* hx = "0123456789abcdef";
    for (int i = 0; i < n; i++) {
        out[i * 2]     = hx[in[i] >> 4];
        out[i * 2 + 1] = hx[in[i] & 0xF];
    }
    out[n * 2] = '\0';
}

static void hash_password(const char* salt, const char* password, char out[65]) {
    uint8_t d[32];
    sha256_t c;
    sha256_init(&c);
    sha256_update(&c, salt, strlen((char*)salt));
    sha256_update(&c, password, strlen((char*)password));
    sha256_final(&c, d);
    for (int i = 1; i < HASH_ROUNDS; i++) {
        sha256_init(&c);
        sha256_update(&c, d, 32);
        sha256_update(&c, salt, strlen((char*)salt));
        sha256_final(&c, d);
    }
    to_hex(d, 32, out);
}

// Salt from what entropy we have: TSC, RTC, ticks
static void make_salt(char out[17]) {
    uint32_t lo, hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    rtc_time_t t;
    rtc_read(&t);
    uint8_t seed[16];
    uint32_t parts[4] = { lo, hi, get_tick(),
                          (uint32_t)t.second | (t.minute << 8) | (t.hour << 16) | (t.day << 24) };
    for (int i = 0; i < 16; i++) seed[i] = (uint8_t)(parts[i / 4] >> ((i % 4) * 8));
    uint8_t d[32];
    sha256(seed, 16, d);
    to_hex(d, 8, out);
}

// ─── passwd file ──────────────────────────────────────────────────────────────

static int parse_uint(const char* s, uint32_t* v) {
    uint32_t r = 0;
    if (!*s) return 0;
    for (; *s; s++) {
        if (*s < '0' || *s > '9') return 0;
        r = r * 10 + (*s - '0');
    }
    *v = r;
    return 1;
}

static void parse_line(char* line) {
    char* f[4];
    int n = 0;
    f[n++] = line;
    for (char* p = line; *p && n < 4; p++)
        if (*p == ':') { *p = '\0'; f[n++] = p + 1; }
    if (n != 4 || count >= MAX_USERS) return;
    account_t* a = &accounts[count];
    if (!users_valid_name(f[0]) || !parse_uint(f[1], &a->uid)) return;
    if (strlen(f[2]) != 16 || strlen(f[3]) != 64) return;
    strcpy(a->name, f[0]);
    strcpy(a->salt, f[2]);
    strcpy(a->hash, f[3]);
    count++;
}

static int load() {
    fs_node_t* f = vfs_resolve(PASSWD_PATH);
    if (!f) return 0;
    static char buf[PASSWD_MAX + 1];
    uint32_t n = read_fs(f, 0, PASSWD_MAX, (uint8_t*)buf);
    vfs_release(f);
    buf[n] = '\0';

    count = 0;
    char* line = buf;
    for (uint32_t i = 0; i <= n; i++) {
        if (buf[i] == '\n' || buf[i] == '\0') {
            buf[i] = '\0';
            if (*line) parse_line(line);
            line = buf + i + 1;
        }
    }
    return 1;
}

static int save() {
    static char buf[PASSWD_MAX];
    int n = 0;
    for (int i = 0; i < count; i++) {
        account_t* a = &accounts[i];
        char uid[12];
        int_to_ascii((int)a->uid, uid);
        if (n + strlen(a->name) + strlen(uid) + 16 + 64 + 4 > PASSWD_MAX) return -1;
        strcpy(buf + n, a->name);            n += strlen(a->name);
        buf[n++] = ':';
        strcpy(buf + n, uid);                n += strlen(uid);
        buf[n++] = ':';
        strcpy(buf + n, a->salt);            n += 16;
        buf[n++] = ':';
        strcpy(buf + n, a->hash);            n += 64;
        buf[n++] = '\n';
    }

    fs_node_t* f = vfs_resolve(PASSWD_PATH);
    if (!f) {
        char leaf[VFS_PATH_MAX];
        fs_node_t* dir = vfs_resolve_parent(PASSWD_PATH, leaf);
        if (dir && create_fs(dir, leaf) == 0) f = finddir_fs(dir, leaf);
        vfs_release(dir);
    }
    if (!f) return -1;
    // FAT32 writes set the size to their end: rewriting from 0 truncates
    int ok = write_fs(f, 0, n, (uint8_t*)buf) == (uint32_t)n;
    vfs_release(f);
    return ok ? 0 : -1;
}

int users_init() {
    fs_node_t* disk = vfs_resolve("disk");
    if (!disk) { disabled = 1; return -1; }
    vfs_release(disk);
    disabled = 0;
    return load() ? 1 : 0;
}

int users_disabled() { return disabled; }

// ─── Accounts ─────────────────────────────────────────────────────────────────

int users_valid_name(const char* name) {
    int n = 0;
    for (; name[n]; n++) {
        char c = name[n];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return 0;
    }
    return n >= 1 && n < USER_NAME_MAX;
}

static account_t* find(const char* name) {
    for (int i = 0; i < count; i++)
        if (strcmp(accounts[i].name, (char*)name) == 0) return &accounts[i];
    return 0;
}

int users_find(const char* name, user_t* out) {
    account_t* a = find(name);
    if (!a) return 0;
    strcpy(out->name, a->name);
    out->uid = a->uid;
    return 1;
}

int users_find_uid(uint32_t uid, user_t* out) {
    for (int i = 0; i < count; i++) {
        if (accounts[i].uid == uid) {
            strcpy(out->name, accounts[i].name);
            out->uid = uid;
            return 1;
        }
    }
    return 0;
}

int users_list(user_t* out, int max) {
    int n = 0;
    for (int i = 0; i < count && n < max; i++, n++) {
        strcpy(out[n].name, accounts[i].name);
        out[n].uid = accounts[i].uid;
    }
    return n;
}

int users_check(const char* name, const char* password) {
    account_t* a = find(name);
    char h[65];
    // Hash even for unknown users, so timing doesn't reveal which exist
    hash_password(a ? a->salt : "0000000000000000", password, h);
    if (!a) return 0;
    int diff = 0;
    for (int i = 0; i < 64; i++) diff |= h[i] ^ a->hash[i];
    return diff == 0;
}

static int mkdir_path(const char* path) {
    fs_node_t* d = vfs_resolve(path);
    if (d) { vfs_release(d); return 0; }
    char leaf[VFS_PATH_MAX];
    fs_node_t* parent = vfs_resolve_parent(path, leaf);
    int r = parent ? mkdir_fs(parent, leaf) : -1;
    vfs_release(parent);
    return r;
}

int users_add(const char* name, const char* password) {
    if (disabled || !users_valid_name(name) || find(name) || count >= MAX_USERS) return -1;
    uint32_t uid = 1000;
    for (int i = 0; i < count; i++)
        if (accounts[i].uid >= uid) uid = accounts[i].uid + 1;

    account_t* a = &accounts[count];
    strcpy(a->name, (char*)name);
    a->uid = uid;
    make_salt(a->salt);
    hash_password(a->salt, password, a->hash);
    count++;
    if (save() != 0) { count--; return -1; }

    char home[32];
    strcpy(home, "disk/home/");
    strcpy(home + strlen(home), (char*)name);
    mkdir_path("disk/home");
    mkdir_path(home);
    return 0;
}

int users_set_password(const char* name, const char* password) {
    account_t* a = find(name);
    if (!a) return -1;
    make_salt(a->salt);
    hash_password(a->salt, password, a->hash);
    return save();
}

int users_setup_root(const char* password) {
    if (disabled) return -1;
    mkdir_path("disk/etc");
    mkdir_path("disk/home");
    mkdir_path("disk/tmp");
    count = 0;
    account_t* a = &accounts[0];
    strcpy(a->name, "root");
    a->uid = UID_ROOT;
    make_salt(a->salt);
    hash_password(a->salt, password, a->hash);
    count = 1;
    return save();
}

// ─── Permissions ──────────────────────────────────────────────────────────────

int path_normalize(const char* in, char* out, int max) {
    int depth = 0;
    int starts[64];               // Where each kept component begins in `out`
    int n = 0;
    out[0] = '\0';
    const char* p = in;
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;
        const char* s = p;
        while (*p && *p != '/') p++;
        int len = p - s;
        if (len == 1 && s[0] == '.') continue;
        if (len == 2 && s[0] == '.' && s[1] == '.') {
            if (depth == 0) return 0;          // Above the root
            n = starts[--depth];
            if (n > 0) n--;                    // Drop the separating '/'
            out[n] = '\0';
            continue;
        }
        if (depth >= 64 || n + len + 2 > max) return 0;
        if (n > 0) out[n++] = '/';
        starts[depth++] = n;
        for (int i = 0; i < len; i++) {
            char c = s[i];
            if (c >= 'A' && c <= 'Z') c += 32;
            out[n++] = c;
        }
        out[n] = '\0';
    }
    return 1;
}

// 1 if `path` is strictly inside `dir` ("dir/something")
static int inside(const char* path, const char* dir) {
    int n = strlen((char*)dir);
    return strncmp((char*)path, (char*)dir, n) == 0 && path[n] == '/' && path[n + 1];
}

int perm_read(uint32_t uid, const char* path) {
    if (uid == UID_ROOT || disabled) return 1;
    char p[VFS_PATH_MAX];
    if (!path_normalize(path, p, sizeof(p))) return 0;
    return strcmp(p, PASSWD_PATH) != 0;
}

int perm_write(uint32_t uid, const char* path) {
    if (uid == UID_ROOT || disabled) return 1;
    char p[VFS_PATH_MAX];
    if (!path_normalize(path, p, sizeof(p))) return 0;
    if (inside(p, "disk/tmp")) return 1;

    user_t u;
    if (!users_find_uid(uid, &u)) return 0;
    char home[32];
    strcpy(home, "disk/home/");
    strcpy(home + strlen(home), u.name);
    return inside(p, home);
}
