#ifndef VFS_H
#define VFS_H

#include <stdint.h>

#define FS_FILE        0x01
#define FS_DIRECTORY   0x02
#define FS_CHARDEVICE  0x03
#define FS_BLOCKDEVICE 0x04
#define FS_PIPE        0x05
#define FS_SYMLINK     0x06
#define FS_MOUNTPOINT  0x08
#define FS_ALLOCATED   0x80  // Node was kmalloc'd by finddir: vfs_release frees it

struct fs_node;

// Function pointer types for filesystem operations
typedef uint32_t (*read_type_t)(struct fs_node*, uint32_t, uint32_t, uint8_t*);
typedef uint32_t (*write_type_t)(struct fs_node*, uint32_t, uint32_t, uint8_t*);
typedef void (*open_type_t)(struct fs_node*);
typedef void (*close_type_t)(struct fs_node*);
typedef struct dirent * (*readdir_type_t)(struct fs_node*, uint32_t);
typedef struct fs_node * (*finddir_type_t)(struct fs_node*, char *name);
typedef int (*create_type_t)(struct fs_node*, char *name);
typedef int (*unlink_type_t)(struct fs_node*, char *name);
typedef int (*mkdir_type_t)(struct fs_node*, char *name);
typedef int (*rename_type_t)(struct fs_node* src_dir, char* src_name,
                             struct fs_node* dst_dir, char* dst_name);

typedef struct fs_node {
    char name[128];       // The requested name.
    uint32_t mask;        // The permissions mask.
    uint32_t uid;         // The owning user.
    uint32_t gid;         // The owning group.
    uint32_t flags;       // Includes the node type.
    uint32_t inode;       // This is device-specific.
    uint32_t length;      // Size of the file, in bytes.
    uint32_t impl;        // Implementation-defined number.
    read_type_t read;
    write_type_t write;
    open_type_t open;
    close_type_t close;
    readdir_type_t readdir;
    finddir_type_t finddir;
    create_type_t create;  // Make an empty child entry (directories).
    unlink_type_t unlink;  // Remove a child entry (directories).
    mkdir_type_t mkdir;    // Make an empty subdirectory (directories).
    rename_type_t rename;  // Move/rename a child within this filesystem.
    struct fs_node *ptr;  // Used by mountpoints and symlinks.
} fs_node_t;

struct dirent {
    char name[128]; 
    uint32_t ino;   // Inode number.
};

extern fs_node_t *fs_root; // The root of the filesystem.

// Standard read/write/open/close functions. Note that these are all suffixed with
// _fs to distinguish them from standard library functions.
uint32_t read_fs(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer);
uint32_t write_fs(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer);
void open_fs(fs_node_t *node);
void close_fs(fs_node_t *node);
struct dirent *readdir_fs(fs_node_t *node, uint32_t index);
fs_node_t *finddir_fs(fs_node_t *node, char *name);
int create_fs(fs_node_t *node, char *name);
int unlink_fs(fs_node_t *node, char *name);
int mkdir_fs(fs_node_t *node, char *name);
// Move `src_dir/src_name` to `dst_dir/dst_name` without copying data.
// VFS_EXDEV if the two directories are on different filesystems.
int rename_fs(fs_node_t *src_dir, char *src_name, fs_node_t *dst_dir, char *dst_name);

// create/mkdir return 0 on success, -1 if it exists/unsupported/disk
// error, VFS_EBADNAME if the name does not fit the filesystem (8.3).
#define VFS_EBADNAME (-3)
#define VFS_EXDEV    (-4)   // rename across filesystems: copy + delete instead
#define VFS_EINVAL   (-5)   // e.g. moving a directory into itself

#define VFS_PATH_MAX 128

// Walk a path like "disk/docs/notes.txt" from fs_root (any number of
// levels, leading/repeated slashes ignored). Returns the node, or 0.
// Release the result with vfs_release() when done.
fs_node_t *vfs_resolve(const char *path);
// Resolve everything but the last component: returns the parent
// directory and copies the leaf name into `leaf` (VFS_PATH_MAX bytes).
fs_node_t *vfs_resolve_parent(const char *path, char *leaf);
// Free a node returned by finddir/vfs_resolve if it was heap-allocated.
void vfs_release(fs_node_t *node);

#endif
