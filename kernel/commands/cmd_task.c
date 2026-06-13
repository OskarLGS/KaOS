#include "../shell.h"
#include "../task.h"
#include "../timer.h"
#include "../elf.h"
#include "../util.h"
#include "../screen.h"

// Demo of preemptive multitasking: a CPU-bound task that NEVER yields.
// volatile so the spin loop's increments are real memory writes the shell
// can observe (and so -O2 doesn't optimize the loop away).
static volatile uint32_t spin_count = 0;
static int spin_started = 0;

static void spinner_task() {
    while (1) {
        spin_count++;   // no yield() — only the timer can take the CPU away
    }
}

static void cmd_spin(char* args) {
    (void)args;
    if (!spin_started) {
        if (create_task("spinner", spinner_task) >= 0) {
            spin_started = 1;
            print_string("Spinner task started (a tight loop with no yield).\n");
            print_string("The shell stays responsive only because the timer\n");
            print_string("preempts it. Run 'spin' again to watch it count.\n");
        } else {
            print_string("Could not start spinner (task table full).\n");
        }
    }
    char buf[16];
    int_to_ascii((int)spin_count, buf);
    print_string("spin_count = ");
    print_string(buf);
    print_string("\n");
}

static void cmd_ps(char* args) {
    (void)args;
    print_string("PID  STATE    NAME\n");
    print_string("---  ------   ----\n");
    for (int i = 0; i < get_task_count(); i++) {
        task_t* t = get_task(i);
        if (t) {
            char pid_buf[8];
            int_to_ascii(t->id, pid_buf);
            print_string(pid_buf);
            print_string("    ");
            if (t->state == TASK_RUNNING) print_string("RUN      ");
            else if (t->state == TASK_READY) print_string("READY    ");
            else print_string("STOPPED  ");
            print_string(t->name);
            print_string("\n");
        }
    }
}

static void cmd_kill(char* args) {
    int pid = ascii_to_int(args);
    kill(pid);
    print_string("Killed task ");
    char buf[8];
    int_to_ascii(pid, buf);
    print_string(buf);
    print_string("\n");
}

static void cmd_exec(char* args) {
    char *path = args;
    
    // Simple path parser
    char *slash = 0;
    for (int i = 0; path[i] != '\0'; i++) {
        if (path[i] == '/') {
            slash = path + i;
            break;
        }
    }

    fs_node_t *file = 0;
    if (slash) {
        *slash = '\0';
        fs_node_t *dir = finddir_fs(fs_root, path);
        if (dir && (dir->flags & 0x7) == FS_DIRECTORY) {
            file = finddir_fs(dir, slash + 1);
        }
        *slash = '/';
    } else {
        file = finddir_fs(fs_root, path);
    }

    if (file) {
        print_string("Loading ELF: ");
        print_string(file->name);
        print_string("\n");
        int pid = elf_load(file);
        if (pid >= 0) {
            // Compose into one buffer: the task may already be printing
            // concurrently, so a single atomic print keeps this line intact.
            char line[40];
            strcpy(line, "Started task PID ");
            int_to_ascii(pid, line + strlen(line));
            append(line, '\n');
            print_string(line);
            // Hand the keyboard to the program so its SYS_READ works; the
            // shell resumes input when the program exits (kexit clears it).
            set_foreground(pid);
        } else {
            print_string("Failed to load ELF.\n");
        }
    } else {
        print_string("File not found.\n");
    }
}

static void cmd_uptime(char* args) {
    (void)args;
    uint32_t seconds = get_uptime_seconds();
    char buf[16];
    int_to_ascii(seconds, buf);
    print_string("Uptime: ");
    print_string(buf);
    print_string(" seconds\n");
}

void init_cmd_task() {
    register_command("ps", "List running tasks", cmd_ps);
    register_command("kill", "Stop a task by PID", cmd_kill);
    register_command("exec", "Run an ELF program", cmd_exec);
    register_command("uptime", "Show system uptime", cmd_uptime);
    register_command("spin", "Demo: start a CPU-bound task (preemption)", cmd_spin);
}
