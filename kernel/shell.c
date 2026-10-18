#include "shell.h"
#include "screen.h"
#include "util.h"
#include "timer.h"
#include "memory.h"
#include "paging.h"
#include "ata.h"
#include "vfs.h"
#include "elf.h"
#include "task.h"
#include "graphics.h"
#include "isr.h"
#include "keyboard.h"

static command_t commands[MAX_COMMANDS];
static int num_commands = 0;
static int job_pid = 0; // Foreground program the shell waits on (0 = none)

static char line[SHELL_LINE_MAX];   // Line being edited

// Command history: ring of the last HISTORY_SIZE distinct lines.
// history_pos counts back from the newest while browsing (0 = editing a
// fresh line); `draft` keeps what was typed before pressing Up.
static char history[HISTORY_SIZE][SHELL_LINE_MAX];
static int  history_count = 0;      // Total ever added (index = count % SIZE)
static int  history_pos = 0;
static char draft[SHELL_LINE_MAX];

static void (*clear_hook)() = 0;

void register_command(const char* name, const char* description, command_func func) {
    if (num_commands >= MAX_COMMANDS) return;
    
    strcpy(commands[num_commands].name, (char*)name);
    strcpy(commands[num_commands].description, (char*)description);
    commands[num_commands].func = func;
    num_commands++;
}

static void cmd_help(char* args) {
    (void)args;
    print_string("Available commands:\n");
    for (int i = 0; i < num_commands; i++) {
        print_string("  ");
        print_string(commands[i].name);
        
        // Pad with spaces for alignment
        int len = strlen(commands[i].name);
        for (int j = 0; j < 10 - len; j++) print_string(" ");
        
        print_string("- ");
        print_string(commands[i].description);
        print_string("\n");
    }
}

void init_shell() {
    num_commands = 0;
    
    // Register internal commands
    register_command("help", "Display this message", cmd_help);
    
    // Initialize external command modules
    init_cmd_sys();
    init_cmd_fs();
    init_cmd_mem();
    init_cmd_task();
}

void shell_handler(char* input) {
    if (strlen(input) == 0) {
        print_string("> ");
        return;
    }

    // Split input into command and arguments
    char cmd_name[16];
    char* args = "";
    int i = 0;
    
    // Find first space or end of string
    while (input[i] != ' ' && input[i] != '\0' && i < 15) {
        cmd_name[i] = input[i];
        i++;
    }
    cmd_name[i] = '\0';
    
    // If there's a space, the rest is arguments
    if (input[i] == ' ') {
        while (input[i] == ' ') i++; // Skip extra spaces
        args = input + i;
    }

    // Search for the command
    int found = 0;
    for (int c = 0; c < num_commands; c++) {
        if (strcmp(cmd_name, commands[c].name) == 0) {
            commands[c].func(args);
            found = 1;
            break;
        }
    }

    if (!found) {
        print_string("Unknown command: '");
        print_string(cmd_name);
        print_string("'\nType 'help' for a list of commands.\n");
    }

    if (!job_pid) print_string("> ");
}

void shell_set_clear_hook(void (*hook)()) {
    clear_hook = hook;
}

void shell_clear() {
    if (clear_hook) {
        clear_hook();
    } else if (is_graphical_mode()) {
        clear_screen(0x1E1E1E);
        swap_buffers();
    } else {
        clear_vga_text();
    }
}

static void history_add(const char* s) {
    if (s[0] == '\0') return;
    if (history_count > 0 &&
        strcmp(history[(history_count - 1) % HISTORY_SIZE], (char*)s) == 0)
        return; // Skip immediate repeats
    strcpy(history[history_count % HISTORY_SIZE], (char*)s);
    history_count++;
}

// Erase the visible line and show `s` in its place.
static void replace_line(const char* s) {
    // Built as one string so the redraw is a single atomic print
    static char out[SHELL_LINE_MAX * 4];
    int n = 0;
    for (int i = strlen(line); i > 0; i--) {
        out[n++] = '\b'; out[n++] = ' '; out[n++] = '\b';
    }
    out[n] = '\0';
    strcpy(out + n, (char*)s);
    print_string(out);
    strcpy(line, (char*)s);
}

static void history_browse(int dir) {
    int available = history_count < HISTORY_SIZE ? history_count : HISTORY_SIZE;
    int pos = history_pos + dir;
    if (pos < 0 || pos > available) return;
    if (history_pos == 0) strcpy(draft, line);  // Leaving the fresh line
    history_pos = pos;
    if (pos == 0) replace_line(draft);
    else replace_line(history[(history_count - pos) % HISTORY_SIZE]);
}

void shell_input_key(char c) {
    if (c == '\n') {
        print_string("\n");
        history_add(line);
        history_pos = 0;
        // Run a copy: the command may print and we reset `line` first
        char cmd[SHELL_LINE_MAX];
        strcpy(cmd, line);
        line[0] = '\0';
        shell_handler(cmd);
    } else if (c == '\b') {
        if (strlen(line) > 0) {
            backspace(line);
            print_string("\b \b");
        }
    } else if (c == KEY_UP) {
        history_browse(+1);
    } else if (c == KEY_DOWN) {
        history_browse(-1);
    } else if ((uint8_t)c >= 0x20 || c == '\t') {
        if (strlen(line) < SHELL_LINE_MAX - 2) {
            char s[2] = { c, '\0' };
            append(line, c);
            print_string(s);
        }
    }
    // Other control codes (Left/Right, PgUp/PgDn) are not line edits
}

void shell_start_job(int pid) {
    job_pid = pid;
}

void shell_poll() {
    if (!job_pid) return;

    int code;
    exit_kind_t kind;
    int st = task_status(job_pid, &code, &kind);
    if (st == 0) return; // Still running

    // Compose the whole report first: one print_string is atomic.
    char line[80];
    line[0] = '\0';
    if (st < 0) {
        strcpy(line, "[program ended]");
    } else if (kind == EXIT_NORMAL) {
        strcpy(line, "[exited with code ");
        int_to_ascii(code, line + strlen(line));
        append(line, ']');
    } else if (kind == EXIT_FAULT) {
        strcpy(line, "[crashed: ");
        strcpy(line + strlen(line), exception_messages[code & 31]);
        append(line, ']');
    } else if (kind == EXIT_INTERRUPTED) {
        strcpy(line, "^C [interrupted]");
    } else {
        strcpy(line, "[killed]");
    }
    strcpy(line + strlen(line), "\n> ");
    job_pid = 0;
    print_string(line);
}

