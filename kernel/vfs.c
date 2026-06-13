#include "vfs.h"

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
