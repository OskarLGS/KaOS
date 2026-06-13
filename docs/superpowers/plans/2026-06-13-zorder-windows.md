# Z-order real para ventanas — Plan de Implementación

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Añadir un array `z_order[]` al window manager para que la ventana enfocada siempre se dibuje al frente, sin invalidar los punteros externos a `windows[]`.

**Architecture:** Se mantiene `windows[]` sin reordenar. Un array paralelo `z_order[MAX_WINDOWS]` almacena los índices de ventana de fondo (índice 0) a frente (índice `z_count-1`). Una función `wm_bring_to_front(idx)` desplaza el índice al final. Todos los caminos de enfoque (clic, taskbar, menú) llaman a esta función.

**Tech Stack:** C freestanding, NASM, QEMU i386. Build: `wsl bash build.sh`. Verificación headless via `-debugcon file:build/debug.log`.

---

## Archivos a modificar

| Archivo | Cambio |
|---------|--------|
| `kernel/window.c` | Todos los cambios — datos nuevos, 5 funciones modificadas/añadidas |

Ningún otro archivo necesita modificarse.

---

## Cómo verificar cada tarea

Este kernel no tiene framework de tests unitarios. La verificación es:

1. **Build limpio:** `wsl bash build.sh` — debe terminar sin errores y con tamaño < 128 KB.
2. **Trail headless gráfico:** boot headless y comprobar que `build/debug.log` contiene `GPQVTRKMS!DW`.
3. **Trail headless texto:** boot en modo texto y comprobar `GTRKS!`.

Comando de trail headless (desde Windows, QEMU en `C:\Program Files\qemu`):

```powershell
Start-Process -NoNewWindow "C:\Program Files\qemu\qemu-system-i386.exe" `
  -ArgumentList "-drive format=raw,file=kaos.img,if=floppy",`
                "-drive format=raw,file=build\fat32.img,if=ide",`
                "-boot order=a",`
                "-display none",`
                "-debugcon file:build/debug.log",`
                "-monitor telnet:127.0.0.1:4555,server,nowait"

Start-Sleep 2
$tcp = [System.Net.Sockets.TcpClient]::new("127.0.0.1", 4555)
$sw  = [System.IO.StreamWriter]::new($tcp.GetStream()); $sw.AutoFlush = $true
$sw.WriteLine("sendkey ret"); Start-Sleep 6
$sw.WriteLine("quit"); $tcp.Close()

$log = Get-Content build\debug.log -Raw
if ($log -match "GPQVTRKMS!DW") { Write-Host "PASS" } else { Write-Host "FAIL: $log" }
```

---

## Tarea 1: Datos estáticos + `wm_bring_to_front` + reset en `wm_init`

**Archivo:** `kernel/window.c`

Añadir variables, función central y reset. Al terminar, el código compila pero el z-order no se usa aún (checkpoint de build limpio).

- [ ] **Paso 1: Añadir variables estáticas**

  Tras la línea `static int drag_off_y = 0;` (alrededor de la línea 15), insertar:

  ```c
  static int z_order[MAX_WINDOWS];
  static int z_count = 0;
  ```

- [ ] **Paso 2: Añadir `wm_bring_to_front`**

  Insertar justo antes del comentario `// ─── Public API`:

  ```c
  static void wm_bring_to_front(int idx) {
      int pos = -1;
      for (int i = 0; i < z_count; i++)
          if (z_order[i] == idx) { pos = i; break; }
      if (pos < 0 || pos == z_count - 1) return;
      for (int i = pos; i < z_count - 1; i++)
          z_order[i] = z_order[i + 1];
      z_order[z_count - 1] = idx;
  }
  ```

- [ ] **Paso 3: Resetear `z_count` en `wm_init`**

  ```c
  void wm_init() {
      win_count   = 0;
      z_count     = 0;   // añadir
      focused_win = -1;
      drag_active = 0;
  }
  ```

- [ ] **Paso 4: Build de verificación**

  ```bash
  wsl bash build.sh
  ```

  Esperado: sin errores, tamaño < 128 KB. Puede haber warnings de "unused variable/function" — es normal en este paso.

---

## Tarea 2: `wm_create` y `wm_draw_all`

**Archivo:** `kernel/window.c`

Registrar cada nueva ventana en `z_order` y cambiar el bucle de dibujo para seguirlo.

- [ ] **Paso 1: Actualizar `wm_create`**

  Insertar `z_order[z_count++] = win_count;` inmediatamente antes de `win_count++;`:

  ```c
      focused_win = win_count;
      win->focused = 1;
      z_order[z_count++] = win_count;   // añadir
      win_count++;
      return win;
  ```

- [ ] **Paso 2: Actualizar el bucle de dibujo en `wm_draw_all`**

  ```c
  // Antes:
  for (int i = 0; i < win_count; i++) {
      draw_window(i);
  }

  // Después:
  for (int i = 0; i < z_count; i++) {
      draw_window(z_order[i]);
  }
  ```

- [ ] **Paso 3: Build + trail headless**

  ```bash
  wsl bash build.sh
  ```

  Esperado: build limpio, trail `GPQVTRKMS!DW`. El escritorio se ve igual — no hay reordenación aún porque `wm_bring_to_front` todavía no se llama.

---

## Tarea 3: Hit-test — bucle de detección de clic

**Archivo:** `kernel/window.c` — bloque dentro de `wm_on_mouse_click`

Sustituir el bloque `// Check windows in reverse order` completo (líneas ~231-266) por la versión que recorre `z_order` en reverso y llama `wm_bring_to_front` en todo hit.

- [ ] **Paso 1: Sustituir el bloque de hit-test**

  Localizar el comentario `// Check windows in reverse order (top-most first)` y reemplazar todo el bloque hasta el final de la función (incluyendo la línea de deselección) por:

  ```c
      // Check windows front-to-back via z_order
      for (int i = z_count - 1; i >= 0; i--) {
          int idx = z_order[i];
          window_t* w = &windows[idx];
          if (!w->visible) continue;

          if (x >= w->x && x < w->x + w->w &&
              y >= w->y && y < w->y + w->h) {

              // Focus
              if (focused_win != idx) {
                  if (focused_win >= 0) windows[focused_win].focused = 0;
                  focused_win = idx;
                  w->focused  = 1;
              }
              wm_bring_to_front(idx);

              // Close button?
              int btn_x = w->x + w->w - 18;
              int btn_y = w->y + 3;
              if (x >= btn_x && x < btn_x + 14 &&
                  y >= btn_y && y < btn_y + 14) {
                  minimize_window(idx);
                  return;
              }

              // Title bar drag?
              if (y >= w->y && y < w->y + TITLEBAR_H) {
                  drag_active = 1;
                  drag_win    = idx;
                  drag_off_x  = x - w->x;
                  drag_off_y  = y - w->y;
              }
              return;
          }
      }
      // Click on desktop — deselect
      if (focused_win >= 0) { windows[focused_win].focused = 0; focused_win = -1; }
  ```

- [ ] **Paso 2: Build + trail headless**

  ```bash
  wsl bash build.sh
  ```

  Esperado: build limpio, trail `GPQVTRKMS!DW`.

---

## Tarea 4: `activate_window`, `minimize_window` y verificación final

**Archivo:** `kernel/window.c`

Últimos dos cambios: traer al frente al restaurar, y elegir el nuevo foco por z_order al minimizar.

- [ ] **Paso 1: Actualizar `activate_window`**

  ```c
  static void activate_window(int i) {
      windows[i].visible = 1;
      if (focused_win >= 0) windows[focused_win].focused = 0;
      focused_win = i;
      windows[i].focused = 1;
      wm_bring_to_front(i);   // añadir
  }
  ```

- [ ] **Paso 2: Actualizar `minimize_window`**

  ```c
  static void minimize_window(int i) {
      windows[i].visible = 0;
      windows[i].focused = 0;
      focused_win = -1;
      for (int j = z_count - 1; j >= 0; j--) {
          int jdx = z_order[j];
          if (windows[jdx].visible) {
              focused_win = jdx;
              windows[jdx].focused = 1;
              break;
          }
      }
  }
  ```

- [ ] **Paso 3: Build final**

  ```bash
  wsl bash build.sh
  ```

  Esperado: build limpio, sin warnings nuevos, tamaño < 128 KB.

- [ ] **Paso 4: Trail headless gráfico**

  Ejecutar el comando de verificación headless de la sección superior.
  Esperado: `GPQVTRKMS!DW` en `build/debug.log`.

- [ ] **Paso 5: Trail headless texto**

  Mismo comando pero seleccionar opción 3 (añadir dos `sendkey down` antes del `sendkey ret`).
  Esperado: `GTRKS!` en `build/debug.log`.

- [ ] **Paso 6: Verificación manual en QEMU interactivo**

  Lanzar QEMU con display. Comprobar:

  1. Terminal y About se abren — About encima de Terminal.
  2. Clic en Terminal → Terminal sube al frente (barra azul).
  3. Clic en About → About sube al frente.
  4. Minimizar About (botón X) → Terminal queda al frente con foco.
  5. Restaurar About desde taskbar → About vuelve al frente.
  6. Clic sobre ventana ya enfocada → sin crash, sigue al frente.
  7. Arrastrar ventana por la barra de título → drag funciona igual que antes.

- [ ] **Paso 7: Commit**

  ```bash
  git add kernel/window.c
  git commit -m "feat(wm): z-order real — ventana enfocada siempre al frente"
  ```
