# AGENTS.md — Working on KaOS

Guidance for AI agents and developers working on this repository.

## What this is

KaOS is an educational x86 (32-bit) operating system: a 16-bit bootloader
(KaBooM), a protected-mode kernel in C, a graphical desktop, a shell, FAT32
read/write, and Ring 3 user programs. Everything is built from scratch;
there is no libc and no external dependencies inside the kernel.

## Build

Building happens **in WSL** (or any Linux with `gcc-multilib`, `nasm`,
`dosfstools`, `mtools`):

```bash
wsl bash build.sh          # from Windows
bash build.sh              # from Linux/WSL
```

Outputs:
- `kaos.img` — bootable 1.44MB floppy image (bootloader + kernel).
- `build/fat32.img` — 64MB FAT32 hard-disk image with the Ring 3 user
  programs (`APP.ELF`, `CRASH.ELF`). Created on first build, **persists**
  afterwards (user data written from inside KaOS survives rebuilds; delete
  it to start fresh).

The script fails if the kernel outgrows its slot (`KERNEL_SECTORS` at the
top of build.sh, 512 sectors = 256KB).
Current size is printed on every build (~107KB used).

## Run in QEMU

The image **must** be attached as a floppy (the loader uses floppy CHS
geometry), and the FAT32 image as an IDE disk. `-boot order=a` makes the
floppy boot first.

Windows (native QEMU, installed at `C:\Program Files\qemu`):
```powershell
& "C:\Program Files\qemu\qemu-system-i386.exe" -drive format=raw,file=kaos.img,if=floppy -drive format=raw,file=build\fat32.img,if=ide -boot order=a -netdev user,id=n0 -device rtl8139,netdev=n0
```

WSL/Linux:
```bash
qemu-system-i386 -drive format=raw,file=kaos.img,if=floppy \
                 -drive format=raw,file=build/fat32.img,if=ide -boot order=a \
                 -netdev user,id=n0 -device rtl8139,netdev=n0
```

Prefer **native Windows QEMU** for interactive use: mouse capture
(click window / Ctrl+Alt+G to release) does not work under WSLg.

Boot menu: option 1 = graphical desktop (800x600), option 2 = recovery
stub, option 3 = text-mode shell.

## Headless verification (the important part)

The kernel logs one-character milestones to debug port 0xE9 during boot.
A fully booted graphical session prints exactly:

```
GPQVTRKMS!DW
```

(`G`raphics, `P`aging start, `Q` paging done, `V`BE fill, `T`asking,
time`R`, `K`eyboard, `M`ouse, `S`hell, `!` = sti, `D`esktop, `W`indows.)
Text mode prints `GTRKS!`. Any prefix of these = it hung right after the
last letter shown. Add `dbg('X')` calls in `kernel/main.c` to bisect.
The same port also receives **every console character** (print_char
mirrors it), so the log is a full transcript of the session with the
milestone letters interleaved.

## Kernel panics

A fault in Ring 0 (or a kernel stack overflow) shows a full report —
exception, CR2, all registers, running task, CR3 — drawn straight to
the screen (red screen on the desktop) and mirrored to the debug port.
Turn its EIP into a function with `build/kernel.map` (written by
build.sh: the last entry whose address is ≤ EIP). `panic` (root) faults
on purpose to try it.

## Automated tests

`./test.sh` builds and runs `tests/kaos_test.py`: seven QEMU sessions
(desktop, files, programs, users, network, ES keyboard, panic), each on a throw-away copy
of `build/fat32.img` (your disk is never touched), driven through the
QEMU monitor and checked against the console transcript, plus host-side
checks (mtools, `cmp`, `fsck.fat`). It starts its own HTTP servers for
the network tests. `./test.sh -k users` runs one; `--keep` keeps logs.
Takes ~1.5 minutes; run it before calling work done.

Scripted end-to-end test pattern (boot, type, screenshot — all headless):

```bash
qemu-system-i386 -drive format=raw,file=kaos.img,if=floppy \
  -drive format=raw,file=build/fat32.img,if=ide -boot order=a \
  -netdev user,id=n0 -device rtl8139,netdev=n0 \
  -display none -debugcon file:build/debug.log \
  -monitor telnet:127.0.0.1:4555,server,nowait &
sleep 2
exec 3<>/dev/tcp/127.0.0.1/4555      # bash builtin TCP, no telnet needed
echo "sendkey ret" >&3               # pick boot menu option 1
sleep 4
for k in h e l p ret; do echo "sendkey $k" >&3; sleep 0.2; done
echo "screendump build/shot.ppm" >&3; sleep 1
echo "quit" >&3
```

Useful monitor commands: `sendkey <key>` (`spc`, `ret`, `dot`, `slash`,
`shift-dot` for `>`, `down`, `up`, `pgup`, `ctrl-c`), `mouse_move dx dy` (guest moves 2x because
of MOUSE_SPEED), `mouse_button 1`/`0`, `screendump file.ppm`.
Convert screenshots with Python/PIL: `Image.open("x.ppm").save("x.png")`.

FAT32 writes can be verified from the host without booting:
`mtype -i build/fat32.img ::/NOTES.TXT`.

## Invariants that must stay in sync

| Value | Defined in | Must match |
|-------|-----------|------------|
| Kernel slot `KERNEL_SECTORS` at LBA 2 | `build.sh` (passed to NASM with `-D`) | used by `kernel/kernel.asm` (load loop) and `boot/boot.asm` (recovery LBA); must stay ≤ 640 so the load (at 0x1000) stays below the VBR at 0x90000 |
| User virtual base 0x40000000 (`USER_BASE`) | `kernel/paging.h` | link address in `user/user.ld` |
| Frame pool 10–16 MB, heap 1–9 MB | `kernel/paging.h`, `kernel/memory.h` | must not overlap; both inside the 16 MB identity map |
| Kernel selectors 0x08/0x10 | `kernel/gdt.c` | `kernel/kernel.asm` GDT, all interrupt stubs, `entry.asm` |
| VBE info block at 0x0900 | `kernel/kernel.asm` | `vbe_info_t` read in `kernel/graphics.c` |
| Terminal window 500x350 | `kernel/main.c` | `TERM_COLS/ROWS` in `kernel/terminal.c` |

## Code conventions and gotchas

- Freestanding C: only `kernel/util.h` helpers exist (`strcmp`, `strcpy`,
  `memory_copy`, `int_to_ascii`, ...). No libc. `memory_copy`/`memory_set`
  use `rep movsl/stosb` — keep them fast, the GUI copies 1.9MB per frame.
- **Never `cli` without restoring EFLAGS** (`pushf; pop; cli ... sti if IF
  was set`). A bare `cli` once froze the keyboard system-wide after the
  caller's context was saved with IF=0.
- **State shared with an ISR must be `volatile`** (keyboard ring buffer,
  mouse x/y/buttons, timer tick). Under `-O2` the optimizer caches
  non-volatile globals in registers in the polling loops and never sees
  the interrupt's updates.
- **Interrupt handlers get `struct registers*`, never the struct by
  value.** By value, the saved frame *is* the C argument area and GCC
  may reuse it (a tail call once wrote a pid over the saved DS → panic
  after Ctrl+C). The stubs `push esp`.
- **Inline asm must declare every clobber.** The old PIC remap wrote `al`
  without listing it as clobbered; `-O2` then miscompiled it and left the
  keyboard IRQ masked. Prefer the `port_byte_out` helpers over hand-rolled
  `out` sequences.
- The build is **`-O2 -fno-strict-aliasing`**. `-fno-strict-aliasing` is
  mandatory — the FS/ELF code type-puns buffers; do not drop it.
- All shell output goes through `print_string` (kernel/screen.c). When a
  print sink is set (`set_print_sink`), output is redirected — in desktop
  mode `terminal_init` installs the graphical terminal as the permanent
  sink, so output from user programs lands in its window too.
- The scheduler is **preemptive** (IRQ0 drives `schedule()`), and tasks
  may still `yield()` cooperatively. Anything touching the task table
  must save EFLAGS and `cli` (see `kernel/task.c`).
- Filesystem calls go through the VFS wrappers, which hold one lock
  (`fs_lock` in vfs.c). Never call a wrapper from inside a filesystem
  driver (deadlock), and don't make FAT32 code yield. `dir_walk` is not
  reentrant: visitors must not walk another directory.
- `kmalloc`/`kfree` run with interrupts off (shared by all tasks and
  reached from the Ctrl+C IRQ). Release `finddir`/`vfs_resolve` results
  with `vfs_release()` — FAT32 hands out heap nodes.
- User programs: any inline `int 0x80` must clobber `"memory"` (see
  `user/syscall.h`), or GCC drops stores to buffers the kernel reads.
- Text is **Latin-1** (one byte per char; ñ = 0xF1). Compare/fold case
  with `latin1_lower`, draw with `latin1_glyph`; chars ≥ 0x80 are
  negative as `char`, so cast to `uint8_t` before range checks.
- `term_grid` rows have a stride of `MAX_TERM_COLS`, not `term_cols`:
  clear/scroll it row by row or by `sizeof`, never as a flat
  `term_rows * term_cols` block.
- ATA waits must keep their timeouts and 0xFF floating-bus checks: with no
  disk attached the status port reads 0xFF with BSY set forever.
- Design docs for completed features live in `docs/superpowers/specs/`.
  Subsystem documentation lives in `docs/` (English, concise, tables).

Network tests: QEMU's 10.0.2.2 is the host's loopback, so run
`python3 -m http.server 8080 --bind 127.0.0.1` on the host and
`fetch 10.0.2.2:8080/<file> > disk/x` in KaOS, then compare with
`mtype -i build/fat32.img ::/x | cmp - <file>`. `ping 10.0.2.2` must
answer. Don't stop test servers with `pkill -f <pattern>` from a shell
whose own command line contains the pattern — it kills that shell.

Accounts: a fresh `build/fat32.img` boots into first-boot setup (type
the root password twice), later boots into `KaOS login:`. Scripted tests
must log in first (type the user name + `ret`, then the password +
`ret`). To start over without accounts: `mdel -i build/fat32.img
::/etc/passwd`.

## Quick smoke test before calling work done

1. `bash build.sh` — builds clean, kernel size under limit.
2. Headless graphical boot → trail is `GPQVTRKMS!DW`.
3. Headless text boot (`down down ret`) → trail `GTRKS!`, `help` works.
4. If you touched storage: `ls disk` / `cat disk/hello.txt` /
   `echo x > disk/notes.txt` + host-side `mtype` check; long names
   (`touch disk/Nombre-Largo.txt`, then `mdir -i build/fat32.img ::`),
   `cp`/`mv`, and `fsck.fat -n build/fat32.img` reports no errors (only
   "Free cluster summary uninitialized", which is intended).
5. If you touched tasks/usermode: `exec disk/app.elf` prints and reports
   `[exited with code 0]`, `exec disk/args.elf a b` lists its argv,
   `exec disk/crash.elf` reports `[crashed: Page Fault]` without taking
   the system down, `exec disk/cp.elf readme.txt disk/docs/c.txt` +
   `cat disk/docs/c.txt`, `exec disk/sleep.elf 2`, `exec disk/heap.elf`,
   `exec disk/iso.elf A &` + `exec disk/iso.elf B` (both "ok", each
   reported), `exec disk/badptr.elf` (all refused), `paging` back to
   0 KB in use once everything ended, `exec disk/loop.elf` stops on Ctrl+C (`sendkey
   ctrl-c`), and `uptime` still answers afterwards. Check the desktop
   terminal too (click it first: the About window starts focused).
