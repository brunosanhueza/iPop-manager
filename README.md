# iPop 📱

**iPop** es un gestor de iPods para Linux hecho en **C++17 y Qt 6**, inspirado en `gtkpod` pero sin GTK 2 ni autotools.
Permite ver la biblioteca de un iPod, **añadir canciones, eliminarlas, crear listas de reproducción y reproducir música** desde el PC.

> ⚠️ **Estado de las pruebas:** el soporte de **iPod Touch** (descrito abajo) está probado con un **iPod Touch 2G con iOS 4.2.1** en Void Linux.
> Otros modelos de Touch/iPhone o versiones de iOS **no están probados**. Los iPods clásicos (almacenamiento masivo) usan el código original del proyecto.

---

## ✨ Qué puede hacer

**iPod Touch (iOS 4):**
* Se detecta por USB y se **monta solo** con `ifuse` (no hace falta tocar nada en la terminal).
* **Añadir canciones** con el botón **➕ Añadir canciones** o arrastrando archivos a la ventana (mp3, m4a, aac; el resto se convierte a MP3 con FFmpeg).
* **Eliminar canciones** de la biblioteca (borra la canción y su archivo del iPod).
* **Crear, renombrar y eliminar listas de reproducción**, y añadir canciones nuevas directamente a la lista seleccionada.
* **Reproducir** las canciones del iPod desde el reproductor integrado.
* **Expulsar** de forma segura.

**Funciones generales:**
* Lista de playlists con resumen (*X canciones • tiempo • tamaño*), tabla de canciones con buscador en tiempo real.
* Identificación del modelo, barra de capacidad por tipo de contenido y estado de batería.

---

## 📋 Requisitos

* Linux con **Qt 6.2+** (Core, Gui, Widgets, Multimedia), **CMake 3.16+**, GCC 9+ o Clang 10+.
* Para iPod Touch: **libgpod**, **glib**, **libimobiledevice**, **usbmuxd**, **ifuse** y **FFmpeg** (conversión de audio).
* SQLite y zlib (para leer la base de datos del iPod).

### Instalar dependencias (automático)

```bash
bash scripts/instalar_dependencias.sh
```

Detecta Void, Debian/Ubuntu, Arch o Fedora e instala todo. En Void además activa el servicio `usbmuxd`.
*(Probado: Void Linux y la instalación de paquetes de compilación en Ubuntu 24.04. Los nombres de paquetes de Arch y Fedora son los habituales pero no los he probado.)*

### Instalar dependencias (manual)

| Distro | Comando |
|---|---|
| **Void** | `sudo xbps-install -S base-devel cmake pkg-config qt6-base-devel qt6-multimedia-devel libgpod-devel glib-devel sqlite-devel zlib-devel libimobiledevice usbmuxd ifuse ffmpeg` y luego `sudo ln -s /etc/sv/usbmuxd /var/service/` |
| **Debian / Ubuntu** | `sudo apt install build-essential cmake pkg-config qt6-base-dev qt6-multimedia-dev libgpod-dev libglib2.0-dev libsqlite3-dev zlib1g-dev libimobiledevice-utils usbmuxd ifuse ffmpeg` |
| **Arch** | `sudo pacman -S --needed base-devel cmake pkgconf qt6-base qt6-multimedia libgpod glib2 sqlite zlib libimobiledevice usbmuxd ifuse ffmpeg` |
| **Fedora** | `sudo dnf install gcc-c++ cmake pkgconf-pkg-config qt6-qtbase-devel qt6-qtmultimedia-devel libgpod-devel glib2-devel sqlite-devel zlib-devel libimobiledevice-utils usbmuxd ifuse ffmpeg` |

---

## 🚀 Compilar y ejecutar

```bash
git clone git@github.com:vicente-arriagada-lost/iPop-Manager.git
cd iPop-Manager
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
./build/ipop
```

La compilación genera, junto a `ipop`, la carpeta `build/tools/ipod/` con tres archivos que la app necesita para escribir en el iPod Touch:
`gestor`, `nochmod.so` y `openssl-legacy.cnf`. **Ejecuta siempre `ipop` desde `build/`** (o deja esa carpeta junto al ejecutable si lo instalas en otro lugar).
Si al compilar ves el aviso *"libgpod-1.0/glib-2.0 no encontrados"*, instala `libgpod-devel`/`libgpod-dev` y `glib-devel`/`libglib2.0-dev` y vuelve a compilar.

---

## 🔌 Primer uso con un iPod Touch

1. Comprueba que el servicio **usbmuxd** está corriendo (en Void: `sudo sv status usbmuxd` debe decir `run`).
2. **Desbloquea el iPod**, conéctalo por USB y, si aparece el aviso, toca **Confiar**.
3. Abre iPop (`./build/ipop`). Detecta el iPod y lo monta solo en `~/.local/share/iPop/iPop/mount`.
   Si no aparece, espera unos segundos y pulsa el botón de actualizar.
4. Pulsa **➕ Añadir canciones**, elige tus archivos y espera a que termine.
5. ⚠️ **Reinicia el iPod** (mantén *Sleep + Home* hasta ver el logo de Apple). El iPod solo importa la base de datos modificada tras reiniciar; si no, los cambios no se ven en la app Música.
6. Antes de desconectar, usa **⏏ Expulsar**.

### Respaldo recomendado
Antes de la primera modificación, copia la base de datos del iPod (con el iPod montado):
```bash
cp -r ~/.local/share/iPop/iPop/mount/iTunes_Control/iTunes ~/ipod-backup
```
Para restaurar: borra `iTunesCDB.*` temporales y copia el contenido del respaldo de vuelta a `iTunes_Control/iTunes`.

---

## 🧯 Solución de problemas

| Síntoma | Qué hacer |
|---|---|
| `Could not connect to lockdownd: SSL error (-5)` | El iOS 4 usa TLS antiguo que OpenSSL 3 rechaza. iPop ya lo resuelve internamente con `openssl-legacy.cnf`; si pruebas a mano: `OPENSSL_CONF=build/tools/ipod/openssl-legacy.cnf ideviceinfo -k ProductVersion` debe mostrar la versión de iOS. |
| iPod por USB pero "sin almacenamiento montado" | Desbloquéalo y toca **Confiar**. Prueba `idevicepair pair`. Revisa que `usbmuxd` esté activo. |
| `Input/output error` en la carpeta de montaje | Montaje roto: `fusermount -uz ~/.local/share/iPop/iPop/mount` y reconecta. |
| Las canciones no aparecen en Música | Reinicia el iPod (*Sleep + Home*). |
| "No se encuentran las herramientas del iPod Touch" | No se compiló `gestor`: instala `libgpod-devel` y `glib-devel` y recompila. |
| No se puede montar por permisos de FUSE | Tu usuario debe poder usar FUSE (grupo `fuse` en algunas distros). |

---

## ⚠️ Limitaciones conocidas (iPod Touch)

* **Eliminar es definitivo:** se borra el archivo de música del iPod, sin papelera. La app pide confirmación.
* Para eliminar canciones hay que estar en la **biblioteca completa**; no se puede quitar una canción de una lista sin borrarla.
* Las **carátulas** no se escriben al añadir canciones.
* La lista maestra se reconoce por el nombre `iPod de Zacky` o, si no existe, por ser la lista que contiene **todas** las canciones. Si tu iPod tiene un nombre distinto, esto es una heurística poco probada.
* Solo se cargan directamente **mp3, m4a y aac**.
* Crear/renombrar/eliminar listas y los nombres de lista duplicados (se rechazan) tienen pruebas limitadas.
* `run_ipop.py` es la versión antigua en Python y **no** incluye el soporte de iPod Touch.

---

## 📂 Estructura

```text
iPop-Manager/
├── CMakeLists.txt                  # Compila iPop y las herramientas de iPod Touch
├── scripts/instalar_dependencias.sh
├── tools/ipod/
│   ├── gestor.c                    # CLI sobre libgpod: añadir / eliminar / listas (escribe la base firmada)
│   ├── nochmod.c                   # LD_PRELOAD: ifuse no implementa chmod
│   ├── openssl-legacy.cnf          # TLS antiguo para iOS 4
│   └── ipod.sh                     # Envoltorio de shell para pruebas manuales
└── src/
    ├── main.cpp
    ├── core/
    │   ├── IpodTouchBridge.*       # Puente entre la app y `gestor` (iPod Touch)
    │   ├── DeviceDetector.*        # Detección por USB y montaje automático con ifuse
    │   ├── ITunesDBReader.*        # Lectura de la biblioteca (SQLite del Touch / iTunesDB clásico)
    │   ├── IPodDevice.*            # Modelo, almacenamiento, batería, borrado, expulsión
    │   ├── TrackImporter.*         # Importación de canciones
    │   └── AudioConverter.*        # Conversión con FFmpeg
    └── ui/                         # Ventana principal, tabla, reproductor, barra de capacidad
```

---

## 📜 Licencia

Proyecto bajo **GNU General Public License v2 (GPLv2) o posterior** (ver `LICENSE` / `COPYING`).
Usa **libgpod** (LGPL), **libimobiledevice** (LGPL) y **FFmpeg**. iPod, iTunes y Apple son marcas registradas de Apple Inc.; este software no está afiliado con Apple.
