#include "terminal.h"
#include "graphics.h"
#include "screen.h"
#include "shell.h"
#include "util.h"

// Grid sized for the 500x350 Terminal window: client area is 500x330,
// minus 6px padding on each side, at 8x8 pixels per character.
#define MAX_TERM_COLS 98
#define MAX_TERM_ROWS 68
#define CHAR_W 8
#define CHAR_H 8
#define TERM_PAD  6

#define TERM_BG   0x1E1E1E
#define TERM_FG   0xDADADA

static window_t* term_win = 0;
static char term_grid[MAX_TERM_ROWS][MAX_TERM_COLS];
static int  term_cols = 60;
static int  term_rows = 38;
static int  term_cx = 0;
static int  term_cy = 0;

static char reflow_buf[MAX_TERM_COLS * MAX_TERM_ROWS + MAX_TERM_ROWS + 1];

static char term_line[256];
static volatile int term_dirty = 0;

window_t* terminal_window() { return term_win; }

int terminal_take_dirty() {
    int d = term_dirty;
    term_dirty = 0;
    return d;
}

// The grid's row stride is MAX_TERM_COLS, not term_cols, so rows must be
// moved one at a time (a flat copy of term_rows * term_cols would mix rows).
static void term_scroll() {
    for (int r = 1; r < term_rows; r++)
        memory_copy(&term_grid[r][0], &term_grid[r - 1][0], term_cols);
    memory_set((uint8_t*)&term_grid[term_rows - 1][0], ' ', term_cols);
}

// Grid writer — also installed as the print sink while shell commands run,
// so everything the shell prints lands here instead of the fullscreen console.
static void term_putc(char c) {
    term_dirty = 1;
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
        if (term_cx >= term_cols) { term_cx = 0; term_cy++; }
    }

    if (term_cy >= term_rows) {
        term_scroll();
        term_cy = term_rows - 1;
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

    for (int r = 0; r < term_rows; r++)
        for (int c = 0; c < term_cols; c++)
            if (term_grid[r][c] != ' ')
                draw_char(ox + c * 8, oy + r * 8, term_grid[r][c], TERM_FG);

    // Input cursor
    draw_char(ox + term_cx * 8, oy + term_cy * 8, '_', 0x6FBF6F);
}

void terminal_on_resize(window_t* w) {
    if (!w || !w->visible) return;

    // 1. Serialize current grid into reflow_buf
    int pos = 0;
    for (int r = 0; r < term_rows; r++) {
        int last = -1;
        for (int c = 0; c < term_cols; c++)
            if (term_grid[r][c] != ' ') last = c;
        for (int c = 0; c <= last && pos < (int)sizeof(reflow_buf) - 2; c++)
            reflow_buf[pos++] = term_grid[r][c];
        if (pos < (int)sizeof(reflow_buf) - 1)
            reflow_buf[pos++] = '\n';
    }
    reflow_buf[pos] = '\0';

    // 2. Recalculate active dimensions
    int nc = (w->w - 2 * TERM_PAD) / CHAR_W;
    int nr = (w->h - TITLEBAR_H - 2 * TERM_PAD) / CHAR_H;
    if (nc < 10) nc = 10;
    if (nr <  4) nr =  4;
    if (nc > MAX_TERM_COLS) nc = MAX_TERM_COLS;
    if (nr > MAX_TERM_ROWS) nr = MAX_TERM_ROWS;
    term_cols = nc;
    term_rows = nr;

    // 3. Clear and reprint
    memory_set((uint8_t*)term_grid, ' ', sizeof(term_grid));
    term_cx = 0;
    term_cy = 0;
    term_print(reflow_buf);
}

void terminal_init(window_t* win) {
    term_win = win;
    if (win) win->on_paint = terminal_paint;
    if (win) win->on_resize = terminal_on_resize;

    term_clear();
    term_line[0] = '\0';
    term_print("KaOS Terminal\n");
    term_print("Type 'help' for a list of commands.\n\n> ");

    // The terminal is the desktop's only console: keep it as the print sink
    // permanently, so user programs (which print from their own task, after
    // `exec` has returned) and exception reports land here too.
    set_print_sink(term_putc);
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
            shell_handler(term_line);
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
