# 🗺️ KaOS: Roadmap

From bare metal to a full operating system.

> Estado a 2026-10: **Fases 1–4 completas**, Fase 5 casi completa.
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

## 🟣 Fase 5: GUI y Ventanas (casi completa)
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
- [ ] **Redimensionar ventanas**: Arrastrando el borde inferior-derecho.
- [ ] **App: Reloj** — uptime/hora con el RTC (ver Fase 7).
- [ ] **App: Visor de ficheros** — navegar el VFS con el ratón
      (estrena `readdir` gráfico y doble clic).
- [ ] **Scroll-back en la terminal** y historial de comandos (flechas ↑/↓,
      requiere emitir teclas extendidas desde el driver).

---

## 🔵 Fase 6: Procesos de Verdad (en progreso)
### Meta: De "cargar un ELF" a un modelo de procesos digno.
- [x] **Multitarea preemptiva**: el timer (IRQ0) conduce `schedule()`;
      una tarea que no cede CPU ya no congela el sistema (demo: `spin`).
- [x] **SYS_READ**: Programas de usuario interactivos (demo: `ask.elf`
      pide tu nombre). Bloqueante vía yield; modelo de foreground para
      la propiedad del teclado entre shell y programa. `print_string`
      atómico bajo preempción. Reaping de tareas paradas.
- [ ] **Más syscalls**: `open`/`close`/`read` sobre el VFS, `sbrk` para
      heap de usuario, `getpid`, `sleep`.
- [x] **Argumentos a programas**: `exec disk/args.elf hola mundo` →
      `main(argc, argv)` (crt0 en `user/crt0.asm`; el kernel prepara la pila).
- [x] **Códigos de salida**: `return` desde `main` / `sys_exit(n)`; la
      shell espera al programa en primer plano e informa
      (`[exited with code 42]`, `[crashed: Page Fault]`, `^C`).
      **Ctrl+C** interrumpe el programa en primer plano.
- [ ] **Varios programas de usuario a la vez**: Asignación dinámica de
      la región de usuario (hoy: un programa fijo en 0xA00000).
- [ ] **Espacios de direcciones por proceso**: Un page directory por
      tarea — el gran salto de aislamiento real.

---

## 🟤 Fase 7: Almacenamiento Completo (en progreso)
### Meta: Un disco que se siente como un disco.
- [x] **Crear ficheros**: `touch disk/nuevo.txt` y `echo texto > nuevo`
      (entrada de directorio 8.3 nueva). VFS con `create`/`unlink`.
- [x] **Borrar ficheros**: `rm` (marca 0xE5 + libera la cadena de clusters).
      Verificado: persiste entre reinicios y desde el host (`mdir`).
- [ ] **Crear directorios**: `mkdir` (entradas `.` y `..`).
- [x] **Liberar clusters al truncar**: reescribir un fichero más corto
      libera la cola de la cadena (verificado con `fsck.fat`).
- [ ] **Nombres largos (LFN)**: Leer (y luego escribir) entradas UCS-2.
- [ ] **Rutas multinivel**: `cat disk/docs/sub/leeme.txt` (resolver el
      path en el VFS en vez de un solo nivel en cada comando).
- [ ] **RTC (CMOS)**: Fecha/hora real para timestamps de ficheros y el
      reloj de la Fase 5.
- [ ] **cp / mv** entre RAMFS y disco.

---

## 🌑 Fase 8: Sistema Multi-Usuario y Red
### Meta: Lo que conocemos como un SO moderno.
- [ ] **Cuentas de Usuario**: Login, contraseñas y permisos en el VFS.
- [ ] **Driver de red**: RTL8139 o e1000 (ambos emulables en QEMU).
- [ ] **Pila de red por capas**: Ethernet → ARP → IPv4 → ICMP
      (¡comando `ping`!) → UDP → DHCP → TCP.
- [ ] **App de red**: Un cliente HTTP mínimo (`fetch example.com`).
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
