#!/bin/bash
# Build KaOS and run the end-to-end tests (tests/kaos_test.py) in QEMU.
#   ./test.sh            all tests
#   ./test.sh -k users   only tests whose name contains "users"
#   ./test.sh --keep     keep logs/disks of a passing run
# Needs: qemu-system-i386, mtools, dosfstools, python3.
set -e
cd "$(dirname "$0")"
bash build.sh > build/build.log 2>&1 || { cat build/build.log; exit 1; }
grep "Kernel size" build/build.log
python3 tests/kaos_test.py "$@"
