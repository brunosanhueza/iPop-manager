# iPop 📱

**iPop** es un gestor moderno de dispositivos iPod para Linux desarrollado desde cero en **C++17 y Qt 6**, inspirado en la robustez y conceptos de `gtkpod`, pero completamente libre de dependencias legadas de GNOME 2 / GTK 2 y sistemas autotools obsoletos.

---

## ✨ Características

* **Explorador y Gestor de Playlists (Sidebar Izquierda):**
  * Lista todas las listas de reproducción creadas en el iPod:
    * 🎵 **Biblioteca Principal** (*Todas las canciones*)
    * 📁 **Playlists de usuario** (con conteo dinámico de canciones)
    * 🎙️ **Podcasts**
  * Resumen dinámico por playlist seleccionada (*X canciones • Y horas/minutos • Z MB/GB*).

* **Tabla de Canciones y Metadatos:**
  * Al seleccionar cualquier playlist, se despliega la lista completa de pistas con sus propiedades:
    * ☑️ **Casilla de selección individual** en cada fila.
    * **Título**, **Artista**, **Álbum**.
    * **Calidad / Bitrate** (ej. `320 kbps`, `256 kbps`).
    * **Duración** formateada (`mm:ss`).
    * **Formato** (`MP3`, `AAC`, `ALAC`, `WAV`, etc.).
    * **Tamaño** (`X.X MB`).
  * **Buscador en tiempo real:** Filtro instantáneo de pistas por nombre, artista o álbum.

* **Selección Múltiple y Eliminación:**
  * Botón **"Seleccionar todas"** / **"Deseleccionar todas"** con un solo clic.
  * Botón **"Eliminar seleccionadas (X)"** interactivo con diálogo de confirmación:
    * Si estás en una playlist personalizada, quita las canciones de esa lista.
    * Si estás en la Biblioteca Principal, elimina las pistas y borra los archivos físicos del iPod liberando espacio.

* **Identificación de Hardware y Batería:**
  * Detección automática en `/run/media/`, `/media/` o selector manual de carpeta.
  * Identificación de modelo exacto de iPod (Classic 1G-7G, Video, Mini 1G-2G, Photo, Nano 1G-7G, Shuffle 1G-4G) a partir del archivo `SysInfo`.
  * Monitoreo de batería y estado de carga USB desde `/sys/class/power_supply`.

* **Desglose Gráfico de Almacenamiento (Estilo iPod/iTunes):**
  * Barra de capacidad segmentada moderna (`CapacityBar`) con degradados suaves y tooltips interactivos:
    * 🎵 **Música / Audio**
    * 🎬 **Video**
    * 🎙️ **Podcasts**
    * 🖼️ **Fotos / Artwork**
    * ⚙️ **Otros / Sistema**
    * 💾 **Espacio Libre**

* **Expulsión Segura:**
  * Sincronización forzada de buffers de disco (`sync`) y desmontaje seguro del dispositivo.

---

## 🛠️ Requisitos de Compilación

En sistemas Linux (Ubuntu, Debian, Fedora, Arch Linux, etc.):

* **Compilador C++**: GCC 9+ o Clang 10+ (soporte C++17)
* **CMake**: versión 3.16 o superior
* **Qt 6**: `qt6-base-dev` (módulos Core, Gui, Widgets)

### Instalación de dependencias:

**Ubuntu / Debian:**
```bash
sudo apt update
sudo apt install build-essential cmake qt6-base-dev
```

**Arch Linux:**
```bash
sudo pacman -S base-devel cmake qt6-base
```

**Fedora:**
```bash
sudo dnf install gcc-c++ cmake qt6-qtbase-devel
```

---

## 🚀 Compilación y Ejecución

```bash
mkdir build
cd build
cmake ..
cmake --build . -j$(nproc)

# Ejecutar iPop
./ipop
```

---

## 📂 Estructura del Código

```text
iPop/
├── CMakeLists.txt              # Configuración moderna de CMake con Qt6
├── README.md                   # Este archivo
└── src/
    ├── main.cpp                # Punto de entrada y tema oscuro moderno (Fusion)
    ├── core/
    │   ├── Track.h             # Modelo de canción (título, artista, bitrate, duración, formato)
    │   ├── Playlist.h          # Modelo de playlist y cálculo de resúmenes
    │   ├── ITunesDBReader.h/.cpp # Lector binario de iTunesDB de Apple y fallback de archivos
    │   ├── IPodDevice.h/.cpp   # Lógica de hardware, SysInfo, batería, gestión de canciones y borrado
    │   └── DeviceDetector.h/.cpp # Detección de puntos de montaje en Linux
    └── ui/
        ├── MainWindow.h/.cpp   # Ventana principal (sidebar de playlists + tabla interactiva)
        ├── TrackTableModel.h/.cpp # Modelo de tabla con checkboxes y ordenamiento
        └── CapacityBar.h/.cpp  # Widget gráfico de la barra de almacenamiento segmentada
```

---

## 📜 Licencia

Proyecto bajo licencia GNU General Public License v2 (GPLv2) o posterior.
iPod, iTunes y Apple son marcas registradas de Apple Inc. Este software no está afiliado con Apple.
