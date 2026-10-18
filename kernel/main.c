#include "screen.h"
#include "gdt.h"
#include "isr.h"
#include "keyboard.h"
#include "timer.h"
#include "memory.h"
#include "paging.h"
#include "task.h"
#include "util.h"
#include "shell.h"
#include "ramfs.h"
#include "graphics.h"
#include "mouse.h"
#include "window.h"
#include "terminal.h"
#include "rtc.h"
#include "files.h"
#include "editor.h"
#include "net/net.h"

// ─── Debug helper ─────────────────────────────────────────────────────────────
// Port 0xE9 is logged by VirtualBox/Bochs to the host debug log (VBox.log).
static inline void dbg(char c) {
    __asm__ __volatile__("outb %%al, $0xE9" : : "a"((unsigned char)c));
}

// ─── Graphical desktop loop ───────────────────────────────────────────────────

// Compose "<label><number><suffix>" into buf using util.h helpers
static void format_stat(char* buf, char* label, int value, char* suffix) {
    strcpy(buf, label);
    int_to_ascii(value, buf + strlen(buf));
    char* s = suffix;
    while (*s) append(buf, *s++);
}

static void about_paint(window_t* self) {
    int cx = self->x + 12;                  // Client-area origin + padding
    int cy = self->y + TITLEBAR_H + 12;
    char line[64];

    draw_string(cx, cy,      "KaOS v1.1.0",        0xFFFFFF);
    draw_string(cx, cy + 14, "K Operating System", 0x9A9A9A);
    draw_rect(cx, cy + 28, self->w - 24, 1, WIN_COLOR_BORDER);

    draw_string(cx, cy + 38, "Bootloader: KaBooM v1.0", 0xC8C8C8);
    draw_string(cx, cy + 52, "Built by Oskar",         0xC8C8C8);

    format_stat(line, "Uptime: ", (int)get_uptime_seconds(), " s");
    draw_string(cx, cy + 74, line, 0x6FBF6F);

    format_stat(line, "Heap: ", (int)(get_heap_used() / 1024), " KB used");
    draw_string(cx, cy + 88, line, 0x6FBF6F);
}

static void clock_paint(window_t* self) {
    int cx = self->x + 14;
    int cy = self->y + TITLEBAR_H + 14;

    rtc_time_t now;
    rtc_read(&now);
    char buf[16];

    rtc_format_time(&now, buf);
    draw_string_scaled(cx, cy, buf, 0xFFFFFF, 3);    // 8 chars x 24 px

    strcpy(buf, (char*)rtc_weekday_name(&now));
    append(buf, ' ');
    rtc_format_date(&now, buf + strlen(buf));
    draw_string(cx, cy + 36, buf, 0x9A9A9A);
}

static void run_desktop() {
    dbg('D'); // entering desktop loop
    wm_init();
    cursor_enable(1);

    window_t* term = wm_create(50,  50,  500, 350, "Terminal");
    terminal_init(term);
    window_t* about = wm_create(300, 200, 280, 160, "About KaOS");
    if (about) about->on_paint = about_paint;
    window_t* clock = wm_create(560, 40, 220, 90, "Clock");
    if (clock) clock->on_paint = clock_paint;
    // File browser: starts hidden, opened from the KaOS start menu
    window_t* files = wm_create(120, 110, 340, 300, "Files");
    files_init(files);
    if (files) wm_hide(files);
    // Text editor window: shown by the `edit` command
    window_t* ed = wm_create(130, 70, 560, 420, "Editor");
    editor_attach_window(ed);
    if (ed) wm_hide(ed);
    dbg('W'); // windows created

    static uint8_t prev_buttons = 0;
    int prev_x = -1, prev_y = -1;
    uint32_t last_uptime = (uint32_t)-1;

    while (1) {
        // Route keystrokes to the terminal while its window is focused.
        // A running foreground program owns the keyboard (SYS_READ).
        shell_poll();
        int typed = terminal_take_dirty();
        window_t* fw = wm_get_focused();
        if (fw == terminal_window() && get_foreground() == 0) {
            char c;
            while ((c = keyboard_get_char()) != 0) {
                terminal_handle_key(c);
                typed = 1;
            }
        } else if (fw && fw != terminal_window() && fw->on_key) {
            char c;
            while ((c = keyboard_get_char()) != 0) {
                fw->on_key(fw, c);
                typed = 1;
            }
        }

        int mx     = mouse_get_x();
        int my     = mouse_get_y();
        uint8_t mb = mouse_get_buttons();

        int moved   = (mx != prev_x || my != prev_y);
        int changed = (mb != prev_buttons);
        int tick    = (get_uptime_seconds() != last_uptime); // Live stats refresh

        if (moved || changed || tick || typed) {
            wm_on_mouse_move(mx, my);
            if (changed) wm_on_mouse_click(mx, my, mb);
            wm_draw_all();
            prev_x       = mx;
            prev_y       = my;
            prev_buttons = mb;
            last_uptime  = get_uptime_seconds();
        }

        yield();
    }
}

// ─── Kernel entry ─────────────────────────────────────────────────────────────

void kernel_main() {
    // 1. Core Systems
    gdt_init();    // Ring 0/3 segments + TSS (replaces the bootloader GDT)
    isr_install();
    memory_init();

    // 2. Graphics
    graphics_init();
    dbg('G'); // graphics_init done

    // 3. Paging
    if (is_graphical_mode()) {
        uint32_t fb_phys = *(uint32_t*)0x0900;
        dbg('P'); // about to enable paging
        init_paging(fb_phys, 800 * 600 * 4);
        dbg('Q'); // paging enabled
        // Fill screen red to confirm VBE + paging are working
        clear_screen(0xFF0000);
        swap_buffers();
        dbg('V'); // VBE swap done
    } else {
        init_paging(0, 0);
    }

    // 4. Welcome Screen
    clear_vga_text();
    if (!is_graphical_mode()) {
        print_string("KaOS v1.1.0 - Text Mode\n");
    }

    tasking_init();
    dbg('T'); // tasking_init done

    print_string("Initializing Drivers...\n");
    init_timer(100);   dbg('R'); // timer done
    init_keyboard();   dbg('K'); // keyboard done
    if (is_graphical_mode()) {
        init_mouse();  dbg('M'); // mouse done
    }
    init_ramfs();
    net_init();        // Probes PCI for an RTL8139; harmless without one
    init_shell();      dbg('S'); // shell done

    print_string("Ready.\n\n");
    __asm__ __volatile__("sti");
    dbg('!'); // interrupts enabled

    if (is_graphical_mode()) {
        run_desktop(); // never returns
    }

    // Text-mode shell loop: log in (or first-boot setup) first
    shell_begin();

    while(1) {
        // Stop consuming keys while a foreground program owns the keyboard
        // (its SYS_READ reads them instead). Still yield so it runs.
        shell_poll();
        char c = (get_foreground() == 0) ? keyboard_get_char() : 0;
        if (c != 0) shell_input_key(c);
        yield();
    }
}
