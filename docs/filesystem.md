# Filesystems: VFS, RAMFS & FAT32

## VFS (kernel/vfs.c)

Every filesystem exposes `fs_node_t` objects with function pointers
(`read`, `write`, `readdir`, `finddir`). Generic wrappers (`read_fs`,
`write_fs`, ...) dispatch through them. `fs_root` is the root node.

Paths:
- `vfs_resolve("disk/docs/notes.txt")` walks any number of levels with
  one `finddir` per component (leading/repeated `/` ignored).
- `vfs_resolve_parent(path, leaf)` stops one level early and returns the
  parent directory plus the leaf name (for create/unlink).
- `finddir` may return a heap node: FAT32 sets `FS_ALLOCATED` (0x80) in
  `flags`. Always hand results to `vfs_release()`, which frees only those
  (RAMFS nodes and the FAT32 root are static). Mask the type with
  `flags & 0x7`.

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

### Directories and long names (kernel/fat32.c)

Every directory operation goes through **`dir_walk`**: it visits each
live entry in disk order and hands over the short entry, its position,
the positions of its LFN entries and its name — the long name when a
valid LFN run precedes it, else the 8.3 display name.

**Long file names (LFN)**: a run of attr-0x0F entries before the short
entry, last part first (order number | 0x40), 13 UCS-2 characters each,
plus a checksum of the 8.3 name. `dir_walk` accepts a run only if the
order numbers count down to 1 without gaps and every checksum matches
the short entry, so stale fragments are ignored. Characters outside
ASCII show as `?`. Names are up to 127 characters.

| Operation | What it does |
|-----------|--------------|
| lookup (`finddir`) | Case-insensitive match on the long name **or** the 8.3 alias (`NOTAS-~1.MAR`) |
| `fat32_create` | Plain one-case 8.3 names → short entry only (shown upper case). Anything else → alias `BASIS~N.EXT` (unique, Windows scheme) + LFN entries |
| `add_entry` | Finds `parts + 1` *consecutive* free slots (`0x00`/`0xE5`), growing the directory by zeroed clusters if needed |
| `fat32_mkdir` | Zeroed cluster with `.` and `..` (cluster 0 when the parent is the root; `finddir` maps it back), then `add_entry` with attr 0x10 |
| `fat32_delete` | Frees the chain and marks the short entry **and its LFN entries** `0xE5`. Directories only when empty (-2) |
| `fat32_rename` | Adds the entry (same cluster, size, timestamps) under the new name/directory, removes the old one; a moved directory's `..` is updated. Refuses moving a directory into itself (`VFS_EINVAL`) |

Invalid names (control chars, `\ / : * ? " < > |`, trailing dot/space,
over 127 chars) return `VFS_EBADNAME`.

On mount, the FSInfo free-cluster count and next-free hint are set to
"unknown" (0xFFFFFFFF): KaOS does not maintain them, so other systems
recompute them.

### Locking

The ATA driver and FAT32 code are not reentrant, and the shell runs
preemptibly. Every VFS wrapper (`read_fs`, `finddir_fs`, `create_fs`,
`rename_fs`, ...) takes one filesystem lock; waiters `yield()`. A holder
never yields inside an operation, and FAT32's internal static buffers
rely on this serialization.

### Limits
A write sets the file size to the end of that write (fine for
sequential writes from offset 0; no append-in-the-middle). No NT
lower-case flags: a one-case 8.3 name is stored upper case. The shell
splits arguments on spaces, so names with spaces are readable but can't
be typed in commands.

## Shell usage

```
ls            list the RAMFS root (readme.txt, user.txt, disk)
ls disk       list the FAT32 root
cat disk/hello.txt
echo new content > disk/notes.txt    (persists across reboots)
exec disk/app.elf                    (load + run ELF from disk)
mkdir disk/proy/kaos                 (parent must exist)
cat disk/proy/kaos/../../hello.txt   (any depth, . and .. work on FAT32)
rm disk/proy                         (files, or directories once empty)
touch disk/Notas-del-Proyecto.txt    (long name; alias NOTAS-~1.TXT)
cp readme.txt disk/proy               (into a directory, any filesystem)
mv disk/proy/readme.txt disk/Leeme.txt   (rename/move, no data copied)
```

`cp`/`mv` with a directory as target keep the source name. `mv` within
FAT32 is a rename; between RAMFS and FAT32 it copies then deletes the
source (RAMFS can't delete, so the original stays and you are told).
