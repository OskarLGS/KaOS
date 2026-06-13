# FAT32 Create / Delete Files — Design

**Date:** 2026-06-13
**Status:** In progress (Phase 7: complete storage)

## Goal

Complete the file lifecycle on the real FAT32 disk:
- `touch disk/file.txt` — create an empty file.
- `rm disk/file.txt` — delete a file (free its clusters).
- `echo text > disk/new.txt` — create the file if it doesn't exist, then
  write (today echo can only write to existing files).

Changes persist across reboots, verifiable from the host with `mdir`/`mtype`.

## VFS additions (kernel/vfs.c/.h)

Two new optional operations on `fs_node_t`:
- `create(dir_node, name)` → make an empty entry, return 0/-1.
- `unlink(dir_node, name)` → remove an entry, return 0/-1.

Wrappers `create_fs` / `unlink_fs` dispatch through them (only directory
nodes that implement them; RAMFS root returns "unsupported").

## FAT32 implementation (kernel/fat32.c)

Helper `name_to_83(name, out[11])` — convert "notes.txt" → "NOTES   TXT"
(uppercase, space-padded, split on the dot).

`fat32_create(dir, name)`:
1. Reject if the name already exists.
2. Walk the directory's cluster chain looking for a free 32-byte slot
   (`name[0] == 0x00` end-of-dir, or `0xE5` deleted).
3. If the chain is full, allocate + link a fresh (zeroed) cluster.
4. Write a new entry: 8.3 name, attr `0x20` (archive), first cluster 0,
   size 0. (A 0x00 slot leaves the following slot 0x00 → still terminal.)

`fat32_delete(dir, name)`:
1. Find the entry; refuse if it's a directory (rm is files-only for now).
2. Free its cluster chain (`set_fat_entry(cl, 0)` along the chain).
3. Mark the directory entry deleted (`name[0] = 0xE5`), rewrite the sector.

Directory nodes (the FAT32 root and any subdir from finddir) get their
`create`/`unlink` pointers set.

## Shell (kernel/commands/cmd_fs.c)

- `touch <path>` and `rm <path>`: resolve one-level `dir/file`, call
  `create_fs` / `unlink_fs` on the parent directory.
- `cmd_echo`: if `finddir` misses, `create_fs` then re-find before writing.

## Verification

In KaOS: `touch disk/new.txt`; `ls disk` shows it; `echo hi > disk/new.txt`;
`cat` shows "hi"; `rm disk/new.txt`; `ls disk` no longer lists it. Power
off; host `mdir -i build/fat32.img ::/` reflects the changes. Reboot;
state persists. Regression: graphical `GPQVTRKMS!DW`, Ring 3, spin.

## Out of scope

`mkdir` (needs a new cluster with `.`/`..`), freeing clusters on truncate,
LFN, nested paths.
