#include "screen.h"
#include "ports.h"
#include "graphics.h"
#include "util.h"

int cursor_x = 0;
int cursor_y = 0;

static int max_cols = MAX_COLS;
static int max_rows = MAX_ROWS;

// Optional output redirection: while set, all print_char output is handed to
// the sink (e.g. the graphical terminal window) instead of the console.
static void (*print_sink)(char) = 0;

void set_print_sink(void (*sink)(char)) {
    print_sink = sink;
}

void update_cursor() {
    if (is_graphical_mode()) {
        // En modo gráfico no tenemos cursor por hardware
        return;
    }

    uint16_t position = (cursor_y * max_cols) + cursor_x;
    port_byte_out(0x3D4, 0x0E);
    port_byte_out(0x3D5, (uint8_t)((position >> 8) & 0xFF));
    port_byte_out(0x3D4, 0x0F);
    port_byte_out(0x3D5, (uint8_t)(position & 0xFF));
}

void clear_vga_text() {
    if (is_graphical_mode()) {
        max_cols = 100; // 800 / 8
        max_rows = 71;  // (600 - 32) / 8
        clear_screen(0x1E1E1E);
        draw_rect(0, 0, 800, 30, 0x3E3E3E);
        draw_string(10, 8, "KaOS v1.1.0 - Graphical Mode", 0xFFFFFF);
    } else {
        max_cols = MAX_COLS;
        max_rows = MAX_ROWS;
        char* video_memory = (char*) VIDEO_ADDRESS;
        for (int i = 0; i < max_rows * max_cols * 2; i += 2) {
            video_memory[i] = ' ';
            video_memory[i+1] = WHITE_ON_BLACK;
        }
    }
    
    cursor_x = 0;
    cursor_y = 0;
    update_cursor();
}

void scroll() {
    if (is_graphical_mode()) {
        graphics_scroll(32, 8); // Start below the title bar, scroll 1 char height
        cursor_y = max_rows - 1;
    } else {
        char* video_memory = (char*) VIDEO_ADDRESS;
        for (int i = 1; i < max_rows; i++) {
            for (int j = 0; j < max_cols * 2; j++) {
                video_memory[((i - 1) * max_cols * 2) + j] = video_memory[(i * max_cols * 2) + j];
            }
        }
        for (int j = 0; j < max_cols * 2; j += 2) {
            video_memory[((max_rows - 1) * max_cols * 2) + j] = ' ';
            video_memory[((max_rows - 1) * max_cols * 2) + j + 1] = WHITE_ON_BLACK;
        }
        cursor_y = max_rows - 1;
    }
    update_cursor();
}

void print_char(char character, char attribute) {
    if (print_sink) {
        print_sink(character);
        return;
    }

    if (character == '\n') {
        cursor_x = 0;
        cursor_y++;
    } else if (character == '\b') {
        if (cursor_x > 0) {
            cursor_x--;
            if (is_graphical_mode()) {
                draw_rect(cursor_x * 8, 32 + cursor_y * 8, 8, 8, 0x1E1E1E);
            } else {
                char* video_memory = (char*) VIDEO_ADDRESS;
                int offset = (cursor_y * max_cols + cursor_x) * 2;
                video_memory[offset] = ' ';
                video_memory[offset + 1] = attribute;
            }
        }
    } else {
        if (is_graphical_mode()) {
            draw_char(cursor_x * 8, 32 + cursor_y * 8, character, 0xFFFFFF);
        } else {
            char* video_memory = (char*) VIDEO_ADDRESS;
            int offset = (cursor_y * max_cols + cursor_x) * 2;
            video_memory[offset] = character;
            video_memory[offset + 1] = attribute;
        }
        cursor_x++;
    }

    if (cursor_x >= max_cols) {
        cursor_x = 0;
        cursor_y++;
    }

    if (cursor_y >= max_rows) {
        scroll();
    }
    update_cursor();
}

void print_string(char* message) {
    // Keep each line atomic under preemptive multitasking: without this a
    // task can be preempted mid-string and another task's output garbles
    // the line. The timer only switches tasks via an IRQ, so disabling
    // interrupts for the (bounded) duration of one line is enough.
    uint32_t eflags;
    __asm__ __volatile__("pushf; pop %0; cli" : "=r"(eflags));

    int i = 0;
    while (message[i] != 0) {
        print_char(message[i++], WHITE_ON_BLACK);
    }
    // With a sink active the desktop loop owns presentation
    if (is_graphical_mode() && !print_sink) {
        swap_buffers();
    }

    if (eflags & 0x200) __asm__ __volatile__("sti");
}

void print_string_at(char* message, int col, int row) {
    cursor_x = col;
    cursor_y = row;
    int prev_y = cursor_y;
    print_string(message);
    // Print_string already swaps buffers
}
