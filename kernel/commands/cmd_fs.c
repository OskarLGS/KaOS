#include "../shell.h"
#include "../vfs.h"
#include "../util.h"
#include "../screen.h"
#include "../task.h"
#include "../users.h"
#include "../editor.h"
#include "../graphics.h"

#define BAD_NAME_MSG "Invalid name (up to 127 chars, none of \\ / : * ? \" < > |).\n"

// Permission gates: print the refusal and return 0 if not allowed
static int may_read(const char* path) {
    if (perm_read(current_uid(), path)) return 1;
    print_string("Permission denied.\n");
    return 0;
}

static int may_write(const char* path) {
    if (perm_write(current_uid(), path)) return 1;
    print_string("Permission denied (you can write in your home and disk/tmp).\n");
    return 0;
}

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
    if (!may_read(path)) return;
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
    if (!may_write(args)) return;

    char name[VFS_PATH_MAX];
    fs_node_t* dir = vfs_resolve_parent(args, name);
    if (!dir) { print_string("Directory not found.\n"); return; }
    int r = create_fs(dir, name);
    if (r == 0) print_string("Created.\n");
    else if (r == VFS_EBADNAME) print_string(BAD_NAME_MSG);
    else print_string("Could not create file (exists, or not supported here).\n");
    vfs_release(dir);
}

static void cmd_rm(char* args) {
    args = skip_spaces(args);
    if (*args == '\0') { print_string("Usage: rm <path>\n"); return; }
    if (!may_write(args)) return;

    char name[VFS_PATH_MAX];
    fs_node_t* dir = vfs_resolve_parent(args, name);
    if (!dir) { print_string("Directory not found.\n"); return; }
    int r = unlink_fs(dir, name);
    if (r == 0) print_string("Removed.\n");
    else if (r == -2) print_string("Directory not empty.\n");
    else print_string("Could not remove (not found, or not supported here).\n");
    vfs_release(dir);
}

static void cmd_mkdir(char* args) {
    args = skip_spaces(args);
    if (*args == '\0') { print_string("Usage: mkdir <path>\n"); return; }
    if (!may_write(args)) return;

    char name[VFS_PATH_MAX];
    fs_node_t* dir = vfs_resolve_parent(args, name);
    if (!dir) { print_string("Directory not found.\n"); return; }
    int r = mkdir_fs(dir, name);
    if (r == 0) print_string("Created.\n");
    else if (r == VFS_EBADNAME) print_string(BAD_NAME_MSG);
    else print_string("Could not create directory (exists, or not supported here).\n");
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
    if (!may_write(filename)) return;

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

// Last component of a path ("disk/docs/a.txt" -> "a.txt")
static char* base_name(char* path) {
    char* b = path;
    for (char* p = path; *p; p++)
        if (*p == '/' && p[1]) b = p + 1;
    return b;
}

// Resolve the destination of cp/mv: an existing directory means
// "into it, same name". Fills dst_dir + dst_name; returns 0 on success.
static int resolve_target(char* src, char* dst, fs_node_t** dst_dir, char* dst_name) {
    fs_node_t* d = vfs_resolve(dst);
    if (d && (d->flags & 0x7) == FS_DIRECTORY) {
        *dst_dir = d;
        strcpy(dst_name, base_name(src));
        return 0;
    }
    vfs_release(d);
    *dst_dir = vfs_resolve_parent(dst, dst_name);
    return *dst_dir ? 0 : -1;
}

// Copy the file `src` to dst_dir/dst_name (created if missing).
// Returns bytes copied, or -1.
static int copy_file(fs_node_t* src, fs_node_t* dst_dir, char* dst_name) {
    fs_node_t* out = finddir_fs(dst_dir, dst_name);
    if (!out) {
        int r = create_fs(dst_dir, dst_name);
        if (r == VFS_EBADNAME) { print_string(BAD_NAME_MSG); return -1; }
        if (r == 0) out = finddir_fs(dst_dir, dst_name);
    }
    if (!out || (out->flags & 0x7) != FS_FILE) { vfs_release(out); return -1; }

    uint8_t buf[512];
    uint32_t off = 0, n;
    while ((n = read_fs(src, off, sizeof(buf), buf)) > 0) {
        if (write_fs(out, off, n, buf) != n) { vfs_release(out); return -1; }
        off += n;
    }
    // An empty source still has to truncate an existing destination
    if (off == 0 && out->length > 0) {
        vfs_release(out);
        unlink_fs(dst_dir, dst_name);
        create_fs(dst_dir, dst_name);
        return 0;
    }
    vfs_release(out);
    return (int)off;
}

// The path cp/mv would write: `dst`, or `dst/<name of src>` when `dst`
// is an existing directory (what resolve_target does).
static void target_path(char* src, char* dst, char* out) {
    strcpy(out, dst);
    fs_node_t* d = vfs_resolve(dst);
    if (d && (d->flags & 0x7) == FS_DIRECTORY &&
        strlen(dst) + strlen(base_name(src)) + 2 < VFS_PATH_MAX) {
        append(out, '/');
        strcpy(out + strlen(out), base_name(src));
    }
    vfs_release(d);
}

// Split "a b" into two words; returns 0 unless exactly two are given.
static int two_args(char* args, char** a, char** b) {
    *a = skip_spaces(args);
    char* p = *a;
    while (*p && *p != ' ') p++;
    if (!*p) return 0;
    *p++ = '\0';
    *b = skip_spaces(p);
    p = *b;
    while (*p && *p != ' ') p++;
    if (*p) { *p = '\0'; if (*skip_spaces(p + 1)) return 0; }
    return **a && **b;
}

static void print_copied(int n) {
    char line[40];
    strcpy(line, "Copied ");
    int_to_ascii(n, line + strlen(line));
    strcpy(line + strlen(line), " bytes.\n");
    print_string(line);
}

static void cmd_cp(char* args) {
    char *src_path, *dst_path;
    if (!two_args(args, &src_path, &dst_path)) {
        print_string("Usage: cp <from> <to>   (to may be a directory)\n");
        return;
    }
    char dst_full[VFS_PATH_MAX];
    target_path(src_path, dst_path, dst_full);
    if (!may_read(src_path) || !may_write(dst_full)) return;
    fs_node_t* src = vfs_resolve(src_path);
    if (!src || (src->flags & 0x7) != FS_FILE) {
        print_string("Source file not found (cp copies files).\n");
        vfs_release(src);
        return;
    }

    fs_node_t* dir;
    char name[VFS_PATH_MAX];
    if (resolve_target(src_path, dst_path, &dir, name) != 0) {
        print_string("Destination directory not found.\n");
    } else {
        int n = copy_file(src, dir, name);
        if (n < 0) print_string("Copy failed (destination not writable here?).\n");
        else print_copied(n);
        vfs_release(dir);
    }
    vfs_release(src);
}

static void cmd_mv(char* args) {
    char *src_path, *dst_path;
    if (!two_args(args, &src_path, &dst_path)) {
        print_string("Usage: mv <from> <to>   (to may be a directory)\n");
        return;
    }
    char dst_full[VFS_PATH_MAX];
    target_path(src_path, dst_path, dst_full);
    if (!may_write(src_path) || !may_write(dst_full)) return;
    char src_name[VFS_PATH_MAX];
    fs_node_t* src_dir = vfs_resolve_parent(src_path, src_name);
    fs_node_t* src = src_dir ? finddir_fs(src_dir, src_name) : 0;
    if (!src) {
        print_string("Source not found.\n");
        vfs_release(src_dir);
        return;
    }

    fs_node_t* dir;
    char name[VFS_PATH_MAX];
    if (resolve_target(src_path, dst_path, &dir, name) != 0) {
        print_string("Destination directory not found.\n");
        vfs_release(src);
        vfs_release(src_dir);
        return;
    }

    int r = rename_fs(src_dir, src_name, dir, name);
    if (r == 0) {
        print_string("Moved.\n");
    } else if (r == VFS_EXDEV && (src->flags & 0x7) == FS_FILE) {
        // Different filesystems: copy, then remove the original
        int n = copy_file(src, dir, name);
        if (n < 0) print_string("Copy failed; source left in place.\n");
        else if (unlink_fs(src_dir, src_name) != 0)
            print_string("Copied, but the source could not be removed here.\n");
        else print_string("Moved (copied across filesystems).\n");
    } else if (r == VFS_EXDEV) {
        print_string("Moving directories between filesystems is not supported.\n");
    } else if (r == VFS_EINVAL) {
        print_string("Cannot move a directory into itself.\n");
    } else if (r == VFS_EBADNAME) {
        print_string(BAD_NAME_MSG);
    } else {
        print_string("Could not move (target exists?).\n");
    }
    vfs_release(dir);
    vfs_release(src);
    vfs_release(src_dir);
}

static void cmd_edit(char* args) {
    char* p = skip_spaces(args);
    if (!*p) { print_string("Usage: edit <path>\n"); return; }
    if (editor_open(p) == 0 && is_graphical_mode())
        print_string("Editing in the Editor window (Ctrl+S save, Ctrl+Q close).\n");
}

void init_cmd_fs() {
    register_command("edit", "Text editor (edit disk/home/me/notes.txt)", cmd_edit);
    register_command("ls", "List files in directory", cmd_ls);
    register_command("cat", "Print file contents", cmd_cat);
    register_command("echo", "Write to file (echo msg > file)", cmd_echo);
    register_command("touch", "Create an empty file (touch disk/f.txt)", cmd_touch);
    register_command("rm", "Delete a file or empty dir (rm disk/f.txt)", cmd_rm);
    register_command("mkdir", "Create a directory (mkdir disk/docs/new)", cmd_mkdir);
    register_command("cp", "Copy a file (cp readme.txt disk/docs)", cmd_cp);
    register_command("mv", "Move/rename (mv disk/a.txt disk/docs/b.txt)", cmd_mv);
}
