#include "../shell.h"
#include "../vfs.h"
#include "../util.h"
#include "../screen.h"

static void cmd_ls(char* args) {
    char *path = args;
    while (*path == ' ') path++;
    
    fs_node_t *dir = fs_root;
    if (strlen(path) > 0) {
        dir = finddir_fs(fs_root, path);
    }

    if (!dir || (dir->flags & 0x7) != FS_DIRECTORY) {
        print_string("Directory not found.\n");
    } else {
        int i = 0;
        struct dirent *node = 0;
        while ((node = readdir_fs(dir, i)) != 0) {
            fs_node_t *fsnode = finddir_fs(dir, node->name);
            if (fsnode && (fsnode->flags & 0x7) == FS_DIRECTORY) {
                print_string("(dir)  ");
            } else {
                print_string("(file) ");
            }
            print_string(node->name);
            print_string("\n");
            i++;
        }
    }
}

static void cmd_cat(char* args) {
    char *path = args;
    
    // Simple path parser for one level: subdir/file
    char *slash = 0;
    for (int i = 0; path[i] != '\0'; i++) {
        if (path[i] == '/') {
            slash = path + i;
            break;
        }
    }

    fs_node_t *node = 0;
    if (slash) {
        *slash = '\0';
        fs_node_t *dir = finddir_fs(fs_root, path);
        if (dir && (dir->flags & 0x7) == FS_DIRECTORY) {
            node = finddir_fs(dir, slash + 1);
        }
        *slash = '/'; // Restore path for error message
    } else {
        node = finddir_fs(fs_root, path);
    }

    if (node) {
        uint8_t buffer[513]; // 512 of data + NUL terminator
        uint32_t sz = read_fs(node, 0, 512, buffer);
        if (sz > 0) {
            buffer[sz] = '\0';
            print_string((char*)buffer);
        }
        if (sz > 0 && buffer[sz-1] != '\n') print_string("\n");
    } else {
        print_string("File not found.\n");
    }
}

// Split a one-level path "dir/file" into the parent directory node and the
// leaf name. For a bare "file", the parent is the filesystem root. Returns
// 0 if the directory part can't be resolved. Modifies `path` in place.
static fs_node_t* resolve_parent(char* path, char** out_name) {
    char* slash = 0;
    for (int i = 0; path[i] != '\0'; i++) {
        if (path[i] == '/') { slash = path + i; break; }
    }
    if (!slash) {
        *out_name = path;
        return fs_root;
    }
    *slash = '\0';
    fs_node_t* dir = finddir_fs(fs_root, path);
    *out_name = slash + 1;
    if (dir && (dir->flags & 0x7) == FS_DIRECTORY) return dir;
    return 0;
}

static void cmd_touch(char* args) {
    while (*args == ' ') args++;
    if (*args == '\0') { print_string("Usage: touch <path>\n"); return; }

    char* name;
    fs_node_t* dir = resolve_parent(args, &name);
    if (!dir) { print_string("Directory not found.\n"); return; }
    if (create_fs(dir, name) == 0) print_string("Created.\n");
    else print_string("Could not create file (exists, or not supported here).\n");
}

static void cmd_rm(char* args) {
    while (*args == ' ') args++;
    if (*args == '\0') { print_string("Usage: rm <path>\n"); return; }

    char* name;
    fs_node_t* dir = resolve_parent(args, &name);
    if (!dir) { print_string("Directory not found.\n"); return; }
    if (unlink_fs(dir, name) == 0) print_string("Removed.\n");
    else print_string("Could not remove file (not found, or not supported here).\n");
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
    
    if (gt_idx > 0) {
        cmd[gt_idx - 1] = '\0'; // End text before space
        char *text = cmd;
        char *filename = cmd + gt_idx + 2; // Skip "> "

        // One-level path support (same as cat): dir/file
        fs_node_t *fsnode = 0;
        char *slash = 0;
        for (int i = 0; filename[i] != '\0'; i++) {
            if (filename[i] == '/') { slash = filename + i; break; }
        }
        if (slash) {
            *slash = '\0';
            fs_node_t *dir = finddir_fs(fs_root, filename);
            if (dir && (dir->flags & 0x7) == FS_DIRECTORY) {
                fsnode = finddir_fs(dir, slash + 1);
            }
            *slash = '/';
        } else {
            fsnode = finddir_fs(fs_root, filename);
        }

        // Create the file if it doesn't exist yet, then write
        if (!fsnode && slash) {
            *slash = '\0';
            fs_node_t *dir = finddir_fs(fs_root, filename);
            if (dir && (dir->flags & 0x7) == FS_DIRECTORY) {
                if (create_fs(dir, slash + 1) == 0)
                    fsnode = finddir_fs(dir, slash + 1);
            }
            *slash = '/';
        }

        if (fsnode) {
            write_fs(fsnode, 0, strlen(text), (uint8_t*)text);
            print_string("Written to file.\n");
        } else {
            print_string("File not found and could not be created.\n");
        }
    } else {
        print_string("Usage: echo text > filename\n");
    }
}

void init_cmd_fs() {
    register_command("ls", "List files in directory", cmd_ls);
    register_command("cat", "Print file contents", cmd_cat);
    register_command("echo", "Write to file (echo msg > file)", cmd_echo);
    register_command("touch", "Create an empty file (touch disk/f.txt)", cmd_touch);
    register_command("rm", "Delete a file (rm disk/f.txt)", cmd_rm);
}
