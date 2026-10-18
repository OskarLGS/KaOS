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

static command_t commands[MAX_COMMANDS];
static int num_commands = 0;
static int job_pid = 0; // Foreground program the shell waits on (0 = none)

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

