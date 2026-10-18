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

// Create an empty file in directory `dir`. Returns 0 on success.
static int fat32_create(fs_node_t* dir, char* name) {
    // Refuse duplicates
    fs_node_t* existing = fat32_finddir(dir, name);
    if (existing) { kfree((uint32_t)existing); return -1; }

    uint8_t entry83[11];
    name_to_83(name, entry83);

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
                    // Free slot — write the new entry here
                    fat32_entry_t* e = (fat32_entry_t*)&buffer[i];
                    memory_set((uint8_t*)e, 0, sizeof(fat32_entry_t));
                    for (int n = 0; n < 11; n++) e->name[n] = entry83[n];
                    e->attr = 0x20; // Archive
                    e->first_cluster_lo = 0;
                    e->first_cluster_hi = 0;
                    e->file_size = 0;
                    if (ata_write_sector(lba + s, buffer) != 0) return -1;
                    return 0;
                }
            }
        }
        prev_cluster = cluster;
        cluster = get_next_cluster(cluster);
    }

    // Directory full: allocate and link a fresh cluster, write the entry first
    uint32_t newc = find_free_cluster();
    if (!newc) return -1;
    if (set_fat_entry(prev_cluster, newc) != 0) return -1;
    if (set_fat_entry(newc, 0x0FFFFFFF) != 0) return -1;

    memory_set(buffer, 0, 512); // Zeroed cluster => rest stays end-of-dir
    fat32_entry_t* e = (fat32_entry_t*)&buffer[0];
    for (int n = 0; n < 11; n++) e->name[n] = entry83[n];
    e->attr = 0x20;
    uint32_t lba = cluster_to_lba(newc);
    if (ata_write_sector(lba, buffer) != 0) return -1;
    return 0;
}

// Delete a file from directory `dir`. Returns 0 on success.
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
                if (e->attr & 0x10) continue;       // Directory — files only

                int match = 1;
                for (int n = 0; n < 11; n++)
                    if (e->name[n] != want[n]) { match = 0; break; }
                if (!match) continue;

                free_chain((e->first_cluster_hi << 16) | e->first_cluster_lo);

                e->name[0] = 0xE5; // Mark deleted
                if (ata_write_sector(lba + s, buffer) != 0) return -1;
                return 0;
            }
        }
        cluster = get_next_cluster(cluster);
    }
    return -1;
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

    return fat32_root_node;
}
