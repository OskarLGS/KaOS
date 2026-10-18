#!/usr/bin/env python3
"""KaOS end-to-end tests.

Boots KaOS in QEMU (headless) on a throw-away copy of the FAT32 disk,
types commands through the QEMU monitor and checks what the kernel
prints. The kernel mirrors all console output to debug port 0xE9, which
QEMU writes to a log file: checks wait for text there (no fixed sleeps,
nothing lost to scrolling). Host-side checks use mtools and fsck.fat.

Run through ../test.sh (which builds first), or directly:
    python3 tests/kaos_test.py [-k name]
"""
import argparse, http.server, os, shutil, socket, socketserver, subprocess
import sys, tempfile, threading, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROOT_PW, USER, USER_PW = "toor", "oskar", "kaos1"

# Characters typed with sendkey (US layout, as the guest expects)
KEYS = {' ': 'spc', '.': 'dot', '/': 'slash', '-': 'minus', '_': 'shift-minus',
        ':': 'shift-semicolon', '>': 'shift-dot', '<': 'shift-comma', '&': 'shift-7',
        '~': 'shift-grave_accent', '=': 'equal', ',': 'comma', '\n': 'ret'}


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


class Fail(Exception):
    pass


class Kaos:
    """One QEMU instance running KaOS."""

    def __init__(self, workdir, disk, mode="text", net=True):
        self.log_path = os.path.join(workdir, f"console-{mode}.log")
        self.pos = 0                      # Log offset already matched
        self.mport = free_port()
        cmd = ["qemu-system-i386",
               "-drive", f"format=raw,file={os.path.join(ROOT, 'kaos.img')},if=floppy,readonly=on",
               "-drive", f"format=raw,file={disk},if=ide",
               "-boot", "order=a", "-display", "none",
               "-debugcon", f"file:{self.log_path}",
               "-monitor", f"tcp:127.0.0.1:{self.mport},server,nowait"]
        if net:
            cmd += ["-netdev", "user,id=n0", "-device", "rtl8139,netdev=n0"]
        self.qemu = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        for _ in range(50):
            try:
                self.mon = socket.create_connection(("127.0.0.1", self.mport))
                break
            except OSError:
                time.sleep(0.1)
        else:
            raise Fail("QEMU monitor did not come up")
        # Pick the boot-menu entry. Keys sent before KaBooM listens are
        # lost and the menu then waits forever, so repeat until the kernel
        # writes its first character to the debug port.
        for attempt in range(6):
            time.sleep(0.5)
            if mode == "text":
                self.key("down"); self.key("down")
            self.key("ret")
            end = time.time() + 3
            while time.time() < end and not self.output():
                time.sleep(0.1)
            if self.output():
                break
        else:
            self.close()
            raise Fail("KaOS did not start (boot menu ignored the keys?)")

    def key(self, k, delay=0.05):
        self.mon.sendall(f"sendkey {k}\n".encode())
        time.sleep(delay)

    def type(self, text):
        for ch in text:
            if ch.isupper():
                self.key("shift-" + ch.lower())
            else:
                self.key(KEYS.get(ch, ch))

    def line(self, text):
        self.type(text + "\n")

    def mouse(self, dx, dy, click=True):
        self.mon.sendall(f"mouse_move {dx} {dy}\n".encode()); time.sleep(0.3)
        if click:
            self.mon.sendall(b"mouse_button 1\n"); time.sleep(0.15)
            self.mon.sendall(b"mouse_button 0\n"); time.sleep(0.3)

    def output(self):
        try:
            with open(self.log_path, "rb") as f:
                return f.read().decode("latin-1")
        except FileNotFoundError:
            return ""

    def expect(self, text, timeout=10):
        """Wait until `text` appears after the previous match."""
        end = time.time() + timeout
        while time.time() < end:
            out = self.output()
            i = out.find(text, self.pos)
            if i >= 0:
                self.pos = i + len(text)
                return
            time.sleep(0.1)
        tail = self.output()[self.pos:][-300:].replace("\r", "")
        raise Fail(f"expected {text!r}; output after last match ends with:\n{tail}")

    def run(self, cmd, *expected, timeout=10):
        self.line(cmd)
        for e in expected:
            self.expect(e, timeout)

    def close(self):
        try:
            self.mon.sendall(b"quit\n")
            self.qemu.wait(timeout=5)
        except Exception:
            self.qemu.kill()


# ─── Host-side helpers ────────────────────────────────────────────────────────

def mtype(disk, path):
    return subprocess.run(["mtype", "-i", disk, "::" + path], capture_output=True).stdout


def fsck_ok(disk):
    r = subprocess.run(["fsck.fat", "-n", disk], capture_output=True, text=True)
    bad = [l for l in r.stdout.splitlines()
           if l and not l.startswith(("fsck.fat", "Free cluster summary", "Leaving", disk))]
    return r.returncode in (0, 1) and not bad, r.stdout


class SlowHandler(socketserver.BaseRequestHandler):
    """HTTP response that trickles 200 bytes every 100 ms (for Ctrl+C tests)."""
    def handle(self):
        try:
            self.request.recv(1024)
            self.request.sendall(b"HTTP/1.0 200 OK\r\n\r\n")
            for i in range(600):
                self.request.sendall(b"x" * 199 + b"\n")
                time.sleep(0.1)
        except OSError:
            pass


def start_servers(www):
    os.makedirs(www, exist_ok=True)
    with open(os.path.join(www, "hola.txt"), "w") as f:
        f.write("Hola desde el host!\n")
    with open(os.path.join(www, "grande.txt"), "w") as f:
        f.write("".join(f"linea {i:05d} {'abcdefghij' * 4}\n" for i in range(1200)))

    handler = lambda *a, **k: http.server.SimpleHTTPRequestHandler(*a, directory=www, **k)
    http.server.SimpleHTTPRequestHandler.log_message = lambda *a: None
    web = socketserver.ThreadingTCPServer(("127.0.0.1", free_port()), handler)
    slow = socketserver.ThreadingTCPServer(("127.0.0.1", free_port()), SlowHandler)
    for srv in (web, slow):
        srv.daemon_threads = True
        threading.Thread(target=srv.serve_forever, daemon=True).start()
    return web.server_address[1], slow.server_address[1]


# ─── Tests ────────────────────────────────────────────────────────────────────

def test_gui_boot(ctx):
    k = Kaos(ctx.tmp, ctx.fresh_disk("gui"), mode="gui", net=False)
    try:
        # The milestone letters (G P Q V T ...) are interleaved with the
        # mirrored console text; reaching the desktop's first-boot prompt
        # means every milestone passed.
        k.expect("Choose a password for root: ", 15)
        k.mouse(-80, -110)                   # Focus the terminal window
        k.line(ROOT_PW); k.expect("Retype it: ")
        k.line(ROOT_PW); k.expect("KaOS login: ")
        k.line("root"); k.expect("Password: ")
        k.line(ROOT_PW); k.expect("root# ")
        k.run("uptime", "Uptime: ")
        k.run("exec disk/args.elf hola", "argv[1] = hola", "[exited with code 1]")
    finally:
        k.close()


def session(ctx, name, net=True):
    """A text-mode KaOS with a fresh disk, logged in as root."""
    disk = ctx.fresh_disk(name)
    k = Kaos(ctx.tmp, disk, net=net)
    try:
        k.expect("Choose a password for root: ", 15)
        k.line(ROOT_PW); k.expect("Retype it: ")
        k.line(ROOT_PW); k.expect("KaOS login: ")
        k.line("root"); k.expect("Password: ")
        k.line(ROOT_PW); k.expect("Welcome, root.")
        k.expect("root# ")
    except Exception:
        k.close()               # Don't leave QEMU holding the disk
        raise
    return k, disk


def test_shell_and_files(ctx):
    k, disk = session(ctx, "files", net=False)
    try:
        k.run("help", "fetch", "useradd")
        k.run("echo hola mundo > disk/notes.txt", "Written to file.")
        k.run("cat disk/notes.txt", "hola mundo")
        k.run("mkdir disk/Proyectos-2026", "Created.")
        k.run("cp readme.txt disk/Proyectos-2026", "Copied 52 bytes.")
        k.run("mv disk/Proyectos-2026/readme.txt disk/Proyectos-2026/Leeme-Largo.txt", "Moved.")
        k.run("cat disk/proyectos-2026/leeme-largo.txt", "Hello from KaOS!")
        k.run("rm disk/Proyectos-2026", "Directory not empty.")
        k.run("mv disk/Proyectos-2026 disk/Proyectos-2026/x", "Cannot move a directory into itself.")
        k.run("date", "20")
    finally:
        k.close()
    if mtype(disk, "/notes.txt").strip() != b"hola mundo":
        raise Fail("host can't read disk/notes.txt back")
    if b"Hello from KaOS" not in mtype(disk, "/Proyectos-2026/Leeme-Largo.txt"):
        raise Fail("long-name file not readable from the host")
    ok, report = fsck_ok(disk)
    if not ok:
        raise Fail("fsck.fat found problems:\n" + report)


def test_programs(ctx):
    k, disk = session(ctx, "programs", net=False)
    try:
        k.run("exec disk/app.elf", "Hello from Ring 3!", "[exited with code 0]")
        k.run("exec disk/exit.elf 42", "[exited with code 42]")
        k.run("exec disk/crash.elf", "Page Fault", "[crashed: Page Fault]")
        k.run("uptime", "Uptime: ")                     # Keyboard still ours
        k.run("exec disk/loop.elf", "Looping forever")
        k.key("ctrl-c"); k.expect("^C [interrupted]")
        k.run("exec disk/ask.elf", "What's your name? ")
        k.line("Oskar"); k.expect("Hello, Oskar!")
        k.run("exec disk/iso.elf A &", "Started task")
        k.run("exec disk/iso.elf B", "[B] round 5", "[exited with code 0]", timeout=15)
        k.run("", "done] [exited with code 0]")
        k.run("exec disk/badptr.elf", *(["refused (-1)"] * 5))
        k.run("exec disk/heap.elf", "verified 64 KB", "sbrk said no", timeout=20)
        k.run("paging", "in use:              0 KB")
    finally:
        k.close()


def test_users(ctx):
    k, disk = session(ctx, "users", net=False)
    try:
        k.run("useradd " + USER, "Password for the new user: ")
        k.line(USER_PW); k.expect("Retype it: ")
        k.line(USER_PW); k.expect("Created " + USER)
        k.run("logout", "KaOS login: ")
        k.line(USER); k.expect("Password: ")
        k.line("wrong"); k.expect("Login incorrect.")
        k.expect("KaOS login: ")
        k.line(USER); k.expect("Password: ")
        k.line(USER_PW); k.expect(USER + "> ")
        k.run("whoami", USER + " (uid 1000)")
        k.run(f"echo mio > disk/home/{USER}/n.txt", "Written to file.")
        k.run("echo x > disk/hello.txt", "Permission denied")
        k.run("cat disk/etc/passwd", "Permission denied.")
        k.run(f"cat disk/home/{USER}/../../etc/passwd", "Permission denied.")
        k.run("exec disk/cat.elf disk/etc/passwd", "cat: cannot open", "[exited with code 1]")
        k.run("exec disk/cp.elf disk/hello.txt disk/tmp/c.txt", "Copied 60 bytes.")
        k.run("su", "Password: ")
        k.line(ROOT_PW); k.expect("root# ")
        k.run("cat disk/etc/passwd", "root:0:", USER + ":1000:")
    finally:
        k.close()
    if mtype(disk, f"/home/{USER}/n.txt").strip() != b"mio":
        raise Fail("user's file not on disk")


def test_network(ctx):
    web, slow = ctx.web_port, ctx.slow_port
    k, disk = session(ctx, "net")
    try:
        k.run("ifconfig", "config   DHCP")
        k.run("ping 10.0.2.2 2", "2 transmitted, 2 received", timeout=15)
        k.run(f"fetch 10.0.2.2:{web}/hola.txt", "Hola desde el host!", "200 OK, 20 bytes]")
        k.run(f"fetch 10.0.2.2:{web}/grande.txt > disk/g1.txt", "200 OK, 63600 bytes]", timeout=30)
        k.run(f"exec disk/wget.elf 10.0.2.2:{web}/grande.txt disk/tmp/a.txt &", "Started task")
        k.run(f"exec disk/wget.elf 10.0.2.2:{web}/grande.txt disk/tmp/b.txt", "wget: 63600 bytes", timeout=30)
        k.run("", "done] [exited with code 0]", timeout=30)
        k.run(f"fetch 10.0.2.2:{slow}/", "xxxx")
        k.key("ctrl-c"); k.expect("^C [interrupted]")
        for i in range(5):        # More than the 4 TCP slots: none leaked
            k.run(f"fetch 10.0.2.2:{web}/hola.txt", "200 OK, 20 bytes]")
        k.run("fetch 10.0.2.2:1/", "connection refused")
    finally:
        k.close()
    big = open(os.path.join(ctx.www, "grande.txt"), "rb").read()
    for p in ("/g1.txt", "/tmp/a.txt", "/tmp/b.txt"):
        if mtype(disk, p) != big:
            raise Fail(f"{p} differs from the served file")
    ok, report = fsck_ok(disk)
    if not ok:
        raise Fail("fsck.fat found problems:\n" + report)


def test_keymap_es(ctx):
    """Spanish layout: physical keys (as QEMU sends them) -> Latin-1 text."""
    k, disk = session(ctx, "keymap", net=False)

    def keys(*names):
        for n in names:
            k.key(n, 0.08)

    def word(w):            # Plain letters type the same in both layouts
        keys(*["spc" if c == " " else c for c in w])

    try:
        k.run("keymap es", "Layout es (saved).")
        # echo año más ¿qué? @ > disk/tmp/año.txt
        word("echo a"); keys("semicolon"); word("o m")              # ñ
        keys("apostrophe"); word("as ")                             # dead ´ + a = á
        keys("shift-equal"); word("qu"); keys("apostrophe"); word("e")  # ¿ ... é
        keys("shift-minus", "spc", "alt_r-2", "spc")                # ? and AltGr+2 = @
        keys("shift-less", "spc"); word("disk"); keys("shift-7")    # > and /
        word("tmp"); keys("shift-7"); word("a"); keys("semicolon"); word("o")
        keys("dot"); word("txt"); keys("ret")
        k.expect("Written to file.")
        # cat disk/tmp/AÑO.TXT  (accent-aware case-insensitive lookup)
        word("cat disk"); keys("shift-7"); word("tmp"); keys("shift-7")
        keys("shift-a", "shift-semicolon", "shift-o", "dot", "shift-t", "shift-x", "shift-t", "ret")
        k.expect("a\xf1o m\xe1s \xbfqu\xe9? @")
        k.run("keymap us", "Layout us (saved).")
    finally:
        k.close()
    if mtype(disk, "/tmp/a\u00f1o.txt").strip() != "año más ¿qué? @".encode("latin-1"):
        raise Fail("año.txt not found from the host under its Unicode long name")
    ok, report = fsck_ok(disk)
    if not ok:
        raise Fail("fsck.fat found problems:\n" + report)


def test_panic(ctx):
    """A kernel fault stops the machine with a readable report."""
    k, disk = session(ctx, "panic", net=False)
    try:
        k.run("panic", "KERNEL PANIC: unhandled exception in the kernel",
              "Page Fault", "Faulting address (CR2) 0xFFFFF000", "EIP=0x",
              "Running task: shell (pid 0)", "The system has been halted.")
        eip = int(k.output().split("EIP=0x")[1][:8], 16)
    finally:
        k.close()
    # The reported EIP must fall inside the function that faulted
    funcs = []
    for l in open(os.path.join(ROOT, "build", "kernel.map")):
        addr, _, name = l.split()
        funcs.append((int(addr, 16), name))
    owner = max((f for f in funcs if f[0] <= eip), default=(0, "?"))[1]
    if owner != "cmd_panic":
        raise Fail(f"EIP {eip:#x} maps to {owner}, expected cmd_panic")


def test_editor(ctx):
    k, disk = session(ctx, "editor", net=False)

    def keys(*names):
        for n in names:
            k.key(n, 0.06)

    try:
        k.run("edit disk/tmp/nota.txt", "[edit] opened")
        k.type("Primera linea\ntercera")
        keys("ctrl-s"); k.expect("[edit] Saved 21 bytes.")
        keys("up", "end", "ret"); k.type("segunda")             # Insert in the middle
        keys("ctrl-s"); k.expect("[edit] Saved 29 bytes.")
        keys("ctrl-q"); k.expect("[edit] closed")
        k.run("cat disk/tmp/nota.txt", "Primera linea\nsegunda\ntercera")

        # Shrink the file: the rest must really go (size from the host)
        k.run("edit disk/tmp/nota.txt", "[edit] opened")
        keys("down", "end", *(["backspace"] * 7), "delete", *(["delete"] * 7))  # Drop "segunda\ntercera"
        keys("ctrl-s"); k.expect("[edit] Saved 14 bytes.")
        k.type("x")
        keys("ctrl-q")                                          # Unsaved: first Ctrl+Q only warns
        time.sleep(0.5)
        if "[edit] closed" in k.output()[k.pos:]:
            raise Fail("Ctrl+Q closed despite unsaved changes")
        keys("ctrl-q"); k.expect("[edit] closed")

        # A normal user can open but not save outside home / tmp
        k.run("useradd " + USER, "Password for the new user: ")
        k.line(USER_PW); k.expect("Retype it: ")
        k.line(USER_PW); k.expect("Created " + USER)
        k.run("su " + USER)
        k.run("edit disk/hello.txt", "[edit] opened")
        k.type("x"); keys("ctrl-s"); k.expect("[edit] Permission denied: not saved.")
        keys("ctrl-q", "ctrl-q"); k.expect("[edit] closed")
    finally:
        k.close()
    if mtype(disk, "/tmp/nota.txt") != b"Primera linea\n":
        raise Fail("edited file has the wrong contents: %r" % mtype(disk, "/tmp/nota.txt"))
    if b"x" in mtype(disk, "/hello.txt")[:1]:
        raise Fail("user's change to disk/hello.txt was saved")


TESTS = [test_gui_boot, test_shell_and_files, test_programs, test_users, test_network,
         test_keymap_es, test_panic, test_editor]


class Ctx:
    def __init__(self):
        self.tmp = tempfile.mkdtemp(prefix="kaos-test-")
        self.www = os.path.join(self.tmp, "www")
        self.web_port, self.slow_port = start_servers(self.www)
        self.base = os.path.join(ROOT, "build", "fat32.img")

    def fresh_disk(self, name):
        """Copy of the built disk as a first boot would find it: no
        accounts and none of the directories KaOS creates (whatever your
        own sessions left in build/fat32.img doesn't leak into tests)."""
        d = os.path.join(self.tmp, f"disk-{name}.img")
        shutil.copy(self.base, d)
        subprocess.run(["mdeltree", "-i", d, "::/etc", "::/home", "::/tmp"],
                       capture_output=True)
        return d


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-k", help="run only tests whose name contains this")
    ap.add_argument("--keep", action="store_true", help="keep the temp dir (logs, disks)")
    args = ap.parse_args()

    for tool in ("qemu-system-i386", "mtype", "mdeltree", "fsck.fat"):
        if not shutil.which(tool):
            sys.exit(f"missing tool: {tool} (apt install qemu-system-x86 mtools dosfstools)")

    ctx = Ctx()
    failed = 0
    for t in TESTS:
        if args.k and args.k not in t.__name__:
            continue
        start = time.time()
        try:
            t(ctx)
            print(f"PASS  {t.__name__}  ({time.time() - start:.1f}s)")
        except Fail as e:
            failed += 1
            print(f"FAIL  {t.__name__}  ({time.time() - start:.1f}s)\n      {e}".replace("\n", "\n      "))
    if args.keep or failed:
        print(f"logs and disks: {ctx.tmp}")
    else:
        shutil.rmtree(ctx.tmp, ignore_errors=True)
    print("ALL PASSED" if not failed else f"{failed} FAILED")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
