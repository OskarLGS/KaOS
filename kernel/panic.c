#include "panic.h"
#include "screen.h"
#include "graphics.h"
#include "task.h"
#include "ports.h"
#include "util.h"

extern char* exception_messages[];

#define BG     0x5A0000
#define FG     0xFFFFFF
#define DIM    0xFFB0B0

static int y;                    // Next line (graphics mode)

static void hex(uint32_t v, char* out) {
    const char* hx = "0123456789ABCDEF";
    out[0] = '0'; out[1] = 'x';
    for (int i = 0; i < 8; i++) out[2 + i] = hx[(v >> ((7 - i) * 4)) & 0xF];
    out[10] = '\0';
}

static void line(const char* s, uint32_t color) {
    if (is_graphical_mode()) {
        draw_string(40, y, s, color);
        y += 14;
        for (const char* p = s; *p; p++) port_byte_out(0xE9, (uint8_t)*p);
        port_byte_out(0xE9, '\n');
    } else {
        print_string((char*)s);          // VGA text; print_char mirrors to 0xE9
        print_string("\n");
    }
}

// "NAME=0x00000000  NAME=0x..." rows of registers
static void regs_line(const char* a, uint32_t va, const char* b, uint32_t vb,
                      const char* c, uint32_t vc, const char* d, uint32_t vd) {
    char out[96], h[11];
    const char* names[4] = { a, b, c, d };
    uint32_t vals[4] = { va, vb, vc, vd };
    out[0] = '\0';
    for (int i = 0; i < 4 && names[i]; i++) {
        strcpy(out + strlen(out), (char*)names[i]);
        append(out, '=');
        hex(vals[i], h);
        strcpy(out + strlen(out), h);
        strcpy(out + strlen(out), "  ");
    }
    line(out, FG);
}

void panic(const char* reason, struct registers* r) {
    __asm__ __volatile__("cli");
    set_print_sink(0);               // Not into a window nobody repaints

    if (is_graphical_mode()) {
        clear_screen(BG);
        draw_string_scaled(40, 40, "KaOS - kernel panic", FG, 3);
        y = 90;
        port_byte_out(0xE9, '\n');
    } else {
        clear_vga_text();
        print_string("*** KaOS - KERNEL PANIC ***\n\n");
    }

    char buf[96];
    strcpy(buf, "KERNEL PANIC: ");
    strcpy(buf + strlen(buf), (char*)reason);
    line(buf, FG);
    line("", FG);

    if (r) {
        char h[11];
        strcpy(buf, "Exception ");
        int_to_ascii((int)r->int_no, buf + strlen(buf));
        strcpy(buf + strlen(buf), ": ");
        strcpy(buf + strlen(buf), r->int_no < 32 ? exception_messages[r->int_no] : "?");
        strcpy(buf + strlen(buf), "  error code ");
        hex(r->err_code, h);
        strcpy(buf + strlen(buf), h);
        line(buf, FG);
        if (r->int_no == 14) {
            uint32_t cr2;
            __asm__ __volatile__("mov %%cr2, %0" : "=r"(cr2));
            strcpy(buf, "Faulting address (CR2) ");
            hex(cr2, h);
            strcpy(buf + strlen(buf), h);
            line(buf, FG);
        }
        line("", FG);
        regs_line("EIP", r->eip, "CS", r->cs, "EFLAGS", r->eflags, 0, 0);
        regs_line("EAX", r->eax, "EBX", r->ebx, "ECX", r->ecx, "EDX", r->edx);
        regs_line("ESI", r->esi, "EDI", r->edi, "EBP", r->ebp, "ESP", r->esp);
        regs_line("DS", r->ds, 0, 0, 0, 0, 0, 0);
        line("", FG);
    }

    task_t* t = current_task_ptr();
    if (t) {
        strcpy(buf, "Running task: ");
        strcpy(buf + strlen(buf), t->name);
        strcpy(buf + strlen(buf), " (pid ");
        int_to_ascii((int)t->id, buf + strlen(buf));
        append(buf, ')');
        line(buf, FG);
    }
    uint32_t cr3;
    __asm__ __volatile__("mov %%cr3, %0" : "=r"(cr3));
    char h[11];
    hex(cr3, h);
    strcpy(buf, "Page directory (CR3) ");
    strcpy(buf + strlen(buf), h);
    line(buf, FG);
    line("", FG);
    line("The system has been halted. Restart the computer.", DIM);
    line("Look up EIP in build/kernel.map (function start addresses).", DIM);

    if (is_graphical_mode()) swap_buffers();
    while (1) __asm__ __volatile__("cli; hlt");
}
