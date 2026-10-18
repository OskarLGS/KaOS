#include "../shell.h"
#include "../screen.h"
#include "../util.h"
#include "../graphics.h"
#include "../rtc.h"

static void cmd_clear(char* args) {
    (void)args;
    if (is_graphical_mode()) {
        clear_screen(0x1E1E1E);
        swap_buffers();
    } else {
        clear_vga_text();
    }
}

static void cmd_info(char* args) {
    (void)args;
    print_string("KaOS - by Oskar\n");
    print_string("A custom operating system built from scratch.\n");
    print_string("Running in 32-bit Protected Mode.\n");
}

static void cmd_version(char* args) {
    (void)args;
    print_string("KaBooM (Ka Boot Manager) v1.0.0\n");
    print_string("KaOS (Kernel)          v1.0.0\n");
}

static void cmd_date(char* args) {
    (void)args;
    rtc_time_t t;
    rtc_read(&t);

    // "Sun 2026-10-18 09:05:07\n" composed first: one atomic print
    char line[32];
    strcpy(line, (char*)rtc_weekday_name(&t));
    append(line, ' ');
    rtc_format_date(&t, line + strlen(line));
    append(line, ' ');
    rtc_format_time(&t, line + strlen(line));
    append(line, '\n');
    print_string(line);
}

static void cmd_halt(char* args) {
    (void)args;
    print_string("Halting system. Goodbye!\n");
    __asm__ __volatile__("hlt");
}

void init_cmd_sys() {
    register_command("clear", "Clear the screen", cmd_clear);
    register_command("info", "Display OS info", cmd_info);
    register_command("version", "Show version numbers", cmd_version);
    register_command("halt", "Halt the system", cmd_halt);
    register_command("date", "Show the date and time (RTC)", cmd_date);
}
