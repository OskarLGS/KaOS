#include "../shell.h"
#include "../screen.h"
#include "../util.h"
#include "../graphics.h"
#include "../rtc.h"
#include "../keyboard.h"
#include "../task.h"
#include "../users.h"
#include "../vfs.h"

static void cmd_clear(char* args) {
    (void)args;
    shell_clear();
}

static void cmd_info(char* args) {
    (void)args;
    print_string("KaOS - by Oskar\n");
    print_string("A custom operating system built from scratch.\n");
    print_string("Running in 32-bit Protected Mode.\n");
}

static void cmd_version(char* args) {
    (void)args;
    print_string("KaBooM (Ka Boot Manager) v1.0.0\n");
    print_string("KaOS (Kernel)          v1.0.0\n");
}

static void cmd_date(char* args) {
    (void)args;
    rtc_time_t t;
    rtc_read(&t);

    // "Sun 2026-10-18 09:05:07\n" composed first: one atomic print
    char line[32];
    strcpy(line, (char*)rtc_weekday_name(&t));
    append(line, ' ');
    rtc_format_date(&t, line + strlen(line));
    append(line, ' ');
    rtc_format_time(&t, line + strlen(line));
    append(line, '\n');
    print_string(line);
}

#define KEYMAP_FILE "disk/etc/keymap"

static void cmd_keymap(char* args) {
    while (*args == ' ') args++;
    if (!*args) {
        print_string(keyboard_layout() == KEYMAP_ES ? "Layout: es\n" : "Layout: us\n");
        return;
    }
    int l;
    if (strcmp(args, "es") == 0) l = KEYMAP_ES;
    else if (strcmp(args, "us") == 0) l = KEYMAP_US;
    else { print_string("Usage: keymap [us|es]\n"); return; }
    keyboard_set_layout(l);

    // Root's choice is kept for the next boots
    if (current_uid() == UID_ROOT && !users_disabled()) {
        fs_node_t* f = vfs_resolve(KEYMAP_FILE);
        if (!f) {
            char leaf[VFS_PATH_MAX];
            fs_node_t* dir = vfs_resolve_parent(KEYMAP_FILE, leaf);
            if (dir && create_fs(dir, leaf) == 0) f = finddir_fs(dir, leaf);
            vfs_release(dir);
        }
        if (f) write_fs(f, 0, 2, (uint8_t*)args);
        vfs_release(f);
        print_string(l == KEYMAP_ES ? "Layout es (saved).\n" : "Layout us (saved).\n");
    } else {
        print_string(l == KEYMAP_ES ? "Layout es (this session).\n" : "Layout us (this session).\n");
    }
}

// At boot: the layout root saved, if any
void keymap_load() {
    fs_node_t* f = vfs_resolve(KEYMAP_FILE);
    if (!f) return;
    char buf[3] = { 0, 0, 0 };
    read_fs(f, 0, 2, (uint8_t*)buf);
    vfs_release(f);
    if (buf[0] == 'e' && buf[1] == 's') keyboard_set_layout(KEYMAP_ES);
}

static void cmd_halt(char* args) {
    (void)args;
    print_string("Halting system. Goodbye!\n");
    __asm__ __volatile__("hlt");
}

// Deliberately fault in the kernel, to see (and test) the panic screen
static void cmd_panic(char* args) {
    (void)args;
    if (current_uid() != UID_ROOT) {
        print_string("panic: only root can crash the kernel\n");
        return;
    }
    print_string("Writing to an unmapped kernel address...\n");
    *(volatile uint32_t*)0xFFFFF000 = 0xDEAD;
}

void init_cmd_sys() {
    register_command("clear", "Clear the screen", cmd_clear);
    register_command("info", "Display OS info", cmd_info);
    register_command("version", "Show version numbers", cmd_version);
    register_command("halt", "Halt the system", cmd_halt);
    register_command("date", "Show the date and time (RTC)", cmd_date);
    register_command("panic", "Crash the kernel on purpose (root, for testing)", cmd_panic);
    register_command("keymap", "Keyboard layout (keymap es / keymap us)", cmd_keymap);
}
