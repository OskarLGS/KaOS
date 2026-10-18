# 🗺️ KaOS: Roadmap

From bare metal to a full operating system.

> Estado a 2026-10: **Fases 1–7 completas**; siguiente: Fase 8.
> Documentación por subsistema en [docs/](docs/README.md); guía de
> build/test en [AGENTS.md](AGENTS.md).

---

## 🟢 Fase 1: Los Cimientos ✅
- [x] KaBooM: Bootloader de 16-bit (Real Mode) con menú y chainloading
- [x] Salto a 32-bit (Protected Mode)
- [x] Entorno de Lenguaje C integrado
- [x] Escritura directa en Memoria de Video (VGA Text Mode)

---

## 🟡 Fase 2: Interactividad ✅
- [x] **Librería de Pantalla**: `print_string`, `clear_screen`, Scrolling.
- [x] **IDT** (Interrupt Descriptor Table): Excepciones y hardware.
- [x] **Controlador de Teclado**: Scancode → ASCII, con Shift y símbolos.
- [x] **Shell Interactiva**: 15 comandos registrables por módulos.

---

## 🟠 Fase 3: Gestión de Recursos ✅
### Meta: Un sistema que "sabe" qué tiene conectado.
- [x] **KMalloc**: Funciones para reservar y liberar memoria RAM.
- [x] **Temporizador (PIT)**: Ticks a 100 Hz, `uptime`.
- [x] **Paginación (Paging)**: Kernel supervisor-only + región de usuario.
- [x] **ATA Driver**: Lectura **y escritura** PIO con timeouts y flush.

---

## 🔴 Fase 4: Multitarea y Seguridad ✅
### Meta: Programas que no pueden romper el sistema.
- [x] **Multitasking**: Multitarea cooperativa (`yield`/`schedule`, `ps`, `kill`).
- [x] **User Mode (Ring 3)**: GDT con segmentos de usuario + TSS;
      un fallo en un programa mata solo a su tarea.
- [x] **System Calls**: `int 0x80` con `SYS_WRITE` y `SYS_EXIT` desde Ring 3.
- [x] **Virtual File System**: Nodos con read/write/readdir/finddir.
- [x] **RAMFS**: Sistema de archivos en memoria para pruebas.
- [x] **FAT32**: Lectura y escritura persistente de discos reales
      (`ls disk`, `cat`, `echo texto > disk/fichero.txt`, `exec disk/app.elf`).

---

## 🟣 Fase 5: GUI y Ventanas ✅
### Meta: Un escritorio usable con ventanas.
- [x] **Modo VESA (VBE)**: 800x600x32 con framebuffer lineal.
- [x] **Librería Gráfica**: Primitivas rápidas (`rep stosl/movsl`),
      fuente ASCII completa, doble búfer sin parpadeo.
- [x] **Driver de Ratón (PS/2)**: Resincronización de paquetes, sensibilidad.
- [x] **Window Server**: Foco, arrastre, minimizar/reabrir,
      barra de tareas con botones y menú de inicio.
- [x] **Terminal gráfica**: La shell completa dentro de una ventana.
- [x] **Z-order real**: Traer al frente la ventana enfocada al hacer clic,
      restaurar desde taskbar o abrir desde el menú inicio.
- [x] **Redimensionar ventanas**: bordes izquierdo/derecho/inferior y
      esquinas; la terminal recoloca su texto al soltar.
- [x] **App: Reloj** — ventana Clock (hora grande + fecha) y hora en la
      barra de tareas, leídas del RTC.
- [x] **App: Visor de ficheros** (Files) — navegar el VFS con ratón
      (doble clic) o teclado y leer ficheros de texto con scroll.
- [x] **Scroll-back en la terminal** (RePág/AvPág, 200 líneas) e
      historial de comandos (↑/↓) en ambos modos: el driver emite teclas
      extendidas (prefijo 0xE0) y la shell tiene un editor de línea único.

---

## 🔵 Fase 6: Procesos de Verdad ✅
### Meta: De "cargar un ELF" a un modelo de procesos digno.
- [x] **Multitarea preemptiva**: el timer (IRQ0) conduce `schedule()`;
      una tarea que no cede CPU ya no congela el sistema (demo: `spin`).
- [x] **SYS_READ**: Programas de usuario interactivos (demo: `ask.elf`
      pide tu nombre). Bloqueante vía yield; modelo de foreground para
      la propiedad del teclado entre shell y programa. `print_string`
      atómico bajo preempción. Reaping de tareas paradas.
- [x] **Más syscalls**: `open`/`close`/`read`/`write` sobre el VFS con
      tabla de descriptores por tarea, `sbrk` (heap de usuario), `getpid`
      y `sleep` (estado dormido real en el planificador). Demos: `cat.elf`,
      `cp.elf`, `sleep.elf`, `heap.elf`.
- [x] **Argumentos a programas**: `exec disk/args.elf hola mundo` →
      `main(argc, argv)` (crt0 en `user/crt0.asm`; el kernel prepara la pila).
- [x] **Códigos de salida**: `return` desde `main` / `sys_exit(n)`; la
      shell espera al programa en primer plano e informa
      (`[exited with code 42]`, `[crashed: Page Fault]`, `^C`).
      **Ctrl+C** interrumpe el programa en primer plano.
- [x] **Espacios de direcciones por proceso**: un page directory por
      programa (CR3 en cada cambio de tarea), pool de marcos de 4 KB,
      punteros de las syscalls validados contra las tablas de páginas.
- [x] **Varios programas de usuario a la vez**: todos enlazados en
      0x40000000 con memoria propia; `exec prog &` en segundo plano,
      procesos zombi hasta que la shell recoge su estado.

---

## 🟤 Fase 7: Almacenamiento Completo ✅
### Meta: Un disco que se siente como un disco.
- [x] **Crear ficheros**: `touch disk/nuevo.txt` y `echo texto > nuevo`
      (entrada de directorio 8.3 nueva). VFS con `create`/`unlink`.
- [x] **Borrar ficheros**: `rm` (marca 0xE5 + libera la cadena de clusters).
      Verificado: persiste entre reinicios y desde el host (`mdir`).
- [x] **Crear directorios**: `mkdir` (entradas `.` y `..`) y `rm` de
      directorios vacíos. Nombres que no caben en 8.3 se rechazan.
- [x] **Liberar clusters al truncar**: reescribir un fichero más corto
      libera la cola de la cadena (verificado con `fsck.fat`).
- [x] **Nombres largos (LFN)**: lectura y escritura de entradas UCS-2
      (checksum, alias `NOMBRE~N.EXT`), búsqueda por nombre largo o alias,
      hasta 127 caracteres; verificado con mtools y `fsck.fat`.
- [x] **Rutas multinivel**: `vfs_resolve` recorre cualquier profundidad
      (`cat disk/docs/sub/leeme.txt`); lo usan la shell y `SYS_OPEN`.
- [x] **RTC (CMOS)**: Fecha/hora real (`date`), timestamps de creación y
      modificación en FAT32 y reloj del escritorio.
- [x] **cp / mv** integrados: `mv` renombra/mueve sin copiar datos en
      FAT32 (actualiza `..` de directorios movidos) y copia+borra entre
      sistemas de ficheros; destino directorio = mismo nombre dentro.
- [x] **Bloqueo del sistema de ficheros**: un lock en el VFS serializa el
      acceso al driver ATA/FAT32 bajo preempción.

---

## 🌑 Fase 8: Sistema Multi-Usuario y Red (en progreso)
### Meta: Lo que conocemos como un SO moderno.
- [ ] **Cuentas de Usuario**: Login, contraseñas y permisos en el VFS.
- [x] **Driver de red**: RTL8139 (PCI, DMA, sondeado por la tarea `net`).
- [x] **Pila de red por capas**: Ethernet → ARP → IPv4 → ICMP
      (`ping`) → UDP → DHCP (al arrancar) → DNS (`host`) → TCP (cliente).
- [x] **App de red**: `fetch host[:port]/ruta [> fichero]` (HTTP/1.0);
      verificado con una descarga de 63 KB idéntica byte a byte.
- [ ] **Self-hosting**: Compilarse a sí mismo (necesita un compilador C
      portado... el jefe final).

---

## 🔧 Transversal (cuando toque)
- [ ] Layout de teclado ES (hoy: US; la ñ espera su momento).
- [ ] `panic()` con volcado de registros legible en vez de colgarse.
- [ ] Liberar la memoria de los `fs_node_t` creados por `finddir`
      (hoy se filtran — no hay `close` real).
- [ ] Soporte de teclas extendidas (E0: flechas, supr, inicio/fin).
- [ ] Build reproducible con tests automáticos (el patrón headless de
      AGENTS.md convertido en script `test.sh`).

---

> [!TIP]
> Cada fase es un mundo. No hay prisa. Lo importante es entender cada
> capa antes de avanzar. El orden de las Fases 6 y 7 es intercambiable:
> elige según te apetezca más procesos o disco.
