#include "../shell.h"
#include "../vfs.h"
#include "../util.h"
#include "../screen.h"

static char* skip_spaces(char* s) {
    while (*s == ' ') s++;
    return s;
}

static void cmd_ls(char* args) {
    char *path = skip_spaces(args);

    fs_node_t *dir = vfs_resolve(path);
    if (!dir || (dir->flags & 0x7) != FS_DIRECTORY) {
        print_string("Directory not found.\n");
        vfs_release(dir);
        return;
    }

    int i = 0;
    struct dirent *node = 0;
    while ((node = readdir_fs(dir, i)) != 0) {
        fs_node_t *fsnode = finddir_fs(dir, node->name);
        if (fsnode && (fsnode->flags & 0x7) == FS_DIRECTORY) {
            print_string("(dir)  ");
        } else {
            print_string("(file) ");
        }
        vfs_release(fsnode);
        print_string(node->name);
        print_string("\n");
        i++;
    }
    vfs_release(dir);
}

static void cmd_cat(char* args) {
    char *path = skip_spaces(args);
    fs_node_t *node = vfs_resolve(path);

    if (!node || (node->flags & 0x7) != FS_FILE) {
        print_string("File not found.\n");
        vfs_release(node);
        return;
    }

    // Print the whole file in 512-byte chunks
    uint8_t buffer[513]; // 512 of data + NUL terminator
    uint32_t offset = 0, sz;
    char last = '\n';
    while ((sz = read_fs(node, offset, 512, buffer)) > 0) {
        buffer[sz] = '\0';
        print_string((char*)buffer);
        last = buffer[sz - 1];
        offset += sz;
    }
    if (last != '\n') print_string("\n");
    vfs_release(node);
}

static void cmd_touch(char* args) {
    args = skip_spaces(args);
    if (*args == '\0') { print_string("Usage: touch <path>\n"); return; }

    char name[VFS_PATH_MAX];
    fs_node_t* dir = vfs_resolve_parent(args, name);
    if (!dir) { print_string("Directory not found.\n"); return; }
    if (create_fs(dir, name) == 0) print_string("Created.\n");
    else print_string("Could not create file (exists, or not supported here).\n");
    vfs_release(dir);
}

static void cmd_rm(char* args) {
    args = skip_spaces(args);
    if (*args == '\0') { print_string("Usage: rm <path>\n"); return; }

    char name[VFS_PATH_MAX];
    fs_node_t* dir = vfs_resolve_parent(args, name);
    if (!dir) { print_string("Directory not found.\n"); return; }
    if (unlink_fs(dir, name) == 0) print_string("Removed.\n");
    else print_string("Could not remove file (not found, or not supported here).\n");
    vfs_release(dir);
}

static void cmd_echo(char* args) {
    char *cmd = args;
    int gt_idx = -1;
    for (int i = 0; cmd[i] != '\0'; i++) {
        if (cmd[i] == '>') {
            gt_idx = i;
            break;
        }
    }

    if (gt_idx <= 0) {
        print_string("Usage: echo text > filename\n");
        return;
    }

    cmd[gt_idx - 1] = '\0'; // End text before space
    char *text = cmd;
    char *filename = skip_spaces(cmd + gt_idx + 1);

    fs_node_t *fsnode = vfs_resolve(filename);

    // Create the file if it doesn't exist yet, then write
    if (!fsnode) {
        char name[VFS_PATH_MAX];
        fs_node_t *dir = vfs_resolve_parent(filename, name);
        if (dir && create_fs(dir, name) == 0)
            fsnode = finddir_fs(dir, name);
        vfs_release(dir);
    }

    if (fsnode && (fsnode->flags & 0x7) == FS_FILE) {
        write_fs(fsnode, 0, strlen(text), (uint8_t*)text);
        print_string("Written to file.\n");
    } else {
        print_string("File not found and could not be created.\n");
    }
    vfs_release(fsnode);
}

void init_cmd_fs() {
    register_command("ls", "List files in directory", cmd_ls);
    register_command("cat", "Print file contents", cmd_cat);
    register_command("echo", "Write to file (echo msg > file)", cmd_echo);
    register_command("touch", "Create an empty file (touch disk/f.txt)", cmd_touch);
    register_command("rm", "Delete a file (rm disk/f.txt)", cmd_rm);
}
