#include "ramfs.h"
#include "util.h"
#include "memory.h"
#include "fat32.h"

// Define a few static structures for our simple RAM disk
fs_node_t *ramfs_root;
fs_node_t *ramfs_dev;
fs_node_t *fat32_dev;
fs_node_t *ramfs_files[2]; 
struct dirent dirent;

// File 1 data
char file1_data[512] = "Hello from KaOS! This is a simple text file in RAM.\n";
uint32_t file1_size = 52;

// File 2 data
char file2_data[512] = "";
uint32_t file2_size = 0;

uint32_t ramfs_read(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    char *data;
    uint32_t max_size;
    if (node->inode == 1) {
        data = file1_data;
        max_size = file1_size;
    } else if (node->inode == 2) {
        data = file2_data;
        max_size = file2_size;
    } else {
        return 0; // Not found
    }

    if (offset >= max_size) return 0;
    
    uint32_t read_size = size;
    if (offset + size > max_size) {
        read_size = max_size - offset;
    }

    memory_copy(data + offset, (char*)buffer, read_size);
    return read_size;
}

uint32_t ramfs_write(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    char *data;
    if (node->inode == 1) {
        data = file1_data;
    } else if (node->inode == 2) {
        data = file2_data;
    } else {
        return 0;
    }

    // Limit write to 512 bytes for our simple ramfs
    uint32_t write_size = size;
    if (offset + size > 512) {
        write_size = 512 - offset;
    }

    memory_copy((char*)buffer, data + offset, write_size);
    
    // Update size if we appended
    if (node->inode == 1 && offset + write_size > file1_size) file1_size = offset + write_size;
    if (node->inode == 2 && offset + write_size > file2_size) file2_size = offset + write_size;

    node->length = (node->inode == 1) ? file1_size : file2_size;
    return write_size;
}

struct dirent *ramfs_readdir(fs_node_t *node, uint32_t index) {
    if (node == ramfs_root && index == 0) {
        memory_copy("readme.txt\0", dirent.name, 11);
        dirent.ino = 1;
        return &dirent;
    } else if (node == ramfs_root && index == 1) {
        memory_copy("user.txt\0", dirent.name, 9);
        dirent.ino = 2;
        return &dirent;
    } else if (node == ramfs_root && index == 2) {
        memory_copy("disk\0", dirent.name, 5);
        dirent.ino = 3; // Inode 3 for FAT32 mount point
        return &dirent;
    }
    return 0;
}

fs_node_t *ramfs_finddir(fs_node_t *node, char *name) {
    if (node == ramfs_root && strcmp(name, "readme.txt") == 0) {
        return ramfs_files[0];
    } else if (node == ramfs_root && strcmp(name, "user.txt") == 0) {
        return ramfs_files[1];
    } else if (node == ramfs_root && strcmp(name, "disk") == 0) {
        return fat32_dev;
    }
    return 0;
}

void init_ramfs() {
    // We allocate these using our memory allocator
    ramfs_root = (fs_node_t*)kmalloc(sizeof(fs_node_t));
    ramfs_files[0] = (fs_node_t*)kmalloc(sizeof(fs_node_t));
    ramfs_files[1] = (fs_node_t*)kmalloc(sizeof(fs_node_t));
    // kmalloc doesn't zero: clear every field so ops we don't set here
    // (create, unlink, mkdir, ...) are null instead of garbage pointers
    memory_set((uint8_t*)ramfs_root, 0, sizeof(fs_node_t));
    memory_set((uint8_t*)ramfs_files[0], 0, sizeof(fs_node_t));
    memory_set((uint8_t*)ramfs_files[1], 0, sizeof(fs_node_t));

    // Initialize root directory
    memory_copy("ramfs\0", ramfs_root->name, 6);
    ramfs_root->flags = FS_DIRECTORY;
    ramfs_root->read = 0;
    ramfs_root->write = 0;
    ramfs_root->open = 0;
    ramfs_root->close = 0;
    ramfs_root->readdir = &ramfs_readdir;
    ramfs_root->finddir = &ramfs_finddir;
    ramfs_root->create = 0; // RAMFS is read-only for creation
    ramfs_root->unlink = 0;

    // Initialize README.TXT
    memory_copy("readme.txt\0", ramfs_files[0]->name, 11);
    ramfs_files[0]->flags = FS_FILE;
    ramfs_files[0]->inode = 1;
    ramfs_files[0]->length = file1_size;
    ramfs_files[0]->read = &ramfs_read;
    ramfs_files[0]->write = &ramfs_write; // Read-only conceptually, but allow writes for testing
    
    // Initialize USER.TXT
    memory_copy("user.txt\0", ramfs_files[1]->name, 9);
    ramfs_files[1]->flags = FS_FILE;
    ramfs_files[1]->inode = 2;
    ramfs_files[1]->length = file2_size;
    ramfs_files[1]->read = &ramfs_read;
    ramfs_files[1]->write = &ramfs_write;

    // Initialize FAT32 mount point
    fat32_dev = fat32_init();
    if (fat32_dev) {
        memory_copy("disk\0", fat32_dev->name, 5);
        fat32_dev->flags = FS_DIRECTORY | FS_MOUNTPOINT;
    }

    // Set root node in VFS
    fs_root = ramfs_root;
}
