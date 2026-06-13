#include "terminal.h"
#include "graphics.h"
#include "screen.h"
#include "shell.h"
#include "util.h"

// Grid sized for the 500x350 Terminal window: client area is 500x330,
// minus 6px padding on each side, at 8x8 pixels per character.
#define TERM_COLS 60
#define TERM_ROWS 38
#define TERM_PAD  6

#define TERM_BG   0x1E1E1E
#define TERM_FG   0xDADADA

static window_t* term_win = 0;
static char term_grid[TERM_ROWS][TERM_COLS];
static int  term_cx = 0;
static int  term_cy = 0;

static char term_line[256];

window_t* terminal_window() { return term_win; }

static void term_scroll() {
    memory_copy(&term_grid[1][0], &term_grid[0][0],
                (TERM_ROWS - 1) * TERM_COLS);
    memory_set((uint8_t*)&term_grid[TERM_ROWS - 1][0], ' ', TERM_COLS);
}

// Grid writer — also installed as the print sink while shell commands run,
// so everything the shell prints lands here instead of the fullscreen console.
static void term_putc(char c) {
    if (c == '\n') {
        term_cx = 0;
        term_cy++;
    } else if (c == '\b') {
        if (term_cx > 0) {
            term_cx--;
            term_grid[term_cy][term_cx] = ' ';
        }
    } else {
        term_grid[term_cy][term_cx] = c;
        term_cx++;
        if (term_cx >= TERM_COLS) { term_cx = 0; term_cy++; }
    }

    if (term_cy >= TERM_ROWS) {
        term_scroll();
        term_cy = TERM_ROWS - 1;
    }
}

static void term_print(const char* s) {
    while (*s) term_putc(*s++);
}

static void term_clear() {
    memory_set((uint8_t*)term_grid, ' ', sizeof(term_grid));
    term_cx = 0;
    term_cy = 0;
}

static void terminal_paint(window_t* self) {
    int ox = self->x + TERM_PAD;
    int oy = self->y + TITLEBAR_H + TERM_PAD;

    // Terminal background (darker than the default window body)
    draw_rect(self->x, self->y + TITLEBAR_H, self->w, self->h - TITLEBAR_H, TERM_BG);

    for (int r = 0; r < TERM_ROWS; r++)
        for (int c = 0; c < TERM_COLS; c++)
            if (term_grid[r][c] != ' ')
                draw_char(ox + c * 8, oy + r * 8, term_grid[r][c], TERM_FG);

    // Input cursor
    draw_char(ox + term_cx * 8, oy + term_cy * 8, '_', 0x6FBF6F);
}

void terminal_init(window_t* win) {
    term_win = win;
    if (win) win->on_paint = terminal_paint;

    term_clear();
    term_line[0] = '\0';
    term_print("KaOS Terminal\n");
    term_print("Type 'help' for a list of commands.\n\n> ");
}

void terminal_handle_key(char c) {
    if (c == '\n') {
        term_putc('\n');
        if (strcmp(term_line, "clear") == 0) {
            // The shell's `clear` resets the fullscreen console — here it
            // means "clear this window's grid".
            term_clear();
            term_print("> ");
        } else {
            set_print_sink(term_putc);
            shell_handler(term_line);
            set_print_sink(0);
        }
        term_line[0] = '\0';
    } else if (c == '\b') {
        if (strlen(term_line) > 0) {
            backspace(term_line);
            term_putc('\b');
        }
    } else if (strlen(term_line) < 254) {
        append(term_line, c);
        term_putc(c);
    }
}
