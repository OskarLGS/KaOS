# Z-order real para ventanas — Spec de Diseño

**Fecha:** 2026-06-13  
**Estado:** Aprobado  
**Archivo objetivo:** `kernel/window.c`

---

## Problema

El orden de dibujo de las ventanas es fijo por orden de creación. Hacer clic en una ventana que está detrás de otra no la trae al frente: la ventana más nueva siempre se dibuja sobre la más antigua, sin importar cuál tiene el foco.

## Objetivo

Cuando el usuario enfoca una ventana (clic directo, restaurar desde la taskbar, o abrir desde el menú de inicio), esa ventana debe dibujarse por encima de todas las demás.

---

## Enfoque elegido: array de índices `z_order[]`

Se mantiene `windows[]` sin reordenar (los punteros externos a `main.c` y `terminal_window()` permanecen válidos). Se añade un array `z_order[MAX_WINDOWS]` que es una permutación de los índices activos de `windows[]`, de fondo (índice 0) a frente (índice `z_count - 1`).

### Invariantes

| Invariante | Garantía |
|-----------|----------|
| `windows[]` no se reordena | Los punteros guardados en `main.c` (`term`, `about`) y los devueltos por `wm_get_focused()` nunca se invalidan |
| `z_count == win_count` siempre | Se incrementan juntos en `wm_create`; se resetean juntos en `wm_init`. Si en el futuro se añade cierre real de ventanas habrá que decrementar ambos |
| `z_order` es siempre una permutación de `[0, win_count)` | `wm_bring_to_front` solo desplaza, no añade ni elimina; el guard `>= MAX_WINDOWS` en `wm_create` retorna antes de tocar cualquier contador |
| Complejidad O(MAX_WINDOWS) = O(8) en todo | Sin malloc, sin estructuras dinámicas |

---

## Datos nuevos en window.c

```c
static int z_order[MAX_WINDOWS];  // z_order[0] = fondo, z_order[z_count-1] = frente
static int z_count = 0;           // espejo de win_count, siempre iguales
```

---

## Función central: `wm_bring_to_front(int idx)`

`idx` es un índice en `windows[]` (no en `z_order`). Mueve `idx` al final de `z_order`, desplazando el resto:

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

---

## Cambios por función

### `wm_init`

```c
void wm_init() {
    win_count   = 0;
    z_count     = 0;   // añadido
    focused_win = -1;
    drag_active = 0;
}
```

### `wm_create`

Añadir el índice al final de `z_order` inmediatamente antes de incrementar `win_count`, para que ambos contadores avancen juntos:

```c
z_order[z_count++] = win_count;  // win_count es el índice de la nueva ventana
win_count++;
```

### `wm_draw_all`

```c
// Antes:
for (int i = 0; i < win_count; i++) draw_window(i);

// Después:
for (int i = 0; i < z_count; i++) draw_window(z_order[i]);
```

Los bucles de `wm_draw_desktop` (botones de taskbar) y `draw_start_menu` siguen usando `windows[i]` por orden de creación — deseable para orden estable de botones.

### `wm_on_mouse_click` — hit-test (cuerpo completo del bloque de ventanas)

El bucle recorre `z_order` de frente (último) a fondo (primero). Dentro del cuerpo, **todo uso del índice de ventana usa `idx = z_order[i]`**, nunca `i` directamente. `wm_bring_to_front(idx)` se llama en **todo hit**, incondicional, fuera del `if (focused_win != idx)`:

```c
// Reemplaza: for (int i = win_count - 1; i >= 0; i--) {
for (int i = z_count - 1; i >= 0; i--) {
    int idx = z_order[i];          // índice real en windows[]
    window_t* w = &windows[idx];
    if (!w->visible) continue;

    if (x >= w->x && x < w->x + w->w &&
        y >= w->y && y < w->y + w->h) {

        // Foco (solo cambia el flag si la ventana no era la enfocada)
        if (focused_win != idx) {
            if (focused_win >= 0) windows[focused_win].focused = 0;
            focused_win = idx;
            w->focused  = 1;
        }
        wm_bring_to_front(idx);   // siempre, incluso si el foco no cambió

        // Close button
        int btn_x = w->x + w->w - 18;
        int btn_y = w->y + 3;
        if (x >= btn_x && x < btn_x + 14 &&
            y >= btn_y && y < btn_y + 14) {
            minimize_window(idx);
            return;
        }

        // Title bar drag
        if (y >= w->y && y < w->y + TITLEBAR_H) {
            drag_active = 1;
            drag_win    = idx;     // idx, no i
            drag_off_x  = x - w->x;
            drag_off_y  = y - w->y;
        }
        return;
    }
}
// Clic en escritorio — deseleccionar (sin cambios)
if (focused_win >= 0) { windows[focused_win].focused = 0; focused_win = -1; }
```

### `activate_window` (taskbar y menú inicio)

`i` aquí es un índice en `windows[]` (índice de creación), igual que el argumento de `wm_bring_to_front`:

```c
static void activate_window(int i) {
    windows[i].visible = 1;
    if (focused_win >= 0) windows[focused_win].focused = 0;
    focused_win = i;
    windows[i].focused = 1;
    wm_bring_to_front(i);   // añadido
}
```

### `minimize_window` — cuerpo completo

El reset de `visible` y `focused` de la ventana minimizada se conserva. El bucle de re-foco recorre `z_order` en reverso para elegir la ventana más al frente visible:

```c
static void minimize_window(int i) {
    windows[i].visible = 0;
    windows[i].focused = 0;
    focused_win = -1;
    // Antes: for (int j = win_count - 1; j >= 0; j--)
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

---

## Qué NO cambia

- `window_t` struct: sin campos nuevos.
- `window.h`: sin cambios en la API pública.
- `wm_get_focused()`: devuelve `&windows[focused_win]` igual que antes.
- `wm_draw_desktop()` y `draw_start_menu()`: sin cambios (iteran `windows[i]` por orden de creación para botones estables en la taskbar).
- Ningún archivo fuera de `window.c` necesita modificarse.

---

## Verificación

1. `bash build.sh` — compila sin errores, kernel bajo límite.
2. Boot headless gráfico → trail `GPQVTRKMS!DW`.
3. Boot QEMU interactivo:
   - Abrir Terminal y About → About se crea encima de Terminal.
   - Hacer clic en Terminal → Terminal sube al frente.
   - Hacer clic en About → About sube al frente.
   - Minimizar About → Terminal sigue al frente (foco vía z_order).
   - Restaurar About desde taskbar → About vuelve al frente.
   - Hacer clic en la ventana ya enfocada → sigue al frente, sin crash.
   - Cerrar ventana vía botón X → minimize_window(idx) con idx correcto.
