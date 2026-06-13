# FAT32 Write Support — Design

**Date:** 2026-06-13
**Status:** In progress (continuation of delegated development)

## Goal

`echo text > disk/file.txt` writes through to the ATA disk and survives
reboots. Scope: overwrite/extend **existing** files (cmd_echo already
documents that file creation is not supported yet).

## Components

1. **ports.c/h** — add `port_word_out` (ATA PIO writes 16 bits at a time).

2. **ata.c/h** — `ata_write_sector(lba, buffer)`: command 0x30, wait DRQ,
   write 256 words, wait not-BSY, then FLUSH CACHE (0xE7) so the write
   reaches the disk image. Same timeout discipline as the read path.

3. **fat32.c**:
   - `get_fat_entry` / `set_fat_entry` — read-modify-write the FAT sector;
     mirror writes to the second FAT (offset `fat_size_32`).
   - `find_free_cluster` — first FAT entry with value 0.
   - `update_dir_entry(node)` — the node remembers where its directory
     entry lives via `impl = dir_lba * 16 + entry_index` (set by finddir);
     rewrite `file_size` and first-cluster fields.
   - `fat32_write(node, offset, size, buf)` — walk the cluster chain to
     `offset`, allocating and linking clusters as needed (also when the
     file is empty); read-modify-write partial sectors; set
     `file_size = offset + size` (truncating semantics — echo always
     rewrites the whole file) and update the dir entry.
   - finddir: hook `res->write = fat32_write`, record `impl`.

4. **cmd_fs.c** — `cmd_echo`: parse one-level `dir/file` paths the same
   way `cmd_cat` does (today it can't reach `disk/...` at all).

## Out of scope

Creating/deleting files, freeing truncated tail clusters, LFN entries.

## Verification

1. In KaOS: `echo Persistencia desde KaOS! > disk/notes.txt`, then `cat`.
2. Power off QEMU; on the host, `mtype -i fat32.img ::/NOTES.TXT` must
   show the new content (proves it reached the disk image, not a cache).
3. Boot again, `cat disk/notes.txt` still shows it.
