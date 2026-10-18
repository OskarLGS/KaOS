# Shell — Command Interpreter

The KaOS shell is a simple command-line interpreter. It receives text input from the keyboard driver and executes built-in commands.

## How Input Flows

```
Keyboard IRQ → ring buffer (keyboard.c)
    → shell loop: text mode in main.c, desktop via terminal_handle_key
    → shell_input_key(c)          line editor shared by both consoles
        printable  → append to the line, echo
        Backspace  → erase last char ("\b \b")
        Up / Down  → walk the history (the half-typed line is kept as a
                     draft and comes back when you go down past the newest)
        Enter      → add to history, shell_handler(line)
```

While a program started with `exec` runs in the foreground, the shell
loops stop reading keys (the program's SYS_READ gets them) and
`shell_poll()` prints the exit report and the prompt when it ends.

History keeps the last 16 distinct commands (`HISTORY_SIZE`), skipping
immediate repeats. The graphical terminal additionally scrolls back with
PgUp/PgDn (see graphics.md).

## Text editor (kernel/editor.c)

`edit <path>` opens a file (created on the first save if missing):
full screen in text mode, in the **Editor** window on the desktop.

| Key | |
|-----|-|
| arrows, Home/End, PgUp/PgDn | Move |
| Backspace / Delete | Erase before / at the cursor |
| Enter, Tab (4 spaces) | Insert |
| Ctrl+S | Save (respects path permissions) |
| Ctrl+Q | Close; with unsaved changes it asks for a second Ctrl+Q |

The status bar shows the file, `[modified]`, line and column. Files up
to 32 KB; DOS line ends are converted on load. Save outcomes are also
written to the debug port (`[edit] Saved N bytes.`) for the tests.

## Commands

`help` lists everything registered. Commands live in modules under
`kernel/commands/` (`cmd_fs.c`, `cmd_mem.c`, `cmd_sys.c`, `cmd_task.c`).

## Adding a New Command

```c
static void cmd_hello(char* args) {   // args = text after the name
    print_string("Hello!\n");
}

void init_cmd_sys() {
    ...
    register_command("hello", "Say hello", cmd_hello);
}
```

Up to `MAX_COMMANDS` (32); names up to 15 characters. Compose multi-part
output into one buffer and print it once — `print_string` is atomic, so
output from a running program can't interleave inside it.

`clear` calls `shell_clear()`: the graphical terminal registers a hook
(`shell_set_clear_hook`) so it clears its own window.

## Utility Functions

Since we don't have the C standard library, we wrote our own in `kernel/util.c`:

| Function | Equivalent |
|----------|-----------|
| `strlen(s)` | `strlen` |
| `strcmp(a, b)` | `strcmp` |
| `append(s, c)` | Append char to string |
| `backspace(s)` | Remove last char |
| `int_to_ascii(n, s)` | `itoa` |

## Key Files
- `kernel/shell.c/h` — Command interpreter
- `kernel/util.c/h` — String utilities
