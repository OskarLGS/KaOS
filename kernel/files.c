#include "files.h"
#include "graphics.h"
#include "keyboard.h"
#include "timer.h"
#include "util.h"
#include "vfs.h"
#include "task.h"
#include "users.h"

#define MAX_ENTRIES   64
#define NAME_MAX      128   // FAT32 long names are up to 127 chars
#define ROW_H         12
#define HEADER_H      18
#define FOOTER_H      14
#define PAD           6
#define VIEW_MAX      4096
#define DOUBLE_CLICK  50    // ticks (100 Hz) between clicks on one row

#define COL_TEXT      0xDADADA
#define COL_DIM       0x8A8A8A
#define COL_DIR       0x7FB2E5
#define COL_HEADER    0x3C3C3C
#define COL_SELECT    0x0078D7
#define COL_SELECT_BG 0x4A4A4A  // Selection while the window isn't focused

typedef struct {
    char     name[NAME_MAX];
    int      is_dir;
    uint32_t size;
} entry_t;

static char    cwd[VFS_PATH_MAX];   // "" = root, else e.g. "disk/docs"
static entry_t entries[MAX_ENTRIES];
static int     count = 0;
static int     selected = 0;
static int     top = 0;             // First visible row

static int      viewing = 0;        // Showing a file instead of the list
static char     view_name[VFS_PATH_MAX];
static char     view_buf[VIEW_MAX + 1];
static uint32_t view_len = 0;
static uint32_t view_size = 0;      // Real file size (may exceed VIEW_MAX)
static int      view_top = 0;       // First visible text line

static int      last_click_row = -1;
static uint32_t last_click_tick = 0;

// ─── Directory model ──────────────────────────────────────────────────────────

static void load_dir() {
    count = 0;
    selected = 0;
    top = 0;

    // Our own ".." first (RAMFS has none; FAT32's are skipped below)
    if (cwd[0]) {
        strcpy(entries[0].name, "..");
        entries[0].is_dir = 1;
        entries[0].size = 0;
        count = 1;
    }

    fs_node_t* dir = vfs_resolve(cwd);
    if (!dir) return;

    struct dirent* de;
    for (uint32_t i = 0; count < MAX_ENTRIES && (de = readdir_fs(dir, i)) != 0; i++) {
        if (strcmp(de->name, ".") == 0 || strcmp(de->name, "..") == 0) continue;
        if (strlen(de->name) >= NAME_MAX) continue;

        entry_t* e = &entries[count++];
        strcpy(e->name, de->name);
        fs_node_t* n = finddir_fs(dir, e->name);
        e->is_dir = n && (n->flags & 0x7) == FS_DIRECTORY;
        e->size   = n ? n->length : 0;
        vfs_release(n);
    }
    vfs_release(dir);
}

// cwd + "/" + name, into out (VFS_PATH_MAX). Returns 0 if it won't fit.
static int join(const char* name, char* out) {
    int need = strlen(cwd) + strlen((char*)name) + 2;
    if (need > VFS_PATH_MAX) return 0;
    strcpy(out, cwd);
    if (cwd[0]) append(out, '/');
    strcpy(out + strlen(out), (char*)name);
    return 1;
}

static void go_up() {
    int cut = 0;
    for (int i = 0; cwd[i]; i++)
        if (cwd[i] == '/') cut = i;
    cwd[cut] = '\0';   // "disk/docs" -> "disk", "disk" -> ""
    load_dir();
}

static void open_file(const char* path) {
    if (!perm_read(current_uid(), path)) {
        strcpy(view_buf, "Permission denied.");
        view_len = strlen(view_buf);
        view_size = 0;
        strcpy(view_name, (char*)path);
        view_top = 0;
        viewing = 1;
        return;
    }
    fs_node_t* f = vfs_resolve(path);
    if (!f) return;
    view_size = f->length;
    view_len  = read_fs(f, 0, VIEW_MAX, (uint8_t*)view_buf);
    view_buf[view_len] = '\0';
    vfs_release(f);

    strcpy(view_name, (char*)path);
    view_top = 0;
    viewing = 1;
}

static void open_selected() {
    if (selected < 0 || selected >= count) return;
    entry_t* e = &entries[selected];

    if (strcmp(e->name, "..") == 0) { go_up(); return; }

    char path[VFS_PATH_MAX];
    if (!join(e->name, path)) return;
    if (e->is_dir) {
        strcpy(cwd, path);
        load_dir();
    } else {
        open_file(path);
    }
}

// ─── Layout helpers ───────────────────────────────────────────────────────────

static int list_rows(window_t* w) {
    int r = (w->h - TITLEBAR_H - HEADER_H - FOOTER_H - 4) / ROW_H;
    return r < 1 ? 1 : r;
}

static int text_cols(window_t* w) {
    int c = (w->w - 2 * PAD) / 8;
    return c < 8 ? 8 : c;
}

static int text_rows(window_t* w) {
    int r = (w->h - TITLEBAR_H - HEADER_H - FOOTER_H - 4) / 10;
    return r < 1 ? 1 : r;
}

static void keep_selection_visible(window_t* w) {
    int rows = list_rows(w);
    if (selected < top) top = selected;
    if (selected >= top + rows) top = selected - rows + 1;
    if (top < 0) top = 0;
}

// Header: a title, shortened from the left to fit
static void draw_header(window_t* w, const char* text) {
    int x = w->x, y = w->y + TITLEBAR_H;
    draw_rect(x, y, w->w, HEADER_H, COL_HEADER);

    int max = (w->w - 2 * PAD) / 8;
    int len = strlen((char*)text);
    if (len > max) text += len - max;
    draw_string(x + PAD, y + 5, text, COL_TEXT);
}

static void draw_footer(window_t* w, const char* text) {
    int y = w->y + w->h - FOOTER_H;
    char buf[100];
    int max = (w->w - 2 * PAD) / 8;
    int i = 0;
    for (; text[i] && i < max && i < 99; i++) buf[i] = text[i];
    buf[i] = '\0';
    draw_string(w->x + PAD, y + 3, buf, COL_DIM);
}

static void format_size(uint32_t n, char* out) {
    if (n >= 10240) {
        int_to_ascii((int)(n / 1024), out);
        append(out, 'K');
    } else {
        int_to_ascii((int)n, out);
    }
}

// ─── Painting ─────────────────────────────────────────────────────────────────

static void paint_list(window_t* w) {
    char header[VFS_PATH_MAX + 1];
    header[0] = '/';
    strcpy(header + 1, cwd);
    draw_header(w, header);

    int rows = list_rows(w);
    int y0 = w->y + TITLEBAR_H + HEADER_H + 2;
    for (int r = 0; r < rows && top + r < count; r++) {
        int i = top + r;
        entry_t* e = &entries[i];
        int y = y0 + r * ROW_H;

        if (i == selected)
            draw_rect(w->x + 2, y - 2, w->w - 4, ROW_H,
                      w->focused ? COL_SELECT : COL_SELECT_BG);

        char size[16];
        size[0] = '\0';
        if (!e->is_dir) format_size(e->size, size);

        // Long names are cut with "~" to leave room for the size column
        int room = (w->w - 2 * PAD) / 8 - (size[0] ? strlen(size) + 1 : 0);
        char label[NAME_MAX + 2];
        strcpy(label, e->name);
        if (e->is_dir) append(label, '/');
        if (room > 1 && strlen(label) > room) {
            label[room - 1] = '~';
            label[room] = '\0';
        }
        draw_string(w->x + PAD, y, label,
                    i == selected ? 0xFFFFFF : (e->is_dir ? COL_DIR : COL_TEXT));

        if (size[0])
            draw_string(w->x + w->w - PAD - strlen(size) * 8, y, size,
                        i == selected ? 0xFFFFFF : COL_DIM);
    }
    if (count == 0)
        draw_string(w->x + PAD, y0, "(empty or not found)", COL_DIM);

    char foot[64];
    int_to_ascii(count, foot);
    strcpy(foot + strlen(foot), " items  Enter: open  Bksp: up");
    draw_footer(w, foot);
}

// Walk the text as wrapped lines: calls emit(line_index, start, len) for
// each line. Returns the number of lines.
static int wrap_lines(int cols, void (*emit)(int, const char*, int, void*), void* ctx) {
    int line = 0;
    const char* p = view_buf;
    const char* end = view_buf + view_len;
    while (p < end) {
        int n = 0;
        while (p + n < end && p[n] != '\n' && n < cols) n++;
        if (emit) emit(line, p, n, ctx);
        line++;
        p += n;
        if (p < end && *p == '\n') p++;
    }
    return line;
}

typedef struct { window_t* w; int rows; } view_ctx_t;

static void emit_line(int line, const char* s, int n, void* vctx) {
    view_ctx_t* ctx = (view_ctx_t*)vctx;
    int r = line - view_top;
    if (r < 0 || r >= ctx->rows) return;

    char buf[128];
    if (n > 127) n = 127;
    for (int i = 0; i < n; i++) {
        char c = s[i];
        if (c == '\t') c = ' ';
        else if (c == '\r') c = ' ';
        else if ((uint8_t)c < 32 || (uint8_t)c > 126) c = '.';
        buf[i] = c;
    }
    buf[n] = '\0';
    int y = ctx->w->y + TITLEBAR_H + HEADER_H + 4 + r * 10;
    draw_string(ctx->w->x + PAD, y, buf, COL_TEXT);
}

static void paint_view(window_t* w) {
    char header[VFS_PATH_MAX + 24];
    header[0] = '/';
    strcpy(header + 1, view_name);
    strcpy(header + strlen(header), "  (");
    int_to_ascii((int)view_size, header + strlen(header));
    strcpy(header + strlen(header), " bytes)");
    draw_header(w, header);

    view_ctx_t ctx = { w, text_rows(w) };
    wrap_lines(text_cols(w), emit_line, &ctx);

    draw_footer(w, view_size > VIEW_MAX
                   ? "First 4 KB only.  Bksp/Esc: back"
                   : "PgUp/PgDn: scroll  Bksp/Esc: back");
}

static void files_paint(window_t* w) {
    if (viewing) paint_view(w);
    else paint_list(w);
}

// ─── Input ────────────────────────────────────────────────────────────────────

static void scroll_view(window_t* w, int delta) {
    int total = wrap_lines(text_cols(w), 0, 0);
    int max_top = total - text_rows(w);
    if (max_top < 0) max_top = 0;
    view_top += delta;
    if (view_top > max_top) view_top = max_top;
    if (view_top < 0) view_top = 0;
}

static void files_key(window_t* w, char c) {
    if (viewing) {
        if (c == '\b' || c == 27) viewing = 0;
        else if (c == KEY_UP)   scroll_view(w, -1);
        else if (c == KEY_DOWN) scroll_view(w, +1);
        else if (c == KEY_PGUP) scroll_view(w, -text_rows(w));
        else if (c == KEY_PGDN) scroll_view(w, +text_rows(w));
        return;
    }

    int rows = list_rows(w);
    if (c == KEY_UP)        selected--;
    else if (c == KEY_DOWN) selected++;
    else if (c == KEY_PGUP) selected -= rows;
    else if (c == KEY_PGDN) selected += rows;
    else if (c == '\n')     { open_selected(); return; }
    else if (c == '\b')     { if (cwd[0]) go_up(); return; }
    else return;

    if (selected >= count) selected = count - 1;
    if (selected < 0) selected = 0;
    keep_selection_visible(w);
}

static void files_click(window_t* w, int x, int y) {
    (void)x;
    uint32_t now = get_tick();
    int dbl = (now - last_click_tick) < DOUBLE_CLICK;
    last_click_tick = now;

    if (viewing) {
        if (dbl) { viewing = 0; last_click_tick = 0; }
        return;
    }

    int row = (y - HEADER_H - 2) / ROW_H;
    if (y < HEADER_H + 2 || row >= list_rows(w)) return;
    int idx = top + row;
    if (idx >= count) return;

    if (dbl && idx == last_click_row) {
        last_click_row = -1;
        last_click_tick = 0;   // A third click starts over
        open_selected();
        return;
    }
    selected = idx;
    last_click_row = idx;
}

void files_init(window_t* win) {
    if (!win) return;
    win->on_paint = files_paint;
    win->on_key   = files_key;
    win->on_click = files_click;
    cwd[0] = '\0';
    load_dir();
}
