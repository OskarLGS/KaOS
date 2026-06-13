# Shell — Command Interpreter

The KaOS shell is a simple command-line interpreter. It receives text input from the keyboard driver and executes built-in commands.

## How Input Flows

```
Keyboard IRQ → keyboard_callback()
    → Characters accumulate in key_buffer[]
    → User presses Enter
    → shell_handler(key_buffer) is called
    → Buffer is cleared
```

## Available Commands

| Command   | Description |
|-----------|-------------|
| `help`    | List all available commands |
| `clear`   | Clear the screen |
| `info`    | Show OS name and mode |
| `version` | Show KaBooM and KaOS versions |
| `uptime`  | Seconds since boot (PIT timer) |
| `malloc`  | Allocate 64 bytes and show address |
| `paging`  | Show paging status and Page Directory address |
| `halt`    | Halt the CPU |

## Adding a New Command

1. Open `kernel/shell.c`
2. Add a new `else if` block:

```c
} else if (strcmp(input, "mycommand") == 0) {
    print_string("Hello from my command!\n");
}
```

3. Add it to the `help` output.
4. Rebuild with `wsl bash build.sh`.

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
