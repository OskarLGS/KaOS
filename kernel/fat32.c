#include "fat32.h"
#include "ata.h"
#include "memory.h"
#include "util.h"
#include "screen.h"

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

static struct dirent *fat32_readdir(fs_node_t *node, uint32_t index) {
    uint32_t cluster = node->inode;
    uint32_t count = 0;
    static struct dirent de;
    uint8_t buffer[512];

    while (cluster < 0x0FFFFFF8) {
        uint32_t lba = cluster_to_lba(cluster);
        for (uint32_t s = 0; s < sectors_per_cluster; s++) {
            if (ata_read_sector(lba + s, buffer) != 0) return 0;
            
            for (int i = 0; i < 512; i += 32) {
                fat32_entry_t *entry = (fat32_entry_t*)&buffer[i];
                if (entry->name[0] == 0x00) return 0; // End of entries
                if (entry->name[0] == 0xE5) continue; // Deleted
                if (entry->attr == 0x0F) continue;    // LFN not supported yet
                if (entry->attr & 0x08) continue;     // Volume label

                if (count == index) {
                    // Convert 8.3 name to string
                    int pos = 0;
                    for (int n = 0; n < 8; n++) {
                        if (entry->name[n] != ' ') de.name[pos++] = entry->name[n];
                    }
                    if (entry->name[8] != ' ') {
                        de.name[pos++] = '.';
                        for (int n = 8; n < 11; n++) {
                            if (entry->name[n] != ' ') de.name[pos++] = entry->name[n];
                        }
                    }
                    de.name[pos] = '\0';
                    de.ino = (entry->first_cluster_hi << 16) | entry->first_cluster_lo;
                    return &de;
                }
                count++;
            }
        }
        cluster = get_next_cluster(cluster);
    }
    return 0;
}

// Build the entry's display name ("FILE.TXT") and compare case-insensitively
// (8.3 names are stored uppercase; users type lowercase).
static int fat83_matches(fat32_entry_t* entry, const char* name) {
    char disp[13];
    int pos = 0;
    for (int n = 0; n < 8; n++)
        if (entry->name[n] != ' ') disp[pos++] = entry->name[n];
    if (entry->name[8] != ' ') {
        disp[pos++] = '.';
        for (int n = 8; n < 11; n++)
            if (entry->name[n] != ' ') disp[pos++] = entry->name[n];
    }
    disp[pos] = '\0';

    for (int i = 0; ; i++) {
        char a = disp[i], b = name[i];
        if (a >= 'A' && a <= 'Z') a += 32;
        if (b >= 'A' && b <= 'Z') b += 32;
        if (a != b) return 0;
        if (a == '\0') return 1;
    }
}

static fs_node_t *fat32_finddir(fs_node_t *node, char *name) {
    // Scan directory entries directly (instead of going through readdir)
    // so we keep the attribute byte and file size for the new node.
    uint32_t cluster = node->inode;
    uint8_t buffer[512];

    while (cluster >= 2 && cluster < 0x0FFFFFF8) {
        uint32_t lba = cluster_to_lba(cluster);
        for (uint32_t s = 0; s < sectors_per_cluster; s++) {
            if (ata_read_sector(lba + s, buffer) != 0) return 0;

            for (int i = 0; i < 512; i += 32) {
                fat32_entry_t *entry = (fat32_entry_t*)&buffer[i];
                if (entry->name[0] == 0x00) return 0; // End of entries
                if (entry->name[0] == 0xE5) continue; // Deleted
                if (entry->attr == 0x0F) continue;    // LFN not supported yet
                if (entry->attr & 0x08) continue;     // Volume label
                if (!fat83_matches(entry, name)) continue;

                fs_node_t *res = (fs_node_t*)kmalloc(sizeof(fs_node_t));
                memory_set((uint8_t*)res, 0, sizeof(fs_node_t));
                strcpy(res->name, name);
                res->inode   = (entry->first_cluster_hi << 16) | entry->first_cluster_lo;
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
                }
                // Remember where this directory entry lives (for writes)
                res->impl    = (lba + s) * 16 + (i / 32);
                return res;
            }
        }
        cluster = get_next_cluster(cluster);
    }
    return 0;
}

// 1 if `name` fits a short 8.3 entry: 1-8 chars, optional '.' + 1-3 chars,
// no characters FAT forbids. Longer names would be silently truncated
// (and then never found again), so create/mkdir refuse them until LFN.
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
            c == ']') return 0;
        if (ext >= 0) ext++; else base++;
    }
    return base >= 1 && base <= 8 && ext != 0 && ext <= 3;
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
    e->first_cluster_lo = first_cluster & 0xFFFF;
    e->first_cluster_hi = (first_cluster >> 16) & 0xFFFF;
}

// Write a new entry into the first free slot of `dir`, growing the
// directory by one cluster when it is full. Returns 0 on success.
static int add_entry(fs_node_t* dir, const uint8_t name83[11],
                     uint8_t attr, uint32_t first_cluster) {
    uint8_t buffer[512];
    uint32_t cluster = dir->inode;
    uint32_t prev_cluster = cluster;

    while (cluster >= 2 && cluster < 0x0FFFFFF8) {
        uint32_t lba = cluster_to_lba(cluster);
        for (uint32_t s = 0; s < sectors_per_cluster; s++) {
            if (ata_read_sector(lba + s, buffer) != 0) return -1;
            for (int i = 0; i < 512; i += 32) {
                uint8_t first = buffer[i];
                if (first == 0x00 || first == 0xE5) {
                    fill_entry((fat32_entry_t*)&buffer[i], name83, attr, first_cluster);
                    return ata_write_sector(lba + s, buffer) == 0 ? 0 : -1;
                }
            }
        }
        prev_cluster = cluster;
        cluster = get_next_cluster(cluster);
    }

    // Directory full: allocate and link a fresh, fully zeroed cluster
    uint32_t newc = find_free_cluster();
    if (!newc) return -1;
    if (set_fat_entry(newc, 0x0FFFFFFF) != 0) return -1;
    if (zero_cluster(newc) != 0) return -1;
    if (set_fat_entry(prev_cluster, newc) != 0) return -1;

    memory_set(buffer, 0, 512);
    fill_entry((fat32_entry_t*)&buffer[0], name83, attr, first_cluster);
    return ata_write_sector(cluster_to_lba(newc), buffer) == 0 ? 0 : -1;
}

// Create an empty file in directory `dir`. Returns 0 on success.
static int fat32_create(fs_node_t* dir, char* name) {
    // Refuse duplicates
    fs_node_t* existing = fat32_finddir(dir, name);
    if (existing) { kfree((uint32_t)existing); return -1; }
    if (!valid_83(name)) return -3;

    uint8_t entry83[11];
    name_to_83(name, entry83);
    return add_entry(dir, entry83, 0x20 /* Archive */, 0);
}

// Create a subdirectory: one zeroed cluster holding "." and "..".
static int fat32_mkdir(fs_node_t* dir, char* name) {
    fs_node_t* existing = fat32_finddir(dir, name);
    if (existing) { kfree((uint32_t)existing); return -1; }

    if (!valid_83(name)) return -3;

    uint8_t entry83[11];
    name_to_83(name, entry83);

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
    if (ata_write_sector(cluster_to_lba(c), buffer) != 0 ||
        add_entry(dir, entry83, 0x10, c) != 0) {
        free_chain(c);
        return -1;
    }
    return 0;
}

// 1 if the directory starting at `cluster` holds nothing but "." / "..".
static int dir_is_empty(uint32_t cluster) {
    uint8_t buffer[512];
    while (cluster >= 2 && cluster < 0x0FFFFFF8) {
        uint32_t lba = cluster_to_lba(cluster);
        for (uint32_t s = 0; s < sectors_per_cluster; s++) {
            if (ata_read_sector(lba + s, buffer) != 0) return 0;
            for (int i = 0; i < 512; i += 32) {
                fat32_entry_t* e = (fat32_entry_t*)&buffer[i];
                if (e->name[0] == 0x00) return 1;
                if (e->name[0] == 0xE5 || e->name[0] == '.') continue;
                if (e->attr == 0x0F) continue; // LFN fragment
                return 0;
            }
        }
        cluster = get_next_cluster(cluster);
    }
    return 1;
}

// Delete a file or an empty subdirectory from `dir`. Returns 0 on
// success, -1 if not found, -2 if it is a directory that is not empty.
static int fat32_delete(fs_node_t* dir, char* name) {
    uint8_t want[11];
    name_to_83(name, want);

    uint8_t buffer[512];
    uint32_t cluster = dir->inode;

    while (cluster >= 2 && cluster < 0x0FFFFFF8) {
        uint32_t lba = cluster_to_lba(cluster);
        for (uint32_t s = 0; s < sectors_per_cluster; s++) {
            if (ata_read_sector(lba + s, buffer) != 0) return -1;
            for (int i = 0; i < 512; i += 32) {
                fat32_entry_t* e = (fat32_entry_t*)&buffer[i];
                if (e->name[0] == 0x00) return -1; // End of dir, not found
                if (e->name[0] == 0xE5) continue;
                if (e->attr == 0x0F) continue;      // LFN

                int match = 1;
                for (int n = 0; n < 11; n++)
                    if (e->name[n] != want[n]) { match = 0; break; }
                if (!match) continue;

                uint32_t first = (e->first_cluster_hi << 16) | e->first_cluster_lo;
                if ((e->attr & 0x10) && !dir_is_empty(first)) return -2;
                free_chain(first);

                e->name[0] = 0xE5; // Mark deleted
                if (ata_write_sector(lba + s, buffer) != 0) return -1;
                return 0;
            }
        }
        cluster = get_next_cluster(cluster);
    }
    return -1;
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

    return fat32_root_node;
}
