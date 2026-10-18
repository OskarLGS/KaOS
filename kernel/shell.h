#ifndef SHELL_H
#define SHELL_H

#define MAX_COMMANDS 48
#define MAX_ARGS 16
#define SHELL_LINE_MAX 256
#define HISTORY_SIZE   16

typedef void (*command_func)(char* args);

typedef struct {
    char name[16];
    char description[64];
    command_func func;
} command_t;

void init_shell();
void register_command(const char* name, const char* description, command_func func);
void shell_handler(char* input);

// Foreground job: the shell withholds its prompt while `pid` runs, and
// shell_poll() (called from the input loop) reports how it ended.
void shell_start_job(int pid);
// Run `fn` as a foreground kernel task (a builtin that waits for the
// network, like ping): the prompt waits, Ctrl+C stops it, and a clean
// exit (code 0) is not reported. `args` is copied; the task reads it
// with shell_job_args(). Returns the pid or -1.
int shell_run_job(const char* name, void (*fn)(), const char* args);
char* shell_job_args();

// A program started with `exec ... &`: it runs alongside the shell (no
// keyboard) and its end is reported at the next prompt.
void shell_start_background(int pid);
void shell_poll();

// Line editor shared by the text console and the graphical terminal:
// feed it every key the shell owns. Echo goes through print_string.
// Enter runs the line; Up/Down walk the command history.
void shell_input_key(char c);

// Optional hook for `clear` (the graphical terminal clears its window);
// without one, `clear` wipes the fullscreen console.
void shell_set_clear_hook(void (*hook)());
void shell_clear();

// Command initialization prototypes
void init_cmd_sys();
void init_cmd_fs();
void init_cmd_mem();
void init_cmd_task();
void init_cmd_net();

#endif
