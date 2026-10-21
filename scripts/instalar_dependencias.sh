#!/usr/bin/env bash
# Instala las dependencias de iPop (compilación + iPod Touch) según tu distribución.
# Uso:  bash scripts/instalar_dependencias.sh
set -e

if command -v xbps-install >/dev/null 2>&1; then
    echo ">> Void Linux detectado"
    sudo xbps-install -Sy base-devel cmake pkg-config qt6-base-devel qt6-multimedia-devel \
        libgpod-devel glib-devel sqlite-devel zlib-devel \
        libimobiledevice usbmuxd ifuse ffmpeg python3 python3-pip
    # usbmuxd es un servicio runit: hay que activarlo
    if [ ! -e /var/service/usbmuxd ]; then
        sudo ln -s /etc/sv/usbmuxd /var/service/
        echo ">> Servicio usbmuxd activado"
    fi
elif command -v apt-get >/dev/null 2>&1; then
    echo ">> Debian/Ubuntu detectado"
    sudo apt-get update
    sudo apt-get install -y build-essential cmake pkg-config qt6-base-dev qt6-multimedia-dev \
        libgpod-dev libglib2.0-dev libsqlite3-dev zlib1g-dev \
        libimobiledevice-utils usbmuxd ifuse ffmpeg python3 python3-pip
elif command -v pacman >/dev/null 2>&1; then
    echo ">> Arch Linux detectado"
    sudo pacman -S --needed base-devel cmake pkgconf qt6-base qt6-multimedia \
        libgpod glib2 sqlite zlib libimobiledevice usbmuxd ifuse ffmpeg python python-pip
elif command -v dnf >/dev/null 2>&1; then
    echo ">> Fedora detectado"
    sudo dnf install -y gcc-c++ cmake pkgconf-pkg-config qt6-qtbase-devel qt6-qtmultimedia-devel \
        libgpod-devel glib2-devel sqlite-devel zlib-devel \
        libimobiledevice-utils usbmuxd ifuse ffmpeg python3 python3-pip
else
    echo "No reconozco tu gestor de paquetes. Instala a mano (ver README.md, sección Dependencias)." >&2
    exit 1
fi

# pykakasi: convierte kanji a romaji para ordenar alfabeticamente los nombres en japones (opcional)
if command -v python3 >/dev/null 2>&1; then
    python3 -m pip install --user pykakasi 2>/dev/null \
        || python3 -m pip install --user --break-system-packages pykakasi 2>/dev/null \
        || echo ">> Aviso: no se pudo instalar pykakasi. Sin el, el romaji solo convierte hiragana/katakana (no kanji)."
fi

echo
echo "Listo. Ahora compila con:"
echo "  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j\"\$(nproc)\""
