#include "../shell.h"
#include "../memory.h"
#include "../paging.h"
#include "../util.h"
#include "../screen.h"

static void cmd_malloc(char* args) {
    (void)args;
    uint32_t addr = kmalloc(64);
    char buf[16];
    print_string("Allocated 64 bytes at 0x");
    char hex[] = "00000000";
    char hex_chars[] = "0123456789ABCDEF";
    uint32_t temp_addr = addr;
    for (int i = 7; i >= 0; i--) {
        hex[i] = hex_chars[temp_addr & 0xF];
        temp_addr >>= 4;
    }
    print_string(hex);
    print_string("\n");
    
    char used_buf[16];
    int_to_ascii(get_heap_used(), used_buf);
    print_string("Heap used: ");
    print_string(used_buf);
    print_string(" bytes\n");

    char free_buf[16];
    int_to_ascii(get_heap_free(), free_buf);
    print_string("Heap free: ");
    print_string(free_buf);
    print_string(" bytes\n");
    
    print_string("Use 'free 0x");
    print_string(hex);
    print_string("' to release it.\n");
}

static void cmd_free(char* args) {
    char* hex_str = args;
    if (hex_str[0] == '0' && hex_str[1] == 'x') hex_str += 2;
    
    uint32_t addr = 0;
    while (*hex_str) {
        addr <<= 4;
        if (*hex_str >= '0' && *hex_str <= '9') addr += (*hex_str - '0');
        else if (*hex_str >= 'A' && *hex_str <= 'F') addr += (*hex_str - 'A' + 10);
        else if (*hex_str >= 'a' && *hex_str <= 'f') addr += (*hex_str - 'a' + 10);
        hex_str++;
    }
    
    kfree(addr);
    print_string("Memory freed.\n");
    
    char used_buf[16];
    int_to_ascii(get_heap_used(), used_buf);
    print_string("Heap used: ");
    print_string(used_buf);
    print_string(" bytes\n");
}

static void cmd_paging(char* args) {
    (void)args;
    uint32_t pd_addr = get_page_directory_addr();
    print_string("Paging: ENABLED\n");
    print_string("Page Directory at 0x");
    char hex[] = "00000000";
    char hc[] = "0123456789ABCDEF";
    for (int i = 7; i >= 0; i--) {
        hex[i] = hc[pd_addr & 0xF];
        pd_addr >>= 4;
    }
    print_string(hex);
    print_string("\n");
}

void init_cmd_mem() {
    register_command("malloc", "Allocate memory (demo)", cmd_malloc);
    register_command("free", "Free memory (e.g. free 0xADDR)", cmd_free);
    register_command("paging", "Show paging info", cmd_paging);
}
