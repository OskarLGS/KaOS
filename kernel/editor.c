#include "editor.h"
#include "screen.h"
#include "graphics.h"
#include "keyboard.h"
#include "vfs.h"
#include "users.h"
#include "task.h"
#include "util.h"
#include "ports.h"
#include "shell.h"
#include "terminal.h"

#define ED_MAX   32768          // Largest file the editor takes
#define TAB_W    4

static char text[ED_MAX];       // The whole file, Latin-1, '\n' lines
static int  len = 0;
static int  cur = 0;            // Cursor: byte offset into text
static int  top = 0;            // First visible line
static int  left = 0;           // First visible column
static char path[VFS_PATH_MAX];
static int  open_ = 0;          // A file is loaded
static int  modified = 0;
static int  quit_armed = 0;     // Ctrl+Q pressed once with unsaved changes
static char msg[80];            // One-shot status message
static window_t* win = 0;       // Desktop window (0 in text mode)

// ─── Geometry ─────────────────────────────────────────────────────────────────

static int text_cols() {
    if (win) { int c = (win->w - 12) / 8; return c > 8 ? c : 8; }
    return 80;
}

static int text_rows() {               // Not counting the status bar
    if (win) { int r = (win->h - TITLEBAR_H - 12) / 10 - 1; return r > 2 ? r : 2; }
    return 24;
}

// ─── Text helpers (offsets into `text`) ───────────────────────────────────────

static int line_start(int p) { while (p > 0 && text[p - 1] != '\n') p--; return p; }
static int line_end(int p)   { while (p < len && text[p] != '\n') p++; return p; }

static int line_of(int p) {
    int n = 0;
    for (int i = 0; i < p; i++) if (text[i] == '\n') n++;
    return n;
}

static int start_of_line(int n) {
    int p = 0;
    while (n > 0 && p < len) { if (text[p++] == '\n') n--; }
    return p;
}

// Same column on another line (clamped to its length)
static int move_to_line(int p, int delta) {
    int col = p - line_start(p);
    int target = line_of(p) + delta;
    if (target < 0) target = 0;
    int s = start_of_line(target);
    if (delta > 0 && s == len && line_of(len) < target) return p;  // Past the end
    int e = line_end(s);
    return (s + col < e) ? s + col : e;
}

static void insert(char c) {
    if (len >= ED_MAX - 1) { strcpy(msg, "File too big for the editor."); return; }
    for (int i = len; i > cur; i--) text[i] = text[i - 1];
    text[cur++] = c;
    len++;
    modified = 1;
}

static void erase(int p) {              // Delete the character at p
    if (p < 0 || p >= len) return;
    for (int i = p; i < len - 1; i++) text[i] = text[i + 1];
    len--;
    if (cur > p) cur--;
    modified = 1;
}

// ─── Drawing ──────────────────────────────────────────────────────────────────

static void keep_cursor_visible() {
    int rows = text_rows(), cols = text_cols();
    int ln = line_of(cur), col = cur - line_start(cur);
    if (ln < top) top = ln;
    if (ln >= top + rows) top = ln - rows + 1;
    if (col < left) left = col;
    if (col >= left + cols) left = col - cols + 1;
}

static void status_text(char* out) {
    if (msg[0]) { strcpy(out, msg); return; }
    strcpy(out, path);
    if (modified) strcpy(out + strlen(out), " [modified]");
    strcpy(out + strlen(out), "  Ln ");
    int_to_ascii(line_of(cur) + 1, out + strlen(out));
    strcpy(out + strlen(out), ", Col ");
    int_to_ascii(cur - line_start(cur) + 1, out + strlen(out));
    strcpy(out + strlen(out), "   ^S save  ^Q close");
}

static void draw_vga() {
    int rows = text_rows(), cols = text_cols();
    int p = start_of_line(top);
    for (int r = 0; r < rows; r++) {
        int c = 0, col = 0;
        while (p < len && text[p] != '\n') {
            if (col >= left && c < cols) { vga_put(c, r, text[p] == '\t' ? ' ' : text[p], 0x0F); c++; }
            col++;
            p++;
        }
        if (p < len) p++;                     // Skip '\n'
        for (; c < cols; c++) vga_put(c, r, ' ', 0x0F);
    }
    char st[160];
    status_text(st);
    int i = 0;
    for (; st[i] && i < cols; i++) vga_put(i, rows, st[i], 0x70);   // Inverse bar
    for (; i < cols; i++) vga_put(i, rows, ' ', 0x70);
    vga_set_cursor(cur - line_start(cur) - left, line_of(cur) - top);
}

static void paint(window_t* self) {
    int rows = text_rows(), cols = text_cols();
    int x0 = self->x + 6, y0 = self->y + TITLEBAR_H + 6;
    draw_rect(self->x, self->y + TITLEBAR_H, self->w, self->h - TITLEBAR_H, 0x1E1E1E);

    int p = start_of_line(top);
    for (int r = 0; r < rows; r++) {
        int col = 0;
        while (p < len && text[p] != '\n') {
            int c = col - left;
            if (c >= 0 && c < cols && text[p] != ' ' && text[p] != '\t')
                draw_char(x0 + c * 8, y0 + r * 10, text[p], 0xDADADA);
            col++;
            p++;
        }
        if (p < len) p++;
    }
    // Cursor block
    int cc = cur - line_start(cur) - left, cr = line_of(cur) - top;
    if (self->focused && cr >= 0 && cr < rows && cc >= 0 && cc < cols)
        draw_rect(x0 + cc * 8, y0 + cr * 10 + 8, 8, 2, 0x6FBF6F);

    // Status bar
    int sy = self->y + self->h - 14;
    draw_rect(self->x, sy - 2, self->w, 16, 0x3C3C3C);
    char st[160];
    status_text(st);
    if ((int)strlen(st) > cols) st[cols] = '\0';
    draw_string(x0, sy + 2, st, modified ? 0xFFD27F : 0xDADADA);
}

static void redraw() {
    keep_cursor_visible();
    if (!win) draw_vga();
}

// ─── Load / save / close ──────────────────────────────────────────────────────

static void debug_log(const char* s) {        // For the test transcript
    for (; *s; s++) port_byte_out(0xE9, (uint8_t)*s);
}


static void save_impl();

// Save, and leave the outcome in the test transcript too
static void save() {
    save_impl();
    debug_log("[edit] ");
    debug_log(msg);
    debug_log("\n");
}

static void save_impl() {
    if (!perm_write(current_uid(), path)) { strcpy(msg, "Permission denied: not saved."); return; }

    fs_node_t* f = vfs_resolve(path);
    char leaf[VFS_PATH_MAX];
    fs_node_t* dir = vfs_resolve_parent(path, leaf);
    // A write only ever sets the size to its end, so an emptied file is
    // recreated rather than written with zero bytes
    if (f && len == 0 && dir) {
        vfs_release(f);
        f = 0;
        unlink_fs(dir, leaf);
    }
    if (!f && dir && create_fs(dir, leaf) == 0) f = finddir_fs(dir, leaf);
    vfs_release(dir);
    if (!f || (f->flags & 0x7) != FS_FILE) {
        vfs_release(f);
        strcpy(msg, "Could not write the file.");
        return;
    }
    int ok = len == 0 || write_fs(f, 0, len, (uint8_t*)text) == (uint32_t)len;
    vfs_release(f);
    if (!ok) { strcpy(msg, "Write failed: file not (fully) saved."); return; }

    modified = 0;
    strcpy(msg, "Saved ");
    int_to_ascii(len, msg + strlen(msg));
    strcpy(msg + strlen(msg), " bytes.");
}

static void close_editor() {
    open_ = 0;
    debug_log("[edit] closed\n");
    if (win) {
        wm_hide(win);
        if (terminal_window()) wm_show(terminal_window());
    } else {
        clear_vga_text();
        shell_handler("");            // Back to the prompt
    }
}

int editor_open(const char* p) {
    if (open_) { print_string("The editor is already open.\n"); return -1; }
    if (strlen((char*)p) >= VFS_PATH_MAX) { print_string("Path too long.\n"); return -1; }

    fs_node_t* f = vfs_resolve(p);
    if (f && (f->flags & 0x7) != FS_FILE) {
        vfs_release(f);
        print_string("That is a directory.\n");
        return -1;
    }
    if (f && !perm_read(current_uid(), p)) {
        vfs_release(f);
        print_string("Permission denied.\n");
        return -1;
    }

    len = 0;
    if (f) {
        if (f->length >= ED_MAX) {
            vfs_release(f);
            print_string("File too big for the editor (32 KB max).\n");
            return -1;
        }
        uint32_t n = read_fs(f, 0, f->length, (uint8_t*)text);
        vfs_release(f);
        for (uint32_t i = 0; i < n; i++)          // Drop CRs of DOS files
            if (text[i] != '\r') text[len++] = text[i];
    }

    strcpy(path, (char*)p);
    cur = top = left = 0;
    modified = quit_armed = 0;
    strcpy(msg, f ? "" : "New file.");
    open_ = 1;
    debug_log("[edit] opened\n");

    if (win) {
        wm_show(win);
    } else {
        clear_vga_text();
        redraw();
    }
    return 0;
}

int editor_active() {
    return open_ && !win;
}

void editor_key(char c) {
    if (!open_) return;
    uint8_t k = (uint8_t)c;
    if (k != CTRL('q')) quit_armed = 0;
    msg[0] = '\0';

    if (k == CTRL('s')) {
        save();
    } else if (k == CTRL('q')) {
        if (modified && !quit_armed) {
            strcpy(msg, "Unsaved changes! Ctrl+Q again to discard them.");
            quit_armed = 1;
        } else {
            close_editor();
            return;
        }
    } else if (k == KEY_LEFT)  { if (cur > 0) cur--; }
    else if (k == KEY_RIGHT)   { if (cur < len) cur++; }
    else if (k == KEY_UP)      { cur = move_to_line(cur, -1); }
    else if (k == KEY_DOWN)    { cur = move_to_line(cur, +1); }
    else if (k == KEY_PGUP)    { cur = move_to_line(cur, -text_rows()); }
    else if (k == KEY_PGDN)    { cur = move_to_line(cur, +text_rows()); }
    else if (k == KEY_HOME)    { cur = line_start(cur); }
    else if (k == KEY_END)     { cur = line_end(cur); }
    else if (c == '\b')        { if (cur > 0) erase(cur - 1); }
    else if (k == KEY_DEL)     { erase(cur); }
    else if (c == '\n')        { insert('\n'); }
    else if (c == '\t')        { for (int i = 0; i < TAB_W; i++) insert(' '); }
    else if (key_is_text(c))   { insert(c); }

    redraw();
}

static void window_key(window_t* self, char c) {
    (void)self;
    editor_key(c);
}

void editor_attach_window(window_t* w) {
    win = w;
    if (!w) return;
    w->on_paint = paint;
    w->on_key = window_key;
}
