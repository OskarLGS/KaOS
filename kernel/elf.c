#include "elf.h"
#include "vfs.h"
#include "paging.h"
#include "memory.h"
#include "util.h"
#include "task.h"
#include "screen.h"

// Lay out argc/argv at the top of the user stack, the way crt0
// (user/crt0.asm) expects it: [esp] = argc, [esp+4] = argv, with argv
// pointing at a NULL-terminated array of strings stored just above.
// Returns the initial user stack pointer.
static uint32_t setup_user_stack(int argc, char** argv) {
    uint32_t sp = USER_STACK_TOP;
    uint32_t ptrs[ELF_MAX_ARGS];

    for (int i = argc - 1; i >= 0; i--) {
        int len = strlen(argv[i]) + 1;
        sp -= len;
        memory_copy(argv[i], (char*)sp, len);
        ptrs[i] = sp;
    }
    sp &= ~3u;

    sp -= 4; *(uint32_t*)sp = 0;            // argv[argc] = NULL
    for (int i = argc - 1; i >= 0; i--) {
        sp -= 4; *(uint32_t*)sp = ptrs[i];
    }
    uint32_t argv_addr = sp;
    sp -= 4; *(uint32_t*)sp = argv_addr;
    sp -= 4; *(uint32_t*)sp = (uint32_t)argc;
    return sp;
}

int elf_load(fs_node_t *file, int argc, char** argv) {
    if (!file) return -1;
    if (argc < 0 || argc > ELF_MAX_ARGS) return -1;

    // Every program is linked at USER_BASE: loading a second one would
    // overwrite the code of the one still running.
    if (user_task_running()) {
        print_string("ELF: Another user program is still running\n");
        return -1;
    }

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

    // Load segments at their linked virtual address. The user region is
    // identity-mapped with the User bit, so copying to p_vaddr is enough.
    for (int i = 0; i < header.e_phnum; i++) {
        if (phdrs[i].p_type == PT_LOAD) {
            uint32_t start = phdrs[i].p_vaddr;
            uint32_t end   = start + phdrs[i].p_memsz;

            if (start < USER_BASE || end > USER_END || end < start) {
                print_string("ELF: Segment outside the user region (link at 0xA00000)\n");
                kfree((uint32_t)phdrs);
                return -1;
            }
            if (phdrs[i].p_filesz > phdrs[i].p_memsz) {
                print_string("ELF: Corrupt segment (filesz > memsz)\n");
                kfree((uint32_t)phdrs);
                return -1;
            }

            read_fs(file, phdrs[i].p_offset, phdrs[i].p_filesz, (uint8_t*)start);

            // Zero the BSS part of the segment
            if (phdrs[i].p_memsz > phdrs[i].p_filesz) {
                memory_set((uint8_t*)(start + phdrs[i].p_filesz), 0,
                           phdrs[i].p_memsz - phdrs[i].p_filesz);
            }
        }
    }

    kfree((uint32_t)phdrs);

    // Run it in Ring 3
    print_string("ELF: Launching in user mode (Ring 3)\n");
    uint32_t user_esp = setup_user_stack(argc, argv);
    return create_user_task(file->name, header.e_entry, user_esp);
}
