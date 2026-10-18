#include "terminal.h"
#include "graphics.h"
#include "screen.h"
#include "shell.h"
#include "util.h"
#include "keyboard.h"

// Grid sized for the 500x350 Terminal window: client area is 500x330,
// minus 6px padding on each side, at 8x8 pixels per character.
#define MAX_TERM_COLS 98
#define MAX_TERM_ROWS 68
#define CHAR_W 8
#define CHAR_H 8
#define TERM_PAD  6

#define TERM_BG   0x1E1E1E
#define TERM_FG   0xDADADA

// Lines that scrolled off the top are kept here; PgUp/PgDn browse them.
#define SB_LINES  200

static window_t* term_win = 0;
static char term_grid[MAX_TERM_ROWS][MAX_TERM_COLS];
static int  term_cols = 60;
static int  term_rows = 38;
static int  term_cx = 0;
static int  term_cy = 0;

static char reflow_buf[MAX_TERM_COLS * MAX_TERM_ROWS + MAX_TERM_ROWS + 1];

static char sb[SB_LINES][MAX_TERM_COLS]; // Ring of scrolled-off rows
static int  sb_count = 0;                // Total ever pushed (slot = n % SB_LINES)
static int  view_offset = 0;             // Rows scrolled back (0 = live view)

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
    memory_copy(&term_grid[0][0], &sb[sb_count % SB_LINES][0], MAX_TERM_COLS);
    sb_count++;
    for (int r = 1; r < term_rows; r++)
        memory_copy(&term_grid[r][0], &term_grid[r - 1][0], term_cols);
    memory_set((uint8_t*)&term_grid[term_rows - 1][0], ' ', term_cols);
}

// Grid writer — also installed as the print sink while shell commands run,
// so everything the shell prints lands here instead of the fullscreen console.
static void term_putc(char c) {
    term_dirty = 1;
    view_offset = 0;   // New output snaps back to the live view
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
    sb_count = 0;
    view_offset = 0;
    term_dirty = 1;
}

static int sb_available() {
    return sb_count < SB_LINES ? sb_count : SB_LINES;
}

// Row `r` of what is on screen: the live grid, or a scrollback line when
// the view is scrolled back (k = 1 is the most recent scrolled-off row).
static const char* view_row(int r) {
    int v = r - view_offset;
    if (v >= 0) return term_grid[v];
    return sb[(sb_count + v) % SB_LINES];
}

static void terminal_paint(window_t* self) {
    int ox = self->x + TERM_PAD;
    int oy = self->y + TITLEBAR_H + TERM_PAD;

    // Terminal background (darker than the default window body)
    draw_rect(self->x, self->y + TITLEBAR_H, self->w, self->h - TITLEBAR_H, TERM_BG);

    for (int r = 0; r < term_rows; r++) {
        const char* row = view_row(r);
        for (int c = 0; c < term_cols; c++)
            if (row[c] != ' ' && row[c] != '\0')
                draw_char(ox + c * 8, oy + r * 8, row[c], TERM_FG);
    }

    if (view_offset == 0) {
        // Input cursor
        draw_char(ox + term_cx * 8, oy + term_cy * 8, '_', 0x6FBF6F);
    } else {
        // Scrolled back: show how far, top right
        char tag[16];
        strcpy(tag, "[-");
        int_to_ascii(view_offset, tag + 2);
        append(tag, ']');
        int tx = self->x + self->w - TERM_PAD - strlen(tag) * 8;
        draw_rect(tx - 2, oy - 2, strlen(tag) * 8 + 4, 12, 0x0078D7);
        draw_string(tx, oy, tag, 0xFFFFFF);
    }
}

void terminal_on_resize(window_t* w) {
    if (!w || !w->visible) return;

    // 1. Serialize the grid up to the cursor into reflow_buf. Rows above
    // the cursor lose trailing blanks; the cursor row is kept exactly up to
    // the cursor (so "> " keeps its space) and gets no newline — reprinting
    // then leaves the cursor where the user was typing, not below the
    // empty rows of the old window.
    int pos = 0;
    for (int r = 0; r <= term_cy; r++) {
        int last = term_cx - 1;
        if (r < term_cy) {
            last = -1;
            for (int c = 0; c < term_cols; c++)
                if (term_grid[r][c] != ' ') last = c;
        }
        for (int c = 0; c <= last && pos < (int)sizeof(reflow_buf) - 2; c++)
            reflow_buf[pos++] = term_grid[r][c];
        if (r < term_cy && pos < (int)sizeof(reflow_buf) - 1)
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
    term_print("KaOS Terminal\n");
    term_print("Type 'help' for a list of commands.\n\n> ");

    // The terminal is the desktop's only console: keep it as the print sink
    // permanently, so user programs (which print from their own task, after
    // `exec` has returned) and exception reports land here too.
    set_print_sink(term_putc);

    // `clear` typed here wipes this window, not the fullscreen console
    shell_set_clear_hook(term_clear);
}

void terminal_handle_key(char c) {
    if (c == KEY_PGUP || c == KEY_PGDN) {
        int step = term_rows / 2;
        if (c == KEY_PGUP) view_offset += step;
        else view_offset -= step;
        if (view_offset > sb_available()) view_offset = sb_available();
        if (view_offset < 0) view_offset = 0;
        term_dirty = 1;
        return;
    }
    // Everything else is line editing (history, Enter, ...) in the shell
    shell_input_key(c);
}
