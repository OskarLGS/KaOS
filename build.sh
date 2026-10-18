#!/bin/bash
# Build script for KaOS
# Usage: ./build.sh (from WSL)

set -e

echo "=== KaOS Build System ==="

# Directories
BOOT_DIR="boot"
KERNEL_DIR="kernel"
BUILD_DIR="build"
OUTPUT="kaos.img"

# Kernel slot on the floppy, in 512-byte sectors. The VBR loads it at
# physical 0x1000, so it must stay well below the VBR itself at 0x90000
# (640 sectors max); 512 = 256 KB.
KERNEL_SECTORS=512
KERNEL_MAX=$((KERNEL_SECTORS * 512))

# Create build dir
mkdir -p "$BUILD_DIR"

# Flags for freestanding kernel code (no host libc, no stack protector, no PIC).
# -O2: the single biggest perf lever — inlines the hot GUI primitives and
#      keeps values in registers (was -O0, leaving 2-5x on the table).
# -fno-strict-aliasing: REQUIRED — the FS/ELF code type-puns buffers
#      (*(uint32_t*)&buf[off], casts to fat32_entry_t*); -O2 would otherwise
#      miscompile it. Do not remove.
# -march=i686: target the real CPU (686 instructions).
CFLAGS="-m32 -ffreestanding -fno-pie -fno-pic -fno-stack-protector -fno-builtin"
CFLAGS="$CFLAGS -O2 -fno-strict-aliasing -march=i686"

# Compile bootloader
echo "[1/5] Compiling bootloader..."
nasm -f bin -D KERNEL_SECTORS=$KERNEL_SECTORS "$BOOT_DIR/boot.asm" -o "$BUILD_DIR/boot.bin"

# Compile C kernel & drivers
echo "[2/5] Compiling C kernel and drivers..."
nasm -f bin -D KERNEL_SECTORS=$KERNEL_SECTORS "$KERNEL_DIR/kernel.asm" -o "$BUILD_DIR/kernel.bin"
nasm -f elf32 "$KERNEL_DIR/entry.asm" -o "$BUILD_DIR/entry.o"
nasm -f elf32 "$KERNEL_DIR/interrupts.asm" -o "$BUILD_DIR/interrupts.o"
nasm -f elf32 "$KERNEL_DIR/task_switch.asm" -o "$BUILD_DIR/task_switch.o"
nasm -f elf32 "$KERNEL_DIR/usermode.asm" -o "$BUILD_DIR/usermode.o"

# Compile all .c files dynamically
for cfile in "$KERNEL_DIR"/*.c "$KERNEL_DIR"/commands/*.c "$KERNEL_DIR"/net/*.c; do
  if [ -f "$cfile" ]; then
    obj_name=$(basename "${cfile%.c}.o")
    gcc $CFLAGS -c "$cfile" -o "$BUILD_DIR/$obj_name"
  fi
done

# We must ensure entry.o is the first object linked
OBJS=("$BUILD_DIR/entry.o")

# Collect all other objects (user_*.o are Ring 3 programs, not kernel code)
for obj in "$BUILD_DIR"/*.o; do
  if [[ "$obj" != *"/entry.o" && "$(basename "$obj")" != user_* ]]; then
    OBJS+=("$obj")
  fi
done

ld -m elf_i386 -T "$KERNEL_DIR/linker.ld" "${OBJS[@]}" -o "$BUILD_DIR/full_kernel.bin"

# Same link as ELF, only for its symbol table: build/kernel.map lists
# every function's address, to turn a panic's EIP into a name.
sed 's/OUTPUT_FORMAT("binary")//' "$KERNEL_DIR/linker.ld" > "$BUILD_DIR/linker_elf.tmp"
ld -m elf_i386 --no-warn-rwx-segments -T "$BUILD_DIR/linker_elf.tmp" "${OBJS[@]}" \
   -o "$BUILD_DIR/kernel-symbols.elf" 2>/dev/null
nm -n "$BUILD_DIR/kernel-symbols.elf" | grep -i " t " > "$BUILD_DIR/kernel.map"
rm -f "$BUILD_DIR/linker_elf.tmp"

# Compile Recovery
echo "[3/5] Compiling recovery..."
nasm -f bin "$KERNEL_DIR/recovery.asm" -o "$BUILD_DIR/recovery.bin"

# Combine into disk image
echo "[4/5] Creating disk image..."
# LBA 0                  : MBR / KaBooM (512)
# LBA 1                  : Kernel Bridge / VBR (512)
# LBA 2 .. 2+N-1         : C Kernel (N = KERNEL_SECTORS)
# LBA 2+N                : Recovery (512)
# kernel.asm (load loop) and boot.asm (recovery LBA) get N via -D above.

# Refuse to build if the kernel outgrew its slot
KERNEL_SIZE=$(stat -c%s "$BUILD_DIR/full_kernel.bin")
if [ "$KERNEL_SIZE" -gt "$KERNEL_MAX" ]; then
  echo "ERROR: kernel is $KERNEL_SIZE bytes, exceeds $KERNEL_MAX ($KERNEL_SECTORS sectors)." >&2
  echo "Raise KERNEL_SECTORS at the top of build.sh." >&2
  exit 1
fi
echo "Kernel size: $KERNEL_SIZE / $KERNEL_MAX bytes"

truncate -s "$KERNEL_MAX" "$BUILD_DIR/full_kernel.bin"

nasm -f bin "$KERNEL_DIR/recovery.asm" -o "$BUILD_DIR/recovery.bin"
truncate -s 512 "$BUILD_DIR/recovery.bin"

# Concatenate in order
cat "$BUILD_DIR/boot.bin" \
    "$BUILD_DIR/kernel.bin" \
    "$BUILD_DIR/full_kernel.bin" \
    "$BUILD_DIR/recovery.bin" > "$BUILD_DIR/os.bin"

# Pad to 1.44MB floppy size
echo "[5/5] Padding to floppy size..."
cp "$BUILD_DIR/os.bin" "$OUTPUT"
truncate -s 1474560 "$OUTPUT"

# --- User programs (Ring 3) ---------------------------------------------
# Built as ELF linked at 0xA00000 (see user/user.ld and kernel/paging.h)
# and shipped on the FAT32 test disk for `exec disk/app.elf`.
# Every program is linked with crt0 (user/crt0.asm), which calls
# main(argc, argv) and exits with its return value.
echo "[+] Building user programs..."
nasm -f elf32 user/crt0.asm -o "$BUILD_DIR/user_crt0.o"
for ufile in user/*.c; do
  [ -f "$ufile" ] || continue
  uname=$(basename "${ufile%.c}")
  gcc $CFLAGS -c "$ufile" -o "$BUILD_DIR/user_$uname.o"
  # One RWX segment is expected: user.ld has no separate PHDRS for data
  ld -m elf_i386 --no-warn-rwx-segments -T user/user.ld "$BUILD_DIR/user_crt0.o" "$BUILD_DIR/user_$uname.o" -o "$BUILD_DIR/$uname.elf"
done

if command -v mkfs.fat >/dev/null && command -v mcopy >/dev/null; then
  if [ ! -f "$BUILD_DIR/fat32.img" ]; then
    echo "[+] Creating FAT32 test disk..."
    dd if=/dev/zero of="$BUILD_DIR/fat32.img" bs=1M count=64 status=none
    mkfs.fat -F 32 -n KAOSDISK "$BUILD_DIR/fat32.img" >/dev/null
  fi
  for elf in "$BUILD_DIR"/*.elf; do
    [ -f "$elf" ] || continue
    [ "$(basename "$elf")" = "full_kernel.elf" ] && continue
    mcopy -o -i "$BUILD_DIR/fat32.img" "$elf" ::/ 2>/dev/null || true
  done
  echo "[+] User programs copied to fat32.img"
else
  echo "[!] mkfs.fat/mcopy not found - skipping FAT32 disk (apt install dosfstools mtools)"
fi

echo "=== Build complete: $OUTPUT ==="
