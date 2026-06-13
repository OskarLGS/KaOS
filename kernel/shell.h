#ifndef SHELL_H
#define SHELL_H

#define MAX_COMMANDS 32
#define MAX_ARGS 16

typedef void (*command_func)(char* args);

typedef struct {
    char name[16];
    char description[64];
    command_func func;
} command_t;

void init_shell();
void register_command(const char* name, const char* description, command_func func);
void shell_handler(char* input);

// Command initialization prototypes
void init_cmd_sys();
void init_cmd_fs();
void init_cmd_mem();
void init_cmd_task();

#endif
