# Filesystems: VFS, RAMFS & FAT32

## VFS (kernel/vfs.c)

Every filesystem exposes `fs_node_t` objects with function pointers
(`read`, `write`, `readdir`, `finddir`). Generic wrappers (`read_fs`,
`write_fs`, ...) dispatch through them. `fs_root` is the root node.

Node fields worth knowing:
- `inode` — filesystem-specific. FAT32 stores the **first cluster** here.
- `length` — file size in bytes (reads/writes are clamped to it).
- `impl` — FAT32 packs the location of the file's directory entry here
  (`dir_lba * 16 + entry_index`) so writes can update size/cluster.

## RAMFS (kernel/ramfs.c)

In-memory root with two demo files (`readme.txt`, `user.txt`, 512B max)
and a `disk` mountpoint that forwards to FAT32 when a disk is present.
This is what `ls` shows at the root.

## FAT32 (kernel/fat32.c)

Reads and writes a real FAT32 volume on the primary ATA disk
(`build/fat32.img` in QEMU). The BPB is read from LBA 0 (superfloppy
layout, no partition table).

### Reading
- `fat32_read` walks the cluster chain, clamping to the file size
  (cluster chains are rounded up to whole clusters — reading past EOF
  would return on-disk garbage).
- `fat32_readdir` / `fat32_finddir` parse 8.3 directory entries, skip
  LFN (attr `0x0F`) and the volume label (attr bit `0x08`), compare
  names **case-insensitively**, and propagate the directory attribute
  (`0x10`) to `FS_DIRECTORY`.

### Writing
- `ata_write_sector` (command `0x30` + cache FLUSH `0xE7`).
- `set_fat_entry` does read-modify-write of the FAT sector, preserving
  the reserved top 4 bits, and mirrors to the second FAT copy.
- `fat32_write` walks/extends the cluster chain (allocating free
  clusters with `find_free_cluster`), read-modify-writes partial
  sectors, then updates the directory entry (size + first cluster).
- Semantics are truncating: `file_size = offset + written` — `echo`
  always rewrites whole files. Freed tail clusters are not reclaimed.

### Creating / deleting (kernel/fat32.c)
- `fat32_create(dir, name)` — find a free 32-byte directory slot (`0x00`
  end or `0xE5` deleted), or allocate+link a new directory cluster if the
  chain is full; write an 8.3 entry (attr `0x20`, cluster 0, size 0).
- `fat32_delete(dir, name)` — free the file's cluster chain and mark the
  entry `0xE5`. Files only (not directories).
- `name_to_83` converts "notes.txt" → "NOTES   TXT".
- Wired through the VFS `create`/`unlink` ops on directory nodes.

### Not supported yet
`mkdir`, freeing clusters on truncate, long filenames (LFN), nested-path
resolution, file timestamps (no RTC — new files show 1980).

## Shell usage

```
ls            list the RAMFS root (readme.txt, user.txt, disk)
ls disk       list the FAT32 root
cat disk/hello.txt
echo new content > disk/notes.txt    (persists across reboots)
exec disk/app.elf                    (load + run ELF from disk)
```

Paths are one level deep (`dir/file`) — that is what the shell parses.
