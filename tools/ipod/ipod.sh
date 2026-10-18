#!/bin/bash
# Uso: ipod.sh list | add <mp3> [titulo] [artista] [dur_ms] | del <id> [id...] | umount
export OPENSSL_CONF="$HOME/openssl-legacy.cnf"
MP="$HOME/ipod"
NOCHMOD="$HOME/nochmod.so"
RUIDO='Could not find corresponding track|CRITICAL|^\[mk_|^\[run_post|^itdb|^libitdbprep|^itlp|^\*\.itdb|library_persistent_id|device name|Locking for sync|^$'

montar() {
  mountpoint -q "$MP" && return 0
  mkdir -p "$MP"
  ifuse "$MP" || { echo "ERROR: no pude montar el iPod (desbloqueado y conectado?)" >&2; exit 1; }
}

escribir() {  # add / del: filtra ruido, devuelve 0 solo si la base se escribio
  local out rc
  out=$(LD_PRELOAD="$NOCHMOD" ~/gestor "$MP" "$@" 2>&1)
  echo "$out" | grep -Ev "$RUIDO"
  echo "$out" | grep -q 'escrita OK'; rc=$?
  sync
  return $rc
}

case "$1" in
  list)   montar; ~/gestor "$MP" list 2>/dev/null ;;
  add)    montar; escribir add "${@:2}" ;;
  del)    montar; escribir del "${@:2}" ;;
  umount) sync; fusermount -u "$MP" ;;
  *)      echo "Uso: $0 list | add <mp3> [titulo] [artista] [dur_ms] | del <id>... | umount" >&2; exit 2 ;;
esac
