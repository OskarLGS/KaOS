#include "elf.h"
#include "vfs.h"
#include "paging.h"
#include "memory.h"
#include "util.h"
#include "task.h"
#include "screen.h"

// Lay out argc/argv at the top of the new process's stack, the way crt0
// (user/crt0.asm) expects it: [esp] = argc, [esp+4] = argv, with argv
// pointing at a NULL-terminated array of strings stored just above.
// The image is built in a kernel buffer (addresses are the process's
// virtual ones) and copied in with vmm_write. Returns the initial user
// stack pointer, or 0 on error.
static uint32_t setup_user_stack(uint32_t* pd, int argc, char** argv) {
    static uint8_t img[1024];          // Top of the stack, as it will look
    uint32_t top = USER_STACK_TOP;
    uint32_t sp = top;
    uint32_t ptrs[ELF_MAX_ARGS];

#define AT(addr) (&img[sizeof(img) - (top - (addr))])
    for (int i = argc - 1; i >= 0; i--) {
        int len = strlen(argv[i]) + 1;
        if (top - sp + len > sizeof(img) - 4 * (ELF_MAX_ARGS + 3)) return 0;
        sp -= len;
        memory_copy(argv[i], (char*)AT(sp), len);
        ptrs[i] = sp;
    }
    sp &= ~3u;

    sp -= 4; *(uint32_t*)AT(sp) = 0;            // argv[argc] = NULL
    for (int i = argc - 1; i >= 0; i--) {
        sp -= 4; *(uint32_t*)AT(sp) = ptrs[i];
    }
    uint32_t argv_addr = sp;
    sp -= 4; *(uint32_t*)AT(sp) = argv_addr;
    sp -= 4; *(uint32_t*)AT(sp) = (uint32_t)argc;
#undef AT

    if (vmm_write(pd, sp, &img[sizeof(img) - (top - sp)], top - sp) != 0) return 0;
    return sp;
}

// Map every page of [start, end) and copy `filesz` bytes of the file at
// `offset` to `start`. Fresh frames are zeroed, which covers the BSS.
static int load_segment(uint32_t* pd, fs_node_t* file, uint32_t offset,
                        uint32_t start, uint32_t filesz, uint32_t end) {
    for (uint32_t page = start & ~0xFFFu; page < end; page += PAGE_SIZE)
        if (vmm_map_user(pd, page) != 0) return -1;

    uint8_t* buf = (uint8_t*)kmalloc(PAGE_SIZE);
    if (!buf) return -1;
    uint32_t done = 0;
    while (done < filesz) {
        uint32_t chunk = filesz - done;
        if (chunk > PAGE_SIZE) chunk = PAGE_SIZE;
        if (read_fs(file, offset + done, chunk, buf) != chunk ||
            vmm_write(pd, start + done, buf, chunk) != 0) {
            kfree((uint32_t)buf);
            return -1;
        }
        done += chunk;
    }
    kfree((uint32_t)buf);
    return 0;
}

int elf_load(fs_node_t *file, int argc, char** argv) {
    if (!file) return -1;
    if (argc < 0 || argc > ELF_MAX_ARGS) return -1;

    Elf32_Ehdr header;
    if (read_fs(file, 0, sizeof(Elf32_Ehdr), (uint8_t*)&header) != sizeof(Elf32_Ehdr)) {
        print_string("ELF: Error reading header\n");
        return -1;
    }

    // Check magic
    if (*(uint32_t*)header.e_ident != ELF_MAGIC) {
        print_string("ELF: Invalid magic number\n");
        return -1;
    }

    // Validate the header before trusting any of its sizes/counts. Without
    // this, a crafted e_phentsize/e_phnum makes the program-header buffer
    // smaller than the loop walks it (heap over-read).
    if (header.e_type != 2 || header.e_machine != 3) { // ET_EXEC, EM_386
        print_string("ELF: Not a 32-bit executable\n");
        return -1;
    }
    if (header.e_phentsize != sizeof(Elf32_Phdr) ||
        header.e_phnum == 0 || header.e_phnum > 64) {
        print_string("ELF: Bad program header table\n");
        return -1;
    }

    uint32_t ph_bytes = (uint32_t)header.e_phnum * sizeof(Elf32_Phdr);
    Elf32_Phdr *phdrs = (Elf32_Phdr*)kmalloc(ph_bytes);
    if (!phdrs) {
        print_string("ELF: Out of memory for program headers\n");
        return -1;
    }
    if (read_fs(file, header.e_phoff, ph_bytes, (uint8_t*)phdrs) != ph_bytes) {
        print_string("ELF: Error reading program headers\n");
        kfree((uint32_t)phdrs);
        return -1;
    }

    // A fresh address space: this program's pages are invisible to every
    // other process, so several programs can be linked at USER_BASE.
    uint32_t* pd = vmm_create();
    if (!pd) {
        print_string("ELF: Out of memory (no free frames)\n");
        kfree((uint32_t)phdrs);
        return -1;
    }

    const char* err = 0;
    uint32_t image_end = USER_BASE;

    for (int i = 0; i < header.e_phnum && !err; i++) {
        if (phdrs[i].p_type != PT_LOAD) continue;
        uint32_t start = phdrs[i].p_vaddr;
        uint32_t end   = start + phdrs[i].p_memsz;

        if (start < USER_BASE || end > USER_HEAP_LIMIT || end < start)
            err = "ELF: Segment outside the user region (link at 0x40000000)\n";
        else if (phdrs[i].p_filesz > phdrs[i].p_memsz)
            err = "ELF: Corrupt segment (filesz > memsz)\n";
        else if (load_segment(pd, file, phdrs[i].p_offset, start,
                              phdrs[i].p_filesz, end) != 0)
            err = "ELF: Could not load segment (out of memory?)\n";
        else if (end > image_end)
            image_end = end;
    }
    kfree((uint32_t)phdrs);

    // The stack: USER_STACK_SIZE below USER_STACK_TOP, with argc/argv
    uint32_t user_esp = 0;
    for (uint32_t p = USER_STACK_TOP - USER_STACK_SIZE; !err && p < USER_STACK_TOP; p += PAGE_SIZE)
        if (vmm_map_user(pd, p) != 0) err = "ELF: Out of memory for the stack\n";
    if (!err && !(user_esp = setup_user_stack(pd, argc, argv)))
        err = "ELF: Arguments too long\n";

    if (err) {
        print_string((char*)err);
        vmm_destroy(pd);
        return -1;
    }

    // Run it in Ring 3. The heap (sbrk) starts at the page after the image.
    print_string("ELF: Launching in user mode (Ring 3)\n");
    uint32_t brk = (image_end + 0xFFF) & ~0xFFFu;
    int pid = create_user_task(file->name, header.e_entry, user_esp, brk, pd);
    if (pid < 0) vmm_destroy(pd);
    return pid;
}
