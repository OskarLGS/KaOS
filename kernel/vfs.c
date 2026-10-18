#include "vfs.h"
#include "memory.h"
#include "util.h"

fs_node_t *fs_root = 0; // The root of the filesystem.

uint32_t read_fs(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    if (node->read != 0) {
        return node->read(node, offset, size, buffer);
    } else {
        return 0;
    }
}

uint32_t write_fs(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    if (node->write != 0) {
        return node->write(node, offset, size, buffer);
    } else {
        return 0;
    }
}

void open_fs(fs_node_t *node) {
    if (node->open != 0) {
        node->open(node);
    }
}

void close_fs(fs_node_t *node) {
    if (node->close != 0) {
        node->close(node);
    }
}

struct dirent *readdir_fs(fs_node_t *node, uint32_t index) {
    if ((node->flags & 0x7) == FS_DIRECTORY && node->readdir != 0) {
        return node->readdir(node, index);
    } else {
        return 0;
    }
}

fs_node_t *finddir_fs(fs_node_t *node, char *name) {
    if ((node->flags & 0x7) == FS_DIRECTORY && node->finddir != 0) {
        return node->finddir(node, name);
    } else {
        return 0;
    }
}

int create_fs(fs_node_t *node, char *name) {
    if ((node->flags & 0x7) == FS_DIRECTORY && node->create != 0) {
        return node->create(node, name);
    }
    return -1;
}

int unlink_fs(fs_node_t *node, char *name) {
    if ((node->flags & 0x7) == FS_DIRECTORY && node->unlink != 0) {
        return node->unlink(node, name);
    }
    return -1;
}

void vfs_release(fs_node_t *node) {
    if (node && (node->flags & FS_ALLOCATED)) kfree((uint32_t)node);
}

// Walk `path` from fs_root, one finddir per component, releasing the
// intermediate nodes. With stop_before_last, stop at the parent of the
// final component and copy that component's name into `leaf`.
static fs_node_t *walk(const char *path, int stop_before_last, char *leaf) {
    char comp[VFS_PATH_MAX];
    fs_node_t *node = fs_root;
    const char *p = path;

    while (1) {
        while (*p == '/') p++;
        if (*p == '\0') break;

        int n = 0;
        while (*p && *p != '/') {
            if (n >= VFS_PATH_MAX - 1) { if (node != fs_root) vfs_release(node); return 0; }
            comp[n++] = *p++;
        }
        comp[n] = '\0';

        // Is this the final component? (only slashes may follow)
        const char *q = p;
        while (*q == '/') q++;
        if (stop_before_last && *q == '\0') {
            strcpy(leaf, comp);
            return node;
        }

        fs_node_t *next = finddir_fs(node, comp);
        if (node != fs_root) vfs_release(node);
        if (!next) return 0;
        node = next;
    }
    if (stop_before_last) return 0; // Empty path: no leaf to split off
    return node;
}

fs_node_t *vfs_resolve(const char *path) {
    return walk(path, 0, 0);
}

fs_node_t *vfs_resolve_parent(const char *path, char *leaf) {
    fs_node_t *dir = walk(path, 1, leaf);
    if (dir && (dir->flags & 0x7) != FS_DIRECTORY) {
        if (dir != fs_root) vfs_release(dir);
        return 0;
    }
    return dir;
}
