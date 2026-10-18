#include "fat32.h"
#include "ata.h"
#include "memory.h"
#include "util.h"
#include "screen.h"
#include "rtc.h"
#include "vfs.h"

static fat32_bpb_t bpb;
static uint32_t fat_start_sector;
static uint32_t data_start_sector;
static uint32_t sectors_per_cluster;
static uint32_t root_dir_cluster;

// VFS node for the FAT32 root
static fs_node_t *fat32_root_node = 0;

// Forward declarations (finddir wires these onto directory nodes)
static int fat32_create(fs_node_t* dir, char* name);
static int fat32_delete(fs_node_t* dir, char* name);
static int fat32_mkdir(fs_node_t* dir, char* name);
static int fat32_rename(fs_node_t* src_dir, char* src_name,
                        fs_node_t* dst_dir, char* dst_name);

static uint32_t cluster_to_lba(uint32_t cluster) {
    return data_start_sector + ((cluster - 2) * sectors_per_cluster);
}

static uint32_t get_next_cluster(uint32_t cluster) {
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = fat_start_sector + (fat_offset / 512);
    uint32_t ent_offset = fat_offset % 512;

    uint8_t buffer[512];
    if (ata_read_sector(fat_sector, buffer) != 0) return 0x0FFFFFF7; // Bad cluster

    uint32_t next_cluster = (*(uint32_t*)&buffer[ent_offset]) & 0x0FFFFFFF;
    return next_cluster;
}

// Write a FAT entry (read-modify-write), mirrored to the second FAT copy.
// The top 4 bits of FAT32 entries are reserved and must be preserved.
static int set_fat_entry(uint32_t cluster, uint32_t value) {
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = fat_start_sector + (fat_offset / 512);
    uint32_t ent_offset = fat_offset % 512;

    uint8_t buffer[512];
    if (ata_read_sector(fat_sector, buffer) != 0) return -1;

    uint32_t* entry = (uint32_t*)&buffer[ent_offset];
    *entry = (*entry & 0xF0000000) | (value & 0x0FFFFFFF);

    if (ata_write_sector(fat_sector, buffer) != 0) return -1;
    if (bpb.num_fats > 1) {
        ata_write_sector(fat_sector + bpb.fat_size_32, buffer);
    }
    return 0;
}

// First FAT entry with value 0 (free). Clusters 0 and 1 are reserved.
static uint32_t find_free_cluster() {
    uint8_t buffer[512];
    uint32_t total_fat_sectors = bpb.fat_size_32;

    for (uint32_t s = 0; s < total_fat_sectors; s++) {
        if (ata_read_sector(fat_start_sector + s, buffer) != 0) return 0;
        for (int e = 0; e < 128; e++) {
            uint32_t cluster = s * 128 + e;
            if (cluster < 2) continue;
            if (((uint32_t*)buffer)[e] & 0x0FFFFFFF) continue;
            return cluster;
        }
    }
    return 0; // Disk full
}

// Release every cluster from `cl` to the end of its chain.
static void free_chain(uint32_t cl) {
    while (cl >= 2 && cl < 0x0FFFFFF8) {
        uint32_t next = get_next_cluster(cl);
        set_fat_entry(cl, 0);
        cl = next;
    }
}

// Cut the chain so it holds just enough clusters for `length` bytes and
// release the tail (left behind when a file is rewritten shorter).
static void truncate_chain(uint32_t first, uint32_t length) {
    uint32_t cluster_bytes = sectors_per_cluster * 512;
    uint32_t keep = (length + cluster_bytes - 1) / cluster_bytes;
    if (keep == 0) return; // Callers never truncate to an empty file

    uint32_t cl = first;
    for (uint32_t i = 1; i < keep; i++) {
        cl = get_next_cluster(cl);
        if (cl < 2 || cl >= 0x0FFFFFF8) return; // Chain already shorter
    }
    uint32_t tail = get_next_cluster(cl);
    if (tail < 2 || tail >= 0x0FFFFFF8) return; // Nothing past the end
    if (set_fat_entry(cl, 0x0FFFFFFF) != 0) return;
    free_chain(tail);
}

// The node's `impl` field remembers where its directory entry lives
// (packed by finddir as dir_lba * 16 + entry_index) so writes can update
// the recorded file size and first cluster.
static void update_dir_entry(fs_node_t* node) {
    if (node->impl == 0) return;
    uint32_t lba = node->impl >> 4;
    int idx      = (node->impl & 0xF) * 32;

    uint8_t buffer[512];
    if (ata_read_sector(lba, buffer) != 0) return;

    fat32_entry_t* entry = (fat32_entry_t*)&buffer[idx];
    entry->file_size        = node->length;
    entry->first_cluster_lo = node->inode & 0xFFFF;
    entry->first_cluster_hi = (node->inode >> 16) & 0xFFFF;

    rtc_time_t now;
    rtc_read(&now);
    entry->write_date = entry->last_access_date = rtc_fat_date(&now);
    entry->write_time = rtc_fat_time(&now);

    ata_write_sector(lba, buffer);
}

static uint32_t fat32_write(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    if ((node->flags & 0x7) != FS_FILE) return 0;
    if (size == 0) return 0;

    uint32_t cluster_bytes = sectors_per_cluster * 512;

    // Empty file: allocate its first cluster
    uint32_t cluster = node->inode;
    if (cluster < 2) {
        cluster = find_free_cluster();
        if (!cluster) return 0;
        if (set_fat_entry(cluster, 0x0FFFFFFF) != 0) return 0; // End of chain
        node->inode = cluster;
    }

    // Walk to the cluster containing `offset`, extending the chain on demand
    uint32_t clusters_to_skip = offset / cluster_bytes;
    for (uint32_t i = 0; i < clusters_to_skip; i++) {
        uint32_t next = get_next_cluster(cluster);
        if (next >= 0x0FFFFFF8) {
            next = find_free_cluster();
            if (!next) return 0;
            if (set_fat_entry(cluster, next) != 0) return 0;
            set_fat_entry(next, 0x0FFFFFFF);
        }
        cluster = next;
    }

    // Write sector by sector, read-modify-write for partial sectors
    uint32_t written = 0;
    uint32_t pos     = offset % cluster_bytes;
    uint8_t  sec[512];

    while (written < size) {
        uint32_t sector_in_cluster = pos / 512;
        uint32_t off_in_sector     = pos % 512;
        uint32_t lba = cluster_to_lba(cluster) + sector_in_cluster;

        uint32_t chunk = 512 - off_in_sector;
        if (chunk > size - written) chunk = size - written;

        if (chunk < 512) {
            if (ata_read_sector(lba, sec) != 0) break;
        }
        memory_copy((char*)buffer + written, (char*)sec + off_in_sector, chunk);
        if (ata_write_sector(lba, sec) != 0) break;

        written += chunk;
        pos     += chunk;

        if (pos >= cluster_bytes) { // Crossed into the next cluster
            pos = 0;
            uint32_t next = get_next_cluster(cluster);
            if (next >= 0x0FFFFFF8) {
                next = find_free_cluster();
                if (!next) break;
                if (set_fat_entry(cluster, next) != 0) break;
                set_fat_entry(next, 0x0FFFFFFF);
            }
            cluster = next;
        }
    }

    // echo rewrites whole files from offset 0, so writes define the new
    // size (truncating semantics); clusters past the new end are freed.
    node->length = offset + written;
    truncate_chain(node->inode, node->length);
    update_dir_entry(node);

    return written;
}

static uint32_t fat32_read(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    uint32_t cluster = node->inode; // We use inode to store the first cluster
    uint32_t bytes_read = 0;

    // Clamp reads to the file size — the cluster chain is rounded up to
    // whole clusters, so reading past EOF would return on-disk garbage.
    if ((node->flags & 0x7) == FS_FILE) {
        if (offset >= node->length) return 0;
        if (offset + size > node->length) size = node->length - offset;
    }
    
    // Skip to the correct offset cluster
    uint32_t clusters_to_skip = offset / (sectors_per_cluster * 512);
    for (uint32_t i = 0; i < clusters_to_skip; i++) {
        cluster = get_next_cluster(cluster);
        if (cluster >= 0x0FFFFFF8) return 0;
    }

    uint32_t offset_in_cluster = offset % (sectors_per_cluster * 512);
    uint8_t cluster_buf[512]; // Note: simplified to 1 sector for now, should handle multi-sector clusters

    while (bytes_read < size && cluster < 0x0FFFFFF8) {
        uint32_t lba = cluster_to_lba(cluster);
        for (uint32_t s = 0; s < sectors_per_cluster && bytes_read < size; s++) {
            if (ata_read_sector(lba + s, cluster_buf) != 0) return bytes_read;
            
            uint32_t to_copy = 512 - offset_in_cluster;
            if (to_copy > (size - bytes_read)) to_copy = size - bytes_read;
            
            memory_copy((char*)cluster_buf + offset_in_cluster, (char*)buffer + bytes_read, to_copy);
            bytes_read += to_copy;
            offset_in_cluster = 0; // Only the first sector of the first cluster has offset
        }
        cluster = get_next_cluster(cluster);
    }

    return bytes_read;
}

// ─── Directory entries and long file names (LFN) ─────────────────────────────
//
// A long name is stored as a run of LFN entries (attr 0x0F) right before
// its 8.3 "short" entry, last part first. Each LFN entry holds 13 UCS-2
// characters, its order number (0x40 flags the last part) and a checksum
// of the short name, so a stale run left by an old system can be told
// apart from the real one.

#define ENTRIES_PER_SECTOR 16
#define LFN_PARTS_MAX      10                 // 130 characters
#define FAT_NAME_MAX       128                // incl. NUL (= fs_node name)

// Byte offsets of the 13 UCS-2 characters inside an LFN entry
static const uint8_t lfn_char_off[13] = { 1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30 };

typedef struct { uint32_t lba; int idx; } epos_t;   // sector + entry index

// One live directory entry, as dir_walk hands it to a visitor
typedef struct {
    fat32_entry_t e;                 // Copy of the short entry
    epos_t        pos;               // Where the short entry lives
    epos_t        lfn[LFN_PARTS_MAX];// Where its LFN entries live
    int           lfn_n;
    char          name[FAT_NAME_MAX];// Long name, or the 8.3 display name
} dslot_t;

typedef int (*dvisit_t)(dslot_t* slot, void* ctx);  // nonzero = stop

static uint8_t lfn_checksum(const uint8_t name83[11]) {
    uint8_t sum = 0;
    for (int i = 0; i < 11; i++)
        sum = (uint8_t)(((sum & 1) << 7) + (sum >> 1) + name83[i]);
    return sum;
}

// "FILE    TXT" -> "FILE.TXT"
static void short_display(const uint8_t name83[11], char* out) {
    int pos = 0;
    for (int n = 0; n < 8; n++)
        if (name83[n] != ' ') out[pos++] = name83[n];
    if (name83[8] != ' ') {
        out[pos++] = '.';
        for (int n = 8; n < 11; n++)
            if (name83[n] != ' ') out[pos++] = name83[n];
    }
    out[pos] = '\0';
}

static int name_eq_ci(const char* a, const char* b) {
    for (int i = 0; ; i++) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y) return 0;
        if (x == '\0') return 1;
    }
}

// Visit every live entry of the directory starting at `cluster`, in disk
// order, with long names resolved. Returns the first nonzero visitor
// result, or 0 at the end. Not reentrant (static buffers; the VFS lock
// serializes callers) — visitors must not walk another directory.
static int dir_walk(uint32_t cluster, dvisit_t visit, void* ctx) {
    static uint8_t buf[512];
    static dslot_t cur;
    static char    lname[LFN_PARTS_MAX * 13 + 1];
    int     lfn_next = 0;     // Order number expected next (0 = none)
    int     lfn_done = 0;     // Saw part 1: the run is complete
    uint8_t lfn_sum = 0;
    cur.lfn_n = 0;

    while (cluster >= 2 && cluster < 0x0FFFFFF8) {
        uint32_t lba0 = cluster_to_lba(cluster);
        for (uint32_t s = 0; s < sectors_per_cluster; s++) {
            uint32_t lba = lba0 + s;
            if (ata_read_sector(lba, buf) != 0) return 0;

            for (int i = 0; i < ENTRIES_PER_SECTOR; i++) {
                uint8_t* raw = &buf[i * 32];
                fat32_entry_t* e = (fat32_entry_t*)raw;
                if (raw[0] == 0x00) return 0;               // End of directory
                if (raw[0] == 0xE5) { lfn_next = lfn_done = 0; cur.lfn_n = 0; continue; }

                if (e->attr == 0x0F) {                       // LFN part
                    int ord = raw[0] & 0x1F;
                    if (raw[0] & 0x40) {                     // Last part comes first
                        if (ord == 0 || ord > LFN_PARTS_MAX) { lfn_next = lfn_done = 0; cur.lfn_n = 0; continue; }
                        lfn_sum = raw[13];
                        lname[ord * 13] = '\0';
                        cur.lfn_n = 0;
                        lfn_done = 0;
                    } else if (ord == 0 || ord != lfn_next || raw[13] != lfn_sum) {
                        lfn_next = lfn_done = 0; cur.lfn_n = 0;      // Broken run
                        continue;
                    }
                    for (int k = 0; k < 13; k++) {
                        uint16_t ch = raw[lfn_char_off[k]] | (raw[lfn_char_off[k] + 1] << 8);
                        if (ch == 0x0000) { lname[(ord - 1) * 13 + k] = '\0'; break; }
                        if (ch == 0xFFFF) break;                     // Padding
                        lname[(ord - 1) * 13 + k] = (ch < 0x80) ? (char)ch : '?';
                    }
                    cur.lfn[cur.lfn_n].lba = lba;
                    cur.lfn[cur.lfn_n].idx = i;
                    cur.lfn_n++;
                    lfn_next = ord - 1;
                    lfn_done = (ord == 1);
                    continue;
                }

                if (e->attr & 0x08) { lfn_next = lfn_done = 0; cur.lfn_n = 0; continue; } // Volume label

                // A short entry: the visible one
                cur.e = *e;
                cur.pos.lba = lba;
                cur.pos.idx = i;
                if (lfn_done && lfn_sum == lfn_checksum(e->name)) {
                    int n = 0;
                    while (lname[n] && n < FAT_NAME_MAX - 1) { cur.name[n] = lname[n]; n++; }
                    cur.name[n] = '\0';
                } else {
                    short_display(e->name, cur.name);
                    cur.lfn_n = 0;
                }
                lfn_next = lfn_done = 0;

                int r = visit(&cur, ctx);
                if (r) return r;
                cur.lfn_n = 0;
            }
        }
        cluster = get_next_cluster(cluster);
    }
    return 0;
}

// ─── Lookups ──────────────────────────────────────────────────────────────────

typedef struct { const char* name; dslot_t found; } find_ctx_t;

static int find_visit(dslot_t* s, void* vctx) {
    find_ctx_t* c = (find_ctx_t*)vctx;
    char alias[13];
    short_display(s->e.name, alias);
    // Match the long name or the 8.3 alias, ignoring case
    if (name_eq_ci(s->name, c->name) || name_eq_ci(alias, c->name)) {
        c->found = *s;
        return 1;
    }
    return 0;
}

// Find `name` in the directory at `cluster`; fills *out. Returns 1 if found.
static int dir_find(uint32_t cluster, const char* name, dslot_t* out) {
    static find_ctx_t c;
    c.name = name;
    if (!dir_walk(cluster, find_visit, &c)) return 0;
    *out = c.found;
    return 1;
}

static uint32_t entry_cluster(const fat32_entry_t* e) {
    return ((uint32_t)e->first_cluster_hi << 16) | e->first_cluster_lo;
}

typedef struct { uint32_t index, count; struct dirent* out; } readdir_ctx_t;

static int readdir_visit(dslot_t* s, void* vctx) {
    readdir_ctx_t* c = (readdir_ctx_t*)vctx;
    if (c->count++ != c->index) return 0;
    strcpy(c->out->name, s->name);
    c->out->ino = entry_cluster(&s->e);
    return 1;
}

static struct dirent *fat32_readdir(fs_node_t *node, uint32_t index) {
    static struct dirent de;
    readdir_ctx_t c = { index, 0, &de };
    return dir_walk(node->inode, readdir_visit, &c) ? &de : 0;
}

static fs_node_t *fat32_finddir(fs_node_t *node, char *name) {
    static dslot_t s;
    if (!dir_find(node->inode, name, &s)) return 0;
    fat32_entry_t* entry = &s.e;

    fs_node_t *res = (fs_node_t*)kmalloc(sizeof(fs_node_t));
    memory_set((uint8_t*)res, 0, sizeof(fs_node_t));
    strcpy(res->name, s.name);
    res->inode   = entry_cluster(entry);
    // ".." of a first-level directory points at cluster 0,
    // which FAT32 uses to mean the root directory
    if ((entry->attr & 0x10) && res->inode == 0)
        res->inode = root_dir_cluster;
    res->length  = entry->file_size;
    res->flags   = ((entry->attr & 0x10) ? FS_DIRECTORY : FS_FILE)
                   | FS_ALLOCATED;
    res->read    = fat32_read;
    res->write   = fat32_write;
    res->readdir = fat32_readdir;
    res->finddir = fat32_finddir;
    if (entry->attr & 0x10) {        // subdirectory: allow create/delete
        res->create = fat32_create;
        res->unlink = fat32_delete;
        res->mkdir  = fat32_mkdir;
        res->rename = fat32_rename;
    }
    // Remember where the short entry lives (for writes)
    res->impl    = s.pos.lba * ENTRIES_PER_SECTOR + s.pos.idx;
    return res;
}

static int notdot_visit(dslot_t* s, void* ctx) {
    (void)ctx;
    return !(s->name[0] == '.' && (s->name[1] == '\0' ||
             (s->name[1] == '.' && s->name[2] == '\0')));
}

// 1 if the directory starting at `cluster` holds nothing but "." / "..".
static int dir_is_empty(uint32_t cluster) {
    return !dir_walk(cluster, notdot_visit, 0);
}

// ─── Names ────────────────────────────────────────────────────────────────────

// 1 if `name` fits a short 8.3 entry: 1-8 chars, optional '.' + 1-3 chars,
// no characters FAT forbids in short names.
static int valid_83(const char* name) {
    int base = 0, ext = -1;
    for (const char* p = name; *p; p++) {
        char c = *p;
        if (c == '.') {
            if (ext >= 0) return 0;      // Only one dot
            ext = 0;
            continue;
        }
        if (c <= ' ' || c == '"' || c == '*' || c == '/' || c == ':' ||
            c == '<' || c == '>' || c == '?' || c == '\\' || c == '|' ||
            c == '+' || c == ',' || c == ';' || c == '=' || c == '[' ||
            c == ']' || (uint8_t)c > 126) return 0;
        if (ext >= 0) ext++; else base++;
    }
    return base >= 1 && base <= 8 && ext != 0 && ext <= 3;
}

// 1 if `name` is acceptable as a long name (ASCII; FAT's forbidden
// characters excluded; no trailing dot or space, which Windows strips).
static int valid_long(const char* name) {
    int len = strlen((char*)name);
    if (len == 0 || len >= FAT_NAME_MAX) return 0;
    if (strcmp((char*)name, ".") == 0 || strcmp((char*)name, "..") == 0) return 0;
    for (int i = 0; i < len; i++) {
        char c = name[i];
        if (c < ' ' || c == '"' || c == '*' || c == '/' || c == ':' ||
            c == '<' || c == '>' || c == '?' || c == '\\' || c == '|' ||
            (uint8_t)c > 126) return 0;
    }
    return name[len - 1] != '.' && name[len - 1] != ' ';
}

// A plain 8.3 name in one case is stored as a short entry only (shown in
// upper case, as before). Anything else gets a long name to keep it as typed.
static int needs_lfn(const char* name) {
    if (!valid_83(name)) return 1;
    int lower = 0, upper = 0;
    for (const char* p = name; *p; p++) {
        if (*p >= 'a' && *p <= 'z') lower = 1;
        if (*p >= 'A' && *p <= 'Z') upper = 1;
    }
    return lower && upper;
}

// Convert "notes.txt" -> "NOTES   TXT" (8.3, uppercase, space padded)
static void name_to_83(const char* name, uint8_t out[11]) {
    for (int i = 0; i < 11; i++) out[i] = ' ';
    int i = 0, o = 0;
    while (name[i] && name[i] != '.' && o < 8) {
        char c = name[i++];
        if (c >= 'a' && c <= 'z') c -= 32;
        out[o++] = c;
    }
    while (name[i] && name[i] != '.') i++;
    if (name[i] == '.') {
        i++; o = 8;
        while (name[i] && o < 11) {
            char c = name[i++];
            if (c >= 'a' && c <= 'z') c -= 32;
            out[o++] = c;
        }
    }
}

// Character usable in a generated short alias ('_' replaces the rest)
static char alias_char(char c) {
    if (c >= 'a' && c <= 'z') return c - 32;
    if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return c;
    if (c == '-' || c == '_' || c == '!' || c == '#' || c == '$' || c == '%' ||
        c == '&' || c == '(' || c == ')' || c == '@' || c == '^' || c == '{' ||
        c == '}' || c == '~' || c == '\'' || c == '`') return c;
    return '_';
}

typedef struct { const uint8_t* name83; } alias_ctx_t;

static int alias_visit(dslot_t* s, void* vctx) {
    const uint8_t* want = ((alias_ctx_t*)vctx)->name83;
    for (int i = 0; i < 11; i++)
        if (s->e.name[i] != want[i]) return 0;
    return 1;
}

// Short alias for a long name, unique in `dir`: "My Long Name.text" ->
// "MYLONG~1TEX" (Windows' basis-name + numeric-tail scheme).
static int make_alias(uint32_t dir_cluster, const char* lname, uint8_t out[11]) {
    char basis[9], ext[4];
    int nb = 0, ne = 0;

    int last_dot = -1;
    for (int i = 0; lname[i]; i++) if (lname[i] == '.') last_dot = i;
    if (last_dot == 0) last_dot = -1;             // ".profile": no extension

    for (int i = 0; lname[i] && (last_dot < 0 || i < last_dot) && nb < 8; i++) {
        if (lname[i] == ' ' || lname[i] == '.') continue;
        basis[nb++] = alias_char(lname[i]);
    }
    if (last_dot >= 0)
        for (int i = last_dot + 1; lname[i] && ne < 3; i++) {
            if (lname[i] == ' ' || lname[i] == '.') continue;
            ext[ne++] = alias_char(lname[i]);
        }
    if (nb == 0) basis[nb++] = '_';

    alias_ctx_t c = { out };
    for (int n = 1; n < 1000000; n++) {
        char tail[8];
        tail[0] = '~';
        int_to_ascii(n, tail + 1);
        int tl = strlen(tail);
        int keep = nb < 8 - tl ? nb : 8 - tl;

        for (int i = 0; i < 11; i++) out[i] = ' ';
        for (int i = 0; i < keep; i++) out[i] = basis[i];
        for (int i = 0; i < tl; i++) out[keep + i] = tail[i];
        for (int i = 0; i < ne; i++) out[8 + i] = ext[i];

        if (!dir_walk(dir_cluster, alias_visit, &c)) return 0;   // Unused
    }
    return -1;
}

// ─── Writing entries ──────────────────────────────────────────────────────────

// Fill every sector of a cluster with zeros (a zeroed directory cluster
// reads as "no more entries").
static int zero_cluster(uint32_t cluster) {
    uint8_t zero[512];
    memory_set(zero, 0, 512);
    uint32_t lba = cluster_to_lba(cluster);
    for (uint32_t s = 0; s < sectors_per_cluster; s++)
        if (ata_write_sector(lba + s, zero) != 0) return -1;
    return 0;
}

static void fill_entry(fat32_entry_t* e, const uint8_t name83[11],
                       uint8_t attr, uint32_t first_cluster) {
    memory_set((uint8_t*)e, 0, sizeof(fat32_entry_t));
    for (int n = 0; n < 11; n++) e->name[n] = name83[n];
    e->attr = attr;

    // Created/modified/accessed = now
    rtc_time_t now;
    rtc_read(&now);
    e->creation_date = e->write_date = e->last_access_date = rtc_fat_date(&now);
    e->creation_time = e->write_time = rtc_fat_time(&now);
    e->first_cluster_lo = first_cluster & 0xFFFF;
    e->first_cluster_hi = (first_cluster >> 16) & 0xFFFF;
}

// Read-modify-write one 32-byte entry
static int write_entry_at(epos_t p, const void* entry32) {
    uint8_t buf[512];
    if (ata_read_sector(p.lba, buf) != 0) return -1;
    memory_copy((char*)entry32, (char*)&buf[p.idx * 32], 32);
    return ata_write_sector(p.lba, buf);
}

static int mark_deleted_at(epos_t p) {
    uint8_t buf[512];
    if (ata_read_sector(p.lba, buf) != 0) return -1;
    buf[p.idx * 32] = 0xE5;
    return ata_write_sector(p.lba, buf);
}

// Find `n` consecutive free slots in `dir` (growing it by zeroed clusters
// at the end if needed); positions go to `run`. Returns 0 on success.
static int find_free_run(uint32_t first, int n, epos_t* run) {
    uint8_t buf[512];
    int have = 0;
    uint32_t cluster = first, prev = first;

    while (1) {
        if (cluster < 2 || cluster >= 0x0FFFFFF8) {
            // Past the end: link a fresh cluster (all free) and keep going
            uint32_t newc = find_free_cluster();
            if (!newc) return -1;
            if (set_fat_entry(newc, 0x0FFFFFFF) != 0) return -1;
            if (zero_cluster(newc) != 0) return -1;
            if (set_fat_entry(prev, newc) != 0) return -1;
            cluster = newc;
        }
        uint32_t lba0 = cluster_to_lba(cluster);
        for (uint32_t s = 0; s < sectors_per_cluster; s++) {
            if (ata_read_sector(lba0 + s, buf) != 0) return -1;
            for (int i = 0; i < ENTRIES_PER_SECTOR; i++) {
                uint8_t first_byte = buf[i * 32];
                if (first_byte == 0x00 || first_byte == 0xE5) {
                    run[have].lba = lba0 + s;
                    run[have].idx = i;
                    if (++have == n) return 0;
                } else {
                    have = 0;           // Must be consecutive
                }
            }
        }
        prev = cluster;
        cluster = get_next_cluster(cluster);
    }
}

// Add `name` to `dir`: a short entry, preceded by LFN entries when the
// name needs them. `tmpl` (optional) supplies size and timestamps (rename).
// Returns 0, -1 on disk error/full, VFS_EBADNAME for an invalid name.
static int add_entry(fs_node_t* dir, const char* name, uint8_t attr,
                     uint32_t first_cluster, const fat32_entry_t* tmpl) {
    uint8_t name83[11];
    int parts = 0;

    if (!needs_lfn(name)) {
        name_to_83(name, name83);
    } else {
        if (!valid_long(name)) return VFS_EBADNAME;
        if (make_alias(dir->inode, name, name83) != 0) return -1;
        parts = (strlen((char*)name) + 12) / 13;
        if (parts > LFN_PARTS_MAX) return VFS_EBADNAME;
    }

    epos_t run[LFN_PARTS_MAX + 1];
    if (find_free_run(dir->inode, parts + 1, run) != 0) return -1;

    // LFN entries: highest part first, flagged 0x40
    uint8_t sum = lfn_checksum(name83);
    int len = strlen((char*)name);
    for (int k = 0; k < parts; k++) {
        int ord = parts - k;
        uint8_t l[32];
        memory_set(l, 0, 32);
        l[0]  = (uint8_t)(ord | (k == 0 ? 0x40 : 0));
        l[11] = 0x0F;
        l[13] = sum;
        for (int c = 0; c < 13; c++) {
            int src = (ord - 1) * 13 + c;
            uint16_t ch = (src < len) ? (uint8_t)name[src]
                        : (src == len) ? 0x0000 : 0xFFFF;
            l[lfn_char_off[c]]     = ch & 0xFF;
            l[lfn_char_off[c] + 1] = ch >> 8;
        }
        if (write_entry_at(run[k], l) != 0) return -1;
    }

    fat32_entry_t e;
    fill_entry(&e, name83, attr, first_cluster);
    if (tmpl) {
        e.file_size        = tmpl->file_size;
        e.creation_date    = tmpl->creation_date;
        e.creation_time    = tmpl->creation_time;
        e.creation_time_tenth = tmpl->creation_time_tenth;
        e.write_date       = tmpl->write_date;
        e.write_time       = tmpl->write_time;
    }
    return write_entry_at(run[parts], &e) == 0 ? 0 : -1;
}

static int remove_slot(const dslot_t* s) {
    for (int i = 0; i < s->lfn_n; i++)
        if (mark_deleted_at(s->lfn[i]) != 0) return -1;
    return mark_deleted_at(s->pos);
}

// ─── VFS operations ───────────────────────────────────────────────────────────

// Create an empty file in directory `dir`. Returns 0 on success.
static int fat32_create(fs_node_t* dir, char* name) {
    static dslot_t s;
    if (dir_find(dir->inode, name, &s)) return -1;   // Refuse duplicates
    return add_entry(dir, name, 0x20 /* Archive */, 0, 0);
}

// Create a subdirectory: one zeroed cluster holding "." and "..".
static int fat32_mkdir(fs_node_t* dir, char* name) {
    static dslot_t s;
    if (dir_find(dir->inode, name, &s)) return -1;
    if (needs_lfn(name) && !valid_long(name)) return VFS_EBADNAME;

    uint32_t c = find_free_cluster();
    if (!c) return -1;
    if (set_fat_entry(c, 0x0FFFFFFF) != 0) return -1;
    if (zero_cluster(c) != 0) { set_fat_entry(c, 0); return -1; }

    // ".." of a directory directly under the root records cluster 0
    uint32_t parent = (dir->inode == root_dir_cluster) ? 0 : dir->inode;
    uint8_t dot[11], dotdot[11];
    for (int n = 0; n < 11; n++) dot[n] = dotdot[n] = ' ';
    dot[0] = '.';
    dotdot[0] = dotdot[1] = '.';

    uint8_t buffer[512];
    memory_set(buffer, 0, 512);
    fill_entry((fat32_entry_t*)&buffer[0],  dot,    0x10, c);
    fill_entry((fat32_entry_t*)&buffer[32], dotdot, 0x10, parent);
    int r = -1;
    if (ata_write_sector(cluster_to_lba(c), buffer) != 0 ||
        (r = add_entry(dir, name, 0x10, c, 0)) != 0) {
        free_chain(c);
        return r;
    }
    return 0;
}

// Delete a file or an empty subdirectory from `dir`. Returns 0 on
// success, -1 if not found, -2 if it is a directory that is not empty.
static int fat32_delete(fs_node_t* dir, char* name) {
    static dslot_t s;
    if (!dir_find(dir->inode, name, &s)) return -1;
    if (strcmp(s.name, ".") == 0 || strcmp(s.name, "..") == 0) return -1;

    uint32_t first = entry_cluster(&s.e);
    if ((s.e.attr & 0x10) && !dir_is_empty(first)) return -2;
    free_chain(first);
    return remove_slot(&s);
}

// Parent cluster of directory `cl` (via its ".." entry); root -> 0.
static uint32_t parent_of(uint32_t cl) {
    static dslot_t s;
    if (cl == root_dir_cluster) return 0;
    if (!dir_find(cl, "..", &s)) return 0;
    uint32_t p = entry_cluster(&s.e);
    return p ? p : root_dir_cluster;
}

// Move an entry (and its long name) to `dst_dir/dst_name`. The data and
// cluster chain stay where they are; a moved directory's ".." follows.
static int fat32_rename(fs_node_t* src_dir, char* src_name,
                        fs_node_t* dst_dir, char* dst_name) {
    static dslot_t src, other;
    if (!dir_find(src_dir->inode, src_name, &src)) return -1;
    if (strcmp(src.name, ".") == 0 || strcmp(src.name, "..") == 0) return VFS_EINVAL;

    // Renaming to a different case of the same name hits `src` itself
    if (dir_find(dst_dir->inode, dst_name, &other) &&
        !(other.pos.lba == src.pos.lba && other.pos.idx == src.pos.idx))
        return -1;                                   // Target exists

    uint32_t first = entry_cluster(&src.e);
    int is_dir = src.e.attr & 0x10;

    // A directory can't move into itself or below itself
    if (is_dir) {
        uint32_t cl = dst_dir->inode;
        for (int depth = 0; depth < 64 && cl; depth++) {
            if (cl == first) return VFS_EINVAL;
            if (cl == root_dir_cluster) break;
            cl = parent_of(cl);
        }
    }

    int r = add_entry(dst_dir, dst_name, src.e.attr, first, &src.e);
    if (r != 0) return r;
    if (remove_slot(&src) != 0) return -1;

    if (is_dir && src_dir->inode != dst_dir->inode) {
        static dslot_t dd;
        if (dir_find(first, "..", &dd)) {
            uint32_t parent = (dst_dir->inode == root_dir_cluster) ? 0 : dst_dir->inode;
            dd.e.first_cluster_lo = parent & 0xFFFF;
            dd.e.first_cluster_hi = (parent >> 16) & 0xFFFF;
            write_entry_at(dd.pos, &dd.e);
        }
    }
    return 0;
}

// The FSInfo sector caches the free-cluster count and a next-free hint.
// We allocate and free clusters without maintaining them, so mark both
// "unknown" (0xFFFFFFFF, allowed by the spec): other systems then
// recompute them instead of trusting a stale count.
static void invalidate_fsinfo() {
    uint16_t sector = bpb.fs_info_sector;
    if (sector == 0 || sector == 0xFFFF) return;

    uint8_t buffer[512];
    if (ata_read_sector(sector, buffer) != 0) return;
    if (*(uint32_t*)&buffer[0]   != 0x41615252 ||   // "RRaA"
        *(uint32_t*)&buffer[484] != 0x61417272) return; // "rrAa"

    uint32_t* free_count = (uint32_t*)&buffer[488];
    uint32_t* next_free  = (uint32_t*)&buffer[492];
    if (*free_count == 0xFFFFFFFF && *next_free == 0xFFFFFFFF) return;
    *free_count = 0xFFFFFFFF;
    *next_free  = 0xFFFFFFFF;
    ata_write_sector(sector, buffer);
}

fs_node_t* fat32_init() {
    uint8_t buffer[512];
    if (!ata_identify()) return 0; // No ATA disk attached
    if (ata_read_sector(0, buffer) != 0) return 0;

    memory_copy((char*)buffer, (char*)&bpb, sizeof(fat32_bpb_t));

    // Basic validation (look for 0x55AA)
    if (buffer[510] != 0x55 || buffer[511] != 0xAA) return 0;

    fat_start_sector = bpb.reserved_sector_count;
    data_start_sector = fat_start_sector + (bpb.num_fats * bpb.fat_size_32);
    sectors_per_cluster = bpb.sectors_per_cluster;
    root_dir_cluster = bpb.root_cluster;

    invalidate_fsinfo();

    // Create VFS root node for FAT32
    fat32_root_node = (fs_node_t*)kmalloc(sizeof(fs_node_t));
    memory_set((uint8_t*)fat32_root_node, 0, sizeof(fs_node_t));
    strcpy(fat32_root_node->name, "fat32_root");
    fat32_root_node->flags = FS_DIRECTORY;
    fat32_root_node->inode = root_dir_cluster;
    fat32_root_node->read = 0;
    fat32_root_node->write = 0;
    fat32_root_node->readdir = fat32_readdir;
    fat32_root_node->finddir = fat32_finddir;
    fat32_root_node->create  = fat32_create;
    fat32_root_node->unlink  = fat32_delete;
    fat32_root_node->mkdir   = fat32_mkdir;
    fat32_root_node->rename  = fat32_rename;

    return fat32_root_node;
}
