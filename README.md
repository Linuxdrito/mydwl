# mydwl

Un fork minimalista y despojado de **dwl**, pensado para correr exactamente una configuración de hardware y una filosofía de trabajo: cero descubrimiento en tiempo de ejecución, cero abstracciones que no se usan, cero X11.

---


## ¿Qué es?

`mydwl` es un compositor Wayland derivado de **dwl** (basado en la API de **wlroots 0.19**), reescrito y podado para eliminar todo lo que no forma parte del flujo de trabajo diario de su autor. No es un compositor "para todos": es un compositor para **una** máquina, **un** monitor y **un** conjunto de aplicaciones específico, hardcodeado directamente en el binario.

El objetivo del fork no es agregar funciones nuevas de gestión de ventanas, sino **restar**: quitar XWayland, quitar el soporte multi-monitor, quitar el layout flotante, quitar cualquier lógica de "detectar y adaptarse" en favor de valores fijos y explícitos en el código fuente.

- **Cero bloat.** Si una característica de dwl no se usa en el día a día, se elimina del binario, no se oculta detrás de un `#ifdef`.
- **Configuración estática, no dinámica.** No hay archivos `.conf`, ni IPC, ni protocolo de reconfiguración en caliente. Todo vive en `config.h`, se decide en tiempo de compilación y no cambia hasta el próximo `make`.
- **Hardcodear en vez de descubrir.** Cuando existe una única forma "correcta" de hacer algo en esta máquina (resolución de pantalla, comandos de volumen, navegador), el código la asume directamente en lugar de intentar detectarla o soportar alternativas.
- **Un binario pequeño y predecible.** Se prioriza tamaño y arranque en frío sobre flexibilidad: flags de compilación agresivos, sin capas de compatibilidad, sin rutas de código muertas.
- **Evita deliberadamente:** soporte para aplicaciones X11, múltiples monitores, ventanas flotantes, múltiples layouts, gestión de ventanas con el mouse (mover/redimensionar), barras de estado externas vía *foreign-toplevel*, gestión remota de salidas, e inhibidores de suspensión.

## Características

- **Tiling automático de una sola disposición.** Todas las ventanas se organizan en un layout maestro/pila (`tile()`); no existe alternancia de layouts ni modo flotante.
- **Sistema de tags (5 etiquetas).** Cada ventana pertenece a un subconjunto de hasta `TAGCOUNT = 5` etiquetas, con soporte para ver, alternar visibilidad, mover ventanas entre tags y alternar tags de una ventana.
- **Pantalla completa nativa.** Cualquier cliente puede solicitar fullscreen vía el protocolo XDG; se respeta con un fondo dedicado (`fullscreen_bg`) y sin bordes.
- **Capa (`layer-shell`).** Soporte completo para `wlr-layer-shell-unstable-v1`, necesario para barras de estado, fondos de pantalla y overlays tipo *wofi/rofi*, organizados en las capas `Bg`, `Bottom`, `Tile`, `Top`, `Overlay` y `FS`.
- **Decoración del lado del servidor.** Los bordes de las ventanas los dibuja el propio compositor (`wlr_server_decoration` y `xdg-decoration`, forzados a modo `SERVER_SIDE`); las ventanas no dibujan sus propias barras de título.
- **Gestión de foco por teclado y sloppy focus opcional.** El foco sigue por defecto los clics/teclado (`sloppyfocus = 0`); puede activarse foco-sigue-al-mouse cambiando una sola constante.
- **Ventanas urgentes.** Los clientes que se activan (`xdg-activation`) mientras no tienen foco se marcan como urgentes y cambian el color de su borde.
- **Portapapeles y arrastre.** Soporte de `wl_data_device`, `data-control` (portapapeles programático, usado por herramientas como `wl-clipboard`) y arrastrar-y-soltar (drag & drop) con ícono de arrastre en pantalla.
- **Captura de pantalla nativa.** Implementa `wlr_screencopy_manager_v1` y `export-dmabuf`, protocolos que utilizan herramientas como `grim`.
- **Ocultamiento automático del cursor.** Un temporizador interno (`hidecursor`) esconde el puntero tras un segundo de inactividad y lo restaura en el primer movimiento.
- **Grupo de teclados unificado.** Todos los teclados conectados se agrupan (`wlr_keyboard_group`) y comparten el mismo keymap, con repetición de tecla configurable (`repeat_rate`, `repeat_delay`).
- **Configuración exhaustiva de libinput.** Tap-to-click, tap-and-drag, drag-lock, scroll natural, disable-while-typing, botón izquierdo, emulación de botón medio, método de scroll, método de clic, perfil y velocidad de aceleración: todo configurado por dispositivo apuntador al conectarse.
- **Autostart de procesos en segundo plano.** Al iniciar, el compositor lanza automáticamente una lista fija de procesos (ver la tabla en la sección correspondiente).
- **Reinicio de GPU en caliente.** Ante la pérdida del contexto de renderizado (`gpureset`), el compositor recrea `renderer` y `allocator` sin necesidad de reiniciar la sesión completa.
- **Resolución de salida fijada por código.** El compositor busca explícitamente un modo de **1920×1080 a ~48 Hz** entre los modos que reporta la salida; si no lo encuentra, aborta con un mensaje de error (ver sección de diferencias).

## Diferencias respecto al dwl original

### Eliminado

- **XWayland por completo.** No hay ni un solo `#include` relacionado a Xwayland, ni estructuras, ni listeners, ni la unión `surface.xwayland` que existe en dwl estándar. Solo se soportan clientes Wayland nativos (`xdg_shell`).
- **Soporte multi-monitor.** dwl original mantiene una lista dinámica de monitores (`wl_list mons`). Este fork usa una única estructura `Monitor` estática (`static Monitor monitor;` con macro `#define selmon (&monitor)`) y una bandera `mon_init` que descarta cualquier salida adicional que Wayland reporte (`if (mon_init) return;` en `createmon`).
- **Layout flotante y alternancia de layouts.** No existe `togglefloating`, ni un arreglo de `Layout` seleccionable, ni la tecla para ciclar disposiciones. Solo existe `tile()`, sin alternativa.
- **Mover/redimensionar ventanas con el mouse.** El enum de modos de cursor se reduce a `CurNormal` y `CurPressed`; no hay `CurMove` ni `CurResize`, y el arreglo `buttons[]` en `config.def.h` está vacío (solo contiene una entrada nula de relleno).
- **Ajuste de `mfact`/`nmaster` en caliente.** No hay funciones `setmfact` ni `incnmaster`, ni atajos para cambiar el factor del master o el número de clientes maestros durante la ejecución; ambos valores se fijan una sola vez en `createmon` (`mfact = 0.55f`, `nmaster = 1`).
- **`foreign-toplevel-management`, `output-management`, `idle-inhibit`, `virtual-keyboard`, `input-method`, `tearing-control` y `pointer-gestures`.** Ninguno de estos protocolos wlroots aparece en el código; se eliminaron por no ser necesarios para el flujo de trabajo del autor (sin barras que listen ventanas, sin herramientas tipo `wlr-randr`/`kanshi`, sin inhibidores de suspensión externos).
- **Descubrimiento automático de resolución/monitor.** En lugar de aceptar cualquier modo que el driver ofrezca, `createmon()` recorre los modos disponibles y **solo acepta uno cercano a 1920×1080 @ 48 Hz**; cualquier otra combinación provoca un `abort()` con un log de error.

### Añadido

- **Modo nocturno (`Alt+N`).** Atajo dedicado que baja el brillo al mínimo (`brightnessctl set 1`) para uso nocturno.
- **Ocultamiento de cursor por inactividad.** Funcionalidad ausente en dwl base, implementada con un temporizador Wayland dedicado (`cursor_hide_timer`).
- **Comandos de volumen y brillo vía teclas multimedia.** Atajos listos para `XF86Audio*` y `XF86MonBrightness*` usando `wpctl` y `brightnessctl`, no presentes en la configuración por defecto de dwl.
- **Lista de autostart declarativa y con doble fork seguro.** `autostart()` recorre `autostart_cmds[][3]` y lanza cada proceso con `PR_SET_PDEATHSIG` para que muera si el compositor muere, evitando procesos huérfanos.
- **Compilación agresiva para tamaño y velocidad.** El `Makefile` añade `-Os -march=native -flto -fomit-frame-pointer -ffunction-sections -fdata-sections` y enlaza con `-s -Wl,--gc-sections`, muy por encima del nivel de optimización del `config.mk` estándar de dwl.
- **Validación estricta de la salida al arrancar.** Registra en el log (`WLR_INFO`) cada modo detectado por el monitor antes de fallar, facilitando el diagnóstico si el panel cambia.
- **Comentarios y nombres de variables en español** en la configuración (`config.def.h`), reflejando que el proyecto está pensado para uso personal y no para distribución genérica.
- **Comando de navegador hardcodeado con flags específicos.** `browsercmd` apunta a una ruta absoluta de un AppImage de Thorium con flags de Ozone/Wayland, VA-API y aislamiento de procesos ya fijados, sin necesidad de variables de entorno externas.

## Características técnicas

- **Arquitectura de un solo archivo de lógica principal** (`mydwl.c`, ~1480 líneas) más `util.c/util.h` para utilidades genéricas (referenciadas por el `Makefile`, no incluidas en este análisis) y `client.h`, que aporta los *wrappers* de compatibilidad de superficie de cliente típicos de dwl.
- **Basado en `wlroots 0.19`** vía `pkg-config`, usando la API de *scene graph* (`wlr_scene`) para todo el árbol de renderizado.
- **Organización por capas de escena (`NUM_LAYERS`):** `LyrBg`, `LyrBottom`, `LyrTile`, `LyrTop`, `LyrFS`, `LyrOverlay`, `LyrBlock`, cada una un `wlr_scene_tree` independiente, más un árbol aparte para el ícono de arrastre.
- **Manejo de ventanas:** cada `Client` es un `xdg_toplevel` con listeners de `commit`, `map`, `unmap`, `destroy`, `fullscreen`, `maximize` y decoración; las ventanas nuevas heredan tags de su padre si son transitorias, o del *tagset* activo del monitor.
- **Manejo de entrada:** un único `wlr_keyboard_group` centraliza todos los teclados; el repeat se maneja con un `wl_event_source` propio (`keyrepeat`). Los punteros se configuran individualmente vía `libinput` en `createpointer()` según las constantes de `config.h`.
- **Layout:** maestro/pila clásico de dwm/dwl (`tile()`), calculado sobre el área utilizable del monitor (`m->w`), que a su vez se recalcula cada vez que cambian las capas de `layer-shell` (`arrangelayers`).
- **Sistema de tags:** máscara de bits de `TAGCOUNT` bits (`TAGMASK`), con `view`, `toggleview`, `tag` y `toggletag`; el monitor guarda dos *tagsets* (`tagset[2]`) para poder alternar entre la vista actual y la anterior (`seltags ^= 1`).
- **Fullscreen:** guarda la geometría previa (`c->prev`) antes de expandir la ventana a `monitor.m`, y la restaura al salir; desactiva el borde (`bw = 0`) mientras está activo.
- **Focus:** pila de foco (`fstack`) independiente de la lista de clientes (`clients`), de modo que `focustop()` siempre resuelve la última ventana enfocada visible en el tagset actual.
- **Cursor:** un solo `wlr_cursor` atado al único `output_layout`; oculto automáticamente tras `CURSOR_HIDE_TIMEOUT` (1000 ms) de inactividad.
- **Autostart:** procesos lanzados por `fork()` + `execvp()` con `setsid()` y `PR_SET_PDEATHSIG(SIGTERM)`, garantizando que no sobrevivan a la muerte del compositor.
- **Selección de modo de salida:** iteración manual de `wlr_output_modes` buscando 1920×1080 con refresco dentro de ±2 Hz de 48 Hz; escala fijada en `1.0` y transformación `WL_OUTPUT_TRANSFORM_NORMAL`, sin lectura de EDID adicional ni lógica de HiDPI.
- **Renderizado:** `wlr_renderer_autocreate` + `wlr_allocator_autocreate`, con soporte DMA-BUF, `linux-dmabuf-v1` y sincronización `linux-drm-syncobj-v1` cuando el backend lo permite; recreación automática ante pérdida de contexto (`gpureset`).

## Dependencias

### Para compilar

| Dependencia | Uso |
|---|---|
| `wlroots-0.19` (+ headers) | API central del compositor |
| `wayland-server`, `wayland-protocols`, `wayland-scanner` | Generación de headers de protocolo (`xdg-shell`, `layer-shell`, `cursor-shape`, `pointer-constraints`) |
| `xkbcommon` | Manejo de teclado y keymaps |
| `libinput` | Configuración de dispositivos apuntadores |
| `pkg-config` | Resolución de flags de compilación/enlazado |
| Un compilador C compatible con C11/GNU (`cc`) | Compilación del binario |
| `make` | Orquestación del build (`Makefile` en formato POSIX) |

### Para ejecutar

| Dependencia | Uso |
|---|---|
| `wlroots-0.19` (runtime) | Backend de renderizado y sesión |
| `libinput`, `xkbcommon` (runtime) | Entrada de teclado/mouse en ejecución |
| Un gestor de sesión/seat (`seatd` o `logind`) | Acceso a DRM/input sin root |

### Opcionales

| Dependencia | Uso |
|---|---|
| Tema de cursores Xcursor instalado | El compositor usa `wlr_xcursor_manager_create(NULL, 24)`, que recurre al tema por defecto del sistema |
| Herramientas de captura compatibles con `wlr-screencopy`/`export-dmabuf` (p. ej. `grim`) | Consumidoras de los protocolos de captura ya expuestos por el compositor |
| Cliente de portapapeles compatible con `wl-data-control` (p. ej. `wl-clipboard`) | Consumidor del protocolo de portapapeles ya expuesto |

## Instalación

```sh
# 1. Clonar el repositorio
git clone <url-del-repositorio> mydwl
cd mydwl

# 2. Generar config.h a partir de config.def.h (solo la primera vez)
cp config.def.h config.h
# (el propio Makefile lo hace automáticamente si config.h no existe)

# 3. Compilar
make

# 4. Instalar (requiere permisos para escribir en $PREFIX, por defecto /usr/local)
sudo make install   # o `doas make install` si se usa doas en vez de sudo

# 5. Ejecutar
mydwl
# también disponible como entrada de sesión Wayland: "mydwl" en el gestor de acceso
```

> **Nota sobre `make uninstall`:** el objetivo `uninstall` del `Makefile` tal como está escrito no incluye el comando de borrado antes de la ruta del archivo `.desktop`; conviene revisarlo/completarlo manualmente antes de confiar en él.

## Configuración

Este proyecto **no tiene archivo de configuración externo**: todo se define en `config.h` (generado a partir de `config.def.h`), un archivo de código C que se compila directamente dentro del binario.

> ⚠️ **Importante:** si vas a modificar aplicaciones por defecto, atajos de teclado, comandos, procesos de autostart, distribución del teclado, comportamiento del monitor o cualquier otro aspecto de configuración, **debes hacerlo editando `config.def.h` (o `config.h`) antes de compilar**. Una vez compilado el binario, esos valores quedan fijos hasta que se vuelva a ejecutar `make` (y `make install`).

Flujo recomendado para aplicar cambios:

```sh
$EDITOR config.def.h   # o config.h si ya existe
make clean
make
sudo make install
```

## Aplicaciones que inicia automáticamente

| Programa | Función | Comentarios |
|---|---|---|
| `pipewire` | Servidor de audio/video multimedia | Sin argumentos |
| `wireplumber` | Gestor de sesión de PipeWire | Sin argumentos |
| `pipewire-pulse` | Capa de compatibilidad PulseAudio sobre PipeWire | Sin argumentos |
| `foot --server` | Servidor del emulador de terminal `foot` | Permite que `footclient` (usado en `Alt+Enter`) abra ventanas de terminal instantáneamente |

Todos se lanzan al arrancar el compositor (`autostart()`), antes de entrar al bucle principal de eventos, y se les asigna `PR_SET_PDEATHSIG(SIGTERM)` para que terminen junto con el compositor.

## Atajos de teclado

`MODKEY` está definido como **Alt** (`WLR_MODIFIER_ALT`).

| Combinación | Acción | Descripción |
|---|---|---|
| `Alt + Enter` | `spawn(termcmd)` | Abre una nueva terminal (`footclient`) |
| `Alt + T` | `spawn(browsercmd)` | Abre el navegador (Thorium AppImage con flags de Wayland/VA-API) |
| `Alt + N` | `spawn(moodnight)` | Activa "modo nocturno": brillo al mínimo |
| `Alt + Q` | `killclient` | Cierra la ventana enfocada |
| `Alt + F` | `togglefullscreen` | Alterna pantalla completa en la ventana enfocada |
| `Alt + ↓` | `focusstack(+1)` | Mueve el foco a la siguiente ventana en la pila |
| `Alt + ↑` | `focusstack(-1)` | Mueve el foco a la ventana anterior en la pila |
| `Alt + ←` | `focusstack(-1)` | Mueve el foco a la ventana anterior en la pila |
| `Alt + →` | `focusstack(+1)` | Mueve el foco a la siguiente ventana en la pila |
| `Alt + 0` | `view(~0)` | Muestra todas las etiquetas a la vez |
| `Alt + Shift + )` | `tag(~0)` | Envía la ventana enfocada a todas las etiquetas |
| `Alt + Shift + Q` | `quit` | Cierra el compositor |
| `Ctrl + Alt + Terminate_Server` | `quit` | Atajo de emergencia estándar de wlroots para terminar la sesión |
| `Alt + 1` … `Alt + 5` | `view(tag N)` | Cambia a la etiqueta *N* (1 a 5) |
| `Alt + Ctrl + 1` … `Alt + Ctrl + 5` | `toggleview(tag N)` | Alterna la visibilidad de la etiqueta *N* junto a la actual |
| `Alt + Shift + !/@/#/$/%` | `tag(tag N)` | Envía la ventana enfocada a la etiqueta *N* |
| `Alt + Ctrl + Shift + !/@/#/$/%` | `toggletag(tag N)` | Alterna la pertenencia de la ventana enfocada a la etiqueta *N* |
| `XF86AudioRaiseVolume` | `spawn(volup)` | Sube el volumen 5% (`wpctl`) |
| `XF86AudioLowerVolume` | `spawn(voldown)` | Baja el volumen 5% (`wpctl`) |
| `XF86AudioMute` | `spawn(volmute)` | Alterna silencio (`wpctl`) |
| `XF86MonBrightnessUp` | `spawn(brightup)` | Sube el brillo 5% (`brightnessctl`) |
| `XF86MonBrightnessDown` | `spawn(brightdn)` | Baja el brillo 5% (`brightnessctl`) |
| `Print` | `spawn(screenshotcmd)` | Ejecuta el script externo de captura de pantalla |

No existen atajos de mouse: el arreglo `buttons[]` está vacío por diseño (solo el clic en sí mismo enfoca la ventana bajo el cursor).

## Organización del código

| Archivo | Propósito |
|---|---|
| `mydwl.c` | Lógica completa del compositor: inicialización de wlroots, manejo de clientes XDG y layer-shell, entrada, foco, tags, layout y bucle principal |
| `config.def.h` | Configuración por defecto, en español, con todos los valores editables antes de compilar (colores, atajos, autostart, libinput, etc.) |
| `config.h` | Copia de `config.def.h` generada automáticamente por el `Makefile`; es el archivo realmente incluido en la compilación y el que se debe editar para personalizar el WM (no se versiona) |
| `Makefile` | Reglas de compilación, generación de headers de protocolo Wayland, instalación/desinstalación y empaquetado (`dist`) |
| `client.h` (referenciado) | Capa de compatibilidad para operar sobre superficies XDG (helpers de geometría, foco, tamaño, etc.), heredada de dwl |
| `util.c` / `util.h` (referenciados) | Utilidades genéricas (por ejemplo, `ecalloc`, `die`) usadas en todo `mydwl.c` |
| `protocols/` (referenciado) | XML del protocolo `wlr-layer-shell-unstable-v1`, usado por `wayland-scanner` para generar su header en tiempo de build |
