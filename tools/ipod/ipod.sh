#!/bin/bash
# Uso: ipod.sh list | add <mp3> [titulo] [artista] [dur_ms] | del <id> [id...] | umount
# Variables opcionales: IPOD_MOUNT (def. ~/ipod), IPOD_TOOLS (carpeta con gestor, nochmod.so y openssl-legacy.cnf)
HERE="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
TOOLS="${IPOD_TOOLS:-}"
if [ -z "$TOOLS" ]; then
  for d in "$HERE" "$HERE/../../build/tools/ipod"; do
    [ -x "$d/gestor" ] && [ -f "$d/nochmod.so" ] && TOOLS="$d" && break
  done
fi
[ -x "$TOOLS/gestor" ] || { echo "ERROR: no encuentro 'gestor'. Compila con CMake (build/tools/ipod) o define IPOD_TOOLS." >&2; exit 1; }
CNF="$TOOLS/openssl-legacy.cnf"; [ -f "$CNF" ] || CNF="$HERE/openssl-legacy.cnf"
export OPENSSL_CONF="$CNF"
MP="${IPOD_MOUNT:-$HOME/ipod}"
NOCHMOD="$TOOLS/nochmod.so"
RUIDO='Could not find corresponding track|CRITICAL|^\[mk_|^\[run_post|^itdb|^libitdbprep|^itlp|^\*\.itdb|library_persistent_id|device name|Locking for sync|^$'

montar() {
  mountpoint -q "$MP" && return 0
  mkdir -p "$MP"
  ifuse "$MP" || { echo "ERROR: no pude montar el iPod (desbloqueado y conectado?)" >&2; exit 1; }
}

escribir() {  # add / del: filtra ruido, devuelve 0 solo si la base se escribio
  local out rc
  out=$(LD_PRELOAD="$NOCHMOD" "$TOOLS/gestor" "$MP" "$@" 2>&1)
  echo "$out" | grep -Ev "$RUIDO"
  echo "$out" | grep -q 'escrita OK'; rc=$?
  sync
  return $rc
}

case "$1" in
  list)   montar; "$TOOLS/gestor" "$MP" list 2>/dev/null ;;
  add)    montar; escribir add "${@:2}" ;;
  del)    montar; escribir del "${@:2}" ;;
  umount) sync; fusermount -u "$MP" ;;
  *)      echo "Uso: $0 list | add <mp3> [titulo] [artista] [dur_ms] | del <id>... | umount" >&2; exit 2 ;;
esac
