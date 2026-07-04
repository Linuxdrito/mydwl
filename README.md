# mydwl

Un fork minimalista y despojado de **dwl**, pensado para correr exactamente una configuración de hardware y una filosofía de trabajo: cero descubrimiento en tiempo de ejecución, cero abstracciones que no se usan, cero X11.

---

## Diferencias respecto al dwl original

### Eliminado

- **LayerShell por completo.** No hay ni un solo #include, protocolo, LayerSurface, manager, listener, callback o lógica de composición relacionada con LayerShell. Se eliminó toda la infraestructura para superficies de capa (layer surfaces), exclusive zones y gestión específica de capas. El compositor está diseñado exclusivamente para clientes Wayland nativos (xdg_shell).
- **XWayland por completo.** No hay ni un solo `#include` relacionado a Xwayland, ni estructuras, ni listeners, ni la unión `surface.xwayland` que existe en dwl estándar. Solo se soportan clientes Wayland nativos (`xdg_shell`).
- **Soporte multi-monitor.** dwl original mantiene una lista dinámica de monitores (`wl_list mons`). Este fork usa una única estructura `Monitor` estática (`static Monitor monitor;` con macro `#define selmon (&monitor)`) y una bandera `mon_init` que descarta cualquier salida adicional que Wayland reporte (`if (mon_init) return;` en `createmon`).
- **Layout flotante y alternancia de layouts.** No existe `togglefloating`, ni un arreglo de `Layout` seleccionable, ni la tecla para ciclar disposiciones. Solo existe `tile()`, sin alternativa.
- **Mover/redimensionar ventanas con el mouse.** El enum de modos de cursor se reduce a `CurNormal` y `CurPressed`; no hay `CurMove` ni `CurResize`, y el arreglo `buttons[]` en `config.def.h` está vacío (solo contiene una entrada nula de relleno).
- **Ajuste de `mfact`/`nmaster` en caliente.** No hay funciones `setmfact` ni `incnmaster`, ni atajos para cambiar el factor del master o el número de clientes maestros durante la ejecución; ambos valores se fijan una sola vez en `createmon` (`mfact = 0.55f`, `nmaster = 1`).
- **`foreign-toplevel-management`, `output-management`, `idle-inhibit`, `virtual-keyboard`, `input-method`, `tearing-control` y `pointer-gestures`.** Ninguno de estos protocolos wlroots aparece en el código; se eliminaron por no ser necesarios para el flujo de trabajo del autor (sin barras que listen ventanas, sin herramientas tipo `wlr-randr`/`kanshi`, sin inhibidores de suspensión externos).
- **Descubrimiento automático de resolución/monitor.** En lugar de aceptar cualquier modo que el driver ofrezca, `createmon()` recorre los modos disponibles.

### Añadido

- **Modo nocturno (`Alt+N`).** Atajo dedicado que baja el brillo al mínimo (`brightnessctl set 1`) para uso nocturno.
- **Ocultamiento de cursor por inactividad.** Funcionalidad ausente en dwl base, implementada con un temporizador Wayland dedicado (`cursor_hide_timer`).
- **Comandos de volumen y brillo vía teclas multimedia.** Atajos listos para `XF86Audio*` y `XF86MonBrightness*` usando `wpctl` y `brightnessctl`, no presentes en la configuración por defecto de dwl.
- **Lista de autostart declarativa y con doble fork seguro.** `autostart()` recorre `autostart_cmds[][3]` y lanza cada proceso con `PR_SET_PDEATHSIG` para que muera si el compositor muere, evitando procesos huérfanos.
- **Compilación agresiva para tamaño y velocidad.** El `Makefile` añade `-Os -march=native -flto -fomit-frame-pointer -ffunction-sections -fdata-sections` y enlaza con `-s -Wl,--gc-sections`, muy por encima del nivel de optimización del `config.mk` estándar de dwl.
- **Validación estricta de la salida al arrancar.** Registra en el log (`WLR_INFO`) cada modo detectado por el monitor antes de fallar, facilitando el diagnóstico si el panel cambia.
- **Comentarios y nombres de variables en español** en la configuración (`config.def.h`), reflejando que el proyecto está pensado para uso personal y no para distribución genérica.

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

## Instalación

```sh
# 1. Clonar el repositorio
git clone <https://github.com/Linuxdrito/mydwl.git>
cd mydwl

# 2. Compilar
make

# 3. Instalar (requiere permisos para escribir en $PREFIX, por defecto /usr/local)
sudo make install

# 4. Ejecutar
dbus-run-session mydwl
```


## Configuración

Este proyecto **no tiene archivo de configuración externo**: todo se define en `config.h` (generado a partir de `config.def.h`), un archivo de código C que se compila directamente dentro del binario.

Flujo recomendado para aplicar cambios:

```sh
make clean
make
sudo make install
```

## Aplicaciones que inicia automáticamente

| Programa | Función |
|---|---|
| `pipewire` | Servidor de audio/video multimedia |
| `wireplumber` | Gestor de sesión de PipeWire |
| `pipewire-pulse` | Capa de compatibilidad PulseAudio sobre PipeWire |
| `foot --server` | Servidor del emulador de terminal `foot` |

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
| `Alt + 1 ... 5` | `view(tag N)` | Cambia a la etiqueta *N* (1 a 5) |
| `Alt + Ctrl + 1 ... 5` | `toggleview(tag N)` | Alterna la visibilidad de la etiqueta *N* junto a la actual |
| `Alt + Shift + 1 ... 5` | `tag(tag N)` | Envía la ventana enfocada a la etiqueta *N* |
| `Alt + Ctrl + Shift + 1 ... 5` | `toggletag(tag N)` | Alterna la pertenencia de la ventana enfocada a la etiqueta *N* |
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
