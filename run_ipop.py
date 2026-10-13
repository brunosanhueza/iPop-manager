#!/usr/bin/env python3
"""
iPop - Gestor Moderno de iPod
Interfaz moderna estilo iTunes con organizador de carpetas/ficheros en árbol,
lector y escritor binario nativo de iTunesDB y eliminación física real de archivos.
"""

import sys
import os
import string
import struct
import hashlib
import subprocess
import time
from dataclasses import dataclass, field
from typing import List, Dict, Optional, Set

try:
    from PyQt6.QtWidgets import (
        QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
        QGridLayout, QFrame, QLabel, QComboBox, QPushButton, QTreeWidget,
        QTreeWidgetItem, QTableView, QLineEdit, QSplitter, QHeaderView,
        QMessageBox, QFileDialog, QToolTip, QProgressBar, QDialog, QSlider,
        QInputDialog, QMenu
    )
    from PyQt6.QtCore import (
        Qt, QAbstractTableModel, QModelIndex, QRectF, QPointF, QStorageInfo, QTimer, QUrl
    )
    from PyQt6.QtGui import (
        QPainter, QColor, QPainterPath, QLinearGradient, QPen, QPalette, QFont
    )
    try:
        from PyQt6.QtMultimedia import QMediaPlayer, QAudioOutput
        HAS_MULTIMEDIA = True
    except ImportError:
        HAS_MULTIMEDIA = False
except ImportError:
    print("\n[!] PyQt6 no está instalado en tu Python.")
    print("Ejecuta en tu terminal:  pip install PyQt6\n")
    sys.exit(1)


@dataclass
class Track:
    id: int
    title: str = "Desconocido"
    artist: str = "Artista Desconocido"
    album: str = "Álbum Desconocido"
    genre: str = ""
    bitrate: int = 256
    duration_ms: int = 0
    file_type: str = "MP3"
    size_bytes: int = 0
    file_path: str = ""
    selected: bool = False

    def formatted_duration(self) -> str:
        if self.duration_ms <= 0:
            return "0:00"
        s = self.duration_ms // 1000
        return f"{s // 60}:{s % 60:02d}"

    def formatted_bitrate(self) -> str:
        return f"{self.bitrate} kbps" if self.bitrate > 0 else "—"

    def formatted_size(self) -> str:
        if self.size_bytes <= 0:
            return "0 B"
        mb = self.size_bytes / (1024.0 * 1024.0)
        return f"{mb:.1f} MB" if mb < 1024.0 else f"{mb/1024.0:.2f} GB"


@dataclass
class Playlist:
    id: int
    name: str
    is_master: bool = False
    is_podcasts: bool = False
    is_videos: bool = False
    tracks: List[Track] = field(default_factory=list)

    def total_bytes(self) -> int:
        return sum(t.size_bytes for t in self.tracks)

    def total_duration_ms(self) -> int:
        return sum(t.duration_ms for t in self.tracks)

    def formatted_summary(self) -> str:
        secs = self.total_duration_ms() // 1000
        h = secs // 3600
        m = (secs % 3600) // 60
        time_str = f"{h} horas, {m} minutos" if h > 0 else f"{m} minutos"
        mb = self.total_bytes() / (1024.0 * 1024.0)
        size_str = f"{mb/1024.0:.2f} GB" if mb >= 1024.0 else f"{mb:.1f} MB"
        return f"{len(self.tracks)} canciones • {time_str} • {size_str}"


def parse_itunesdb(db_path: str):
    """
    Decodificador binario exacto de iTunesDB de Apple para iPod.
    Interpreta mhbd, mhsd (tracks y playlists), mhit, mhod (strings UTF-16LE) y mhip.
    """
    try:
        with open(db_path, "rb") as f:
            data = f.read()
    except Exception as e:
        print(f"Error leyendo {db_path}: {e}")
        return {}, []

    if len(data) < 100 or data[:4] != b'mhbd':
        return {}, []

    mhbd_hdr_len = struct.unpack('<I', data[4:8])[0]
    pos = mhbd_hdr_len
    tracks: Dict[int, Track] = {}
    playlists: List[Playlist] = []

    while pos + 16 <= len(data):
        if data[pos:pos+4] != b'mhsd':
            break
        hdr_len, tot_len, ds_type = struct.unpack('<III', data[pos+4:pos+16])
        child_pos = pos + hdr_len

        # Dataset 1: Lista maestra de pistas (mhlt -> mhit)
        if ds_type == 1 and child_pos + 12 <= len(data) and data[child_pos:child_pos+4] == b'mhlt':
            mhlt_hdr, num_tracks = struct.unpack('<II', data[child_pos+4:child_pos+12])
            tpos = child_pos + mhlt_hdr

            for _ in range(num_tracks):
                if tpos + 40 > len(data) or data[tpos:tpos+4] != b'mhit':
                    break
                thdr_len, ttot_len, num_mhod, uid = struct.unpack('<IIII', data[tpos+4:tpos+20])

                title = ""
                artist = ""
                album = ""
                path = ""
                genre = ""
                file_type = "MP3"

                fourcc = data[tpos+24:tpos+28].decode('latin1', errors='ignore').strip()
                if fourcc:
                    file_type = fourcc

                size_bytes = struct.unpack('<I', data[tpos+36:tpos+40])[0]
                duration_ms = struct.unpack('<I', data[tpos+40:tpos+44])[0]
                bitrate = struct.unpack('<I', data[tpos+56:tpos+60])[0]

                mpos = tpos + thdr_len
                for _ in range(num_mhod):
                    if mpos + 40 > len(data) or data[mpos:mpos+4] != b'mhod':
                        break
                    mhdr_len, mtot_len, mtype = struct.unpack('<III', data[mpos+4:mpos+16])
                    if mtype in (1, 2, 3, 4, 5, 6):
                        str_len = struct.unpack('<I', data[mpos+28:mpos+32])[0]
                        str_end = mpos + 40 + str_len
                        if str_end <= len(data):
                            val = data[mpos+40:str_end].decode('utf-16le', errors='ignore').strip()
                            if mtype == 1: title = val
                            elif mtype == 2: path = val
                            elif mtype == 3: album = val
                            elif mtype == 4: artist = val
                            elif mtype == 5: genre = val
                            elif mtype == 6 and val: file_type = val
                    if mtot_len == 0: break
                    mpos += mtot_len

                if not title:
                    title = f"Pista #{uid}"

                tracks[uid] = Track(
                    id=uid,
                    title=title,
                    artist=artist or "Artista Desconocido",
                    album=album or "Álbum Desconocido",
                    genre=genre,
                    bitrate=bitrate,
                    duration_ms=duration_ms,
                    file_type=file_type,
                    size_bytes=size_bytes,
                    file_path=path
                )
                tpos += ttot_len

        # Dataset 2: Listas de reproducción (mhlp -> mhyp -> mhip)
        elif ds_type == 2 and child_pos + 12 <= len(data) and data[child_pos:child_pos+4] == b'mhlp':
            mhlp_hdr, num_pl = struct.unpack('<II', data[child_pos+4:child_pos+12])
            pl_pos = child_pos + mhlp_hdr

            for pi in range(num_pl):
                if pl_pos + 20 > len(data) or data[pl_pos:pl_pos+4] != b'mhyp':
                    break
                phdr_len, ptot_len, pnum_mhod, pnum_tracks = struct.unpack('<IIII', data[pl_pos+4:pl_pos+20])

                pl_name = f"Playlist #{pi}"
                mpos = pl_pos + phdr_len
                for _ in range(pnum_mhod):
                    if mpos + 40 > len(data) or data[mpos:mpos+4] != b'mhod':
                        break
                    mhdr_len, mtot_len, mtype = struct.unpack('<III', data[mpos+4:mpos+16])
                    if mtype == 1:
                        str_len = struct.unpack('<I', data[mpos+28:mpos+32])[0]
                        str_end = mpos + 40 + str_len
                        if str_end <= len(data):
                            pl_name = data[mpos+40:str_end].decode('utf-16le', errors='ignore').strip()
                    if mtot_len == 0: break
                    mpos += mtot_len

                pl_tracks: List[Track] = []
                hip_pos = mpos
                for _ in range(pnum_tracks):
                    if hip_pos + 28 > len(data) or data[hip_pos:hip_pos+4] != b'mhip':
                        break
                    htot_len = struct.unpack('<I', data[hip_pos+8:hip_pos+12])[0]
                    track_uid = struct.unpack('<I', data[hip_pos+24:hip_pos+28])[0]
                    if track_uid in tracks:
                        pl_tracks.append(tracks[track_uid])
                    if htot_len == 0: break
                    hip_pos += htot_len

                playlists.append(Playlist(
                    id=pi + 1,
                    name=pl_name,
                    is_master=(pi == 0),
                    tracks=pl_tracks
                ))
                pl_pos += ptot_len

        pos += tot_len

    return tracks, playlists


HASH58_INV = [
    0x74, 0x85, 0x96, 0xA7, 0xB8, 0xC9, 0xDA, 0xEB, 0xFC, 0x0D, 0x1E, 0x2F, 0x40, 0x51, 0x62, 0x73,
    0x84, 0x95, 0xA6, 0xB7, 0xC8, 0xD9, 0xEA, 0xFB, 0x0C, 0x1D, 0x2E, 0x3F, 0x50, 0x61, 0x72, 0x83,
    0x94, 0xA5, 0xB6, 0xC7, 0xD8, 0xE9, 0xFA, 0x0B, 0x1C, 0x2D, 0x3E, 0x4F, 0x60, 0x71, 0x82, 0x93,
    0xA4, 0xB5, 0xC6, 0xD7, 0xE8, 0xF9, 0x0A, 0x1B, 0x2C, 0x3D, 0x4E, 0x5F, 0x70, 0x81, 0x92, 0xA3,
    0xB4, 0xC5, 0xD6, 0xE7, 0xF8, 0x09, 0x1A, 0x2B, 0x3C, 0x4D, 0x5E, 0x6F, 0x80, 0x91, 0xA2, 0xB3,
    0xC4, 0xD5, 0xE6, 0xF7, 0x08, 0x19, 0x2A, 0x3B, 0x4C, 0x5D, 0x6E, 0x7F, 0x90, 0xA1, 0xB2, 0xC3,
    0xD4, 0xE5, 0xF6, 0x07, 0x18, 0x29, 0x3A, 0x4B, 0x5C, 0x6D, 0x7E, 0x8F, 0xA0, 0xB1, 0xC2, 0xD3,
    0xE4, 0xF5, 0x06, 0x17, 0x28, 0x39, 0x4A, 0x5B, 0x6C, 0x7D, 0x8E, 0x9F, 0xB0, 0xC1, 0xD2, 0xE3,
    0xF4, 0x05, 0x16, 0x27, 0x38, 0x49, 0x5A, 0x6B, 0x7C, 0x8D, 0x9E, 0xAF, 0xC0, 0xD1, 0xE2, 0xF3,
    0x04, 0x15, 0x26, 0x37, 0x48, 0x59, 0x6A, 0x7B, 0x8C, 0x9D, 0xAE, 0xBF, 0xD0, 0xE1, 0xF2, 0x03,
    0x14, 0x25, 0x36, 0x47, 0x58, 0x69, 0x7A, 0x8B, 0x9C, 0xAD, 0xBE, 0xCF, 0xE0, 0xF1, 0x02, 0x13,
    0x24, 0x35, 0x46, 0x57, 0x68, 0x79, 0x8A, 0x9B, 0xAC, 0xBD, 0xCE, 0xDF, 0xF0, 0x01, 0x12, 0x23,
    0x34, 0x45, 0x56, 0x67, 0x78, 0x89, 0x9A, 0xAB, 0xBC, 0xCD, 0xDE, 0xEF, 0x00, 0x11, 0x22, 0x33,
    0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x10, 0x21, 0x32, 0x43,
    0x54, 0x65, 0x76, 0x87, 0x98, 0xA9, 0xBA, 0xCB, 0xDC, 0xED, 0xFE, 0x0F, 0x20, 0x31, 0x42, 0x53,
    0x64, 0x75, 0x86, 0x97, 0xA8, 0xB9, 0xCA, 0xDB, 0xEC, 0xFD, 0x0E, 0x1F, 0x30, 0x41, 0x52, 0x63
]

HASH58_TABLE1 = [
    0x3A, 0x3F, 0x3E, 0x72, 0xBD, 0xA2, 0xD6, 0xB4, 0x63, 0xC0, 0x6E, 0x62, 0x59, 0x1E, 0xE2, 0x71,
    0xB5, 0x0D, 0xE8, 0x0C, 0x25, 0x38, 0xCE, 0x23, 0x7C, 0xB7, 0xAD, 0x16, 0xDF, 0x47, 0x3D, 0xB3,
    0x7E, 0x8C, 0xAA, 0x61, 0x31, 0x66, 0xBE, 0x4F, 0x97, 0x14, 0x54, 0xF0, 0x70, 0xEB, 0x30, 0xC4,
    0x27, 0x4E, 0xFA, 0x1A, 0x2B, 0x11, 0xF4, 0x45, 0x8E, 0x5D, 0x73, 0xED, 0x22, 0x2E, 0x7D, 0xA4,
    0x28, 0xDA, 0x2F, 0xC5, 0x92, 0x09, 0x05, 0x13, 0x9D, 0x32, 0x51, 0x4A, 0xC8, 0xBA, 0x96, 0xA7,
    0x6A, 0x50, 0xF3, 0xBC, 0x93, 0xBF, 0xB0, 0xD2, 0xD5, 0x82, 0x19, 0x98, 0x35, 0xCF, 0x6B, 0xB6,
    0x83, 0x56, 0x15, 0xF2, 0x9A, 0x9C, 0xCA, 0x74, 0x34, 0x58, 0x8D, 0xA6, 0x03, 0xFF, 0x46, 0x7B,
    0xD0, 0x7A, 0x33, 0x76, 0xDD, 0xAC, 0xCB, 0x24, 0x7F, 0xB1, 0x85, 0x60, 0xC3, 0x26, 0x8A, 0x1D,
    0x1C, 0x8F, 0x2A, 0xEF, 0x06, 0xDE, 0x67, 0x5E, 0xE7, 0xAE, 0xD9, 0xCC, 0x07, 0x6C, 0xF8, 0x0A,
    0xD3, 0x40, 0x36, 0x1F, 0x2D, 0x95, 0x43, 0xDB, 0x01, 0x89, 0x4B, 0xF7, 0xB9, 0x39, 0xC2, 0x52,
    0x53, 0xFD, 0x65, 0xF5, 0x68, 0xC1, 0xC7, 0x9F, 0x4D, 0xEA, 0xAF, 0x6D, 0x10, 0x44, 0x87, 0xD8,
    0xEE, 0x1B, 0xFE, 0x3C, 0xDC, 0x84, 0x69, 0x48, 0x6F, 0xD1, 0x57, 0x55, 0xD4, 0xA5, 0x49, 0x5B,
    0xE5, 0x0B, 0x94, 0xC9, 0x5F, 0xE1, 0x17, 0x81, 0xBB, 0xEC, 0xD7, 0xC6, 0x02, 0x4C, 0x42, 0x75,
    0xA3, 0x99, 0xE4, 0xA1, 0x9B, 0x5A, 0xF1, 0x29, 0xA0, 0x64, 0x9E, 0x18, 0x41, 0x80, 0x2C, 0x79,
    0x20, 0x8B, 0xAB, 0x90, 0x08, 0xB8, 0xA9, 0x77, 0x12, 0xF9, 0x0E, 0x88, 0xE9, 0x04, 0xFB, 0x86,
    0x0F, 0xE0, 0xA8, 0x5C, 0xE6, 0x21, 0xCD, 0x3B, 0x00, 0x78, 0xFC, 0xF6, 0xE3, 0x37, 0xB2, 0x91
]

HASH58_TABLE2 = [
    0xF3, 0xE4, 0x1B, 0x38, 0xE5, 0x6F, 0xE8, 0x9D, 0x3E, 0x55, 0xBA, 0xC7, 0xAC, 0xEA, 0x66, 0xA2,
    0xB9, 0x7A, 0x34, 0x43, 0x02, 0x4E, 0xFE, 0x36, 0x41, 0x57, 0x1A, 0xB1, 0x31, 0x87, 0x04, 0x52,
    0x21, 0x22, 0xE1, 0x13, 0x7F, 0x03, 0x3A, 0x90, 0xF7, 0x69, 0x78, 0x12, 0x83, 0x0B, 0x9A, 0x97,
    0x4D, 0xB7, 0x8C, 0xBF, 0x2D, 0x94, 0xD1, 0x93, 0x2F, 0x42, 0x23, 0xA4, 0xE0, 0x92, 0xDC, 0x68,
    0xD3, 0xDD, 0xAF, 0x91, 0x9F, 0xED, 0x3D, 0x8F, 0xA1, 0x51, 0xD9, 0xE9, 0x70, 0x28, 0xEF, 0xB3,
    0x49, 0xA5, 0x0D, 0xC5, 0xD0, 0x60, 0xB4, 0x2B, 0x07, 0xF8, 0xDF, 0xE6, 0x16, 0xC0, 0x30, 0x71,
    0x85, 0xFD, 0x72, 0x95, 0x29, 0x79, 0x0A, 0x7B, 0x46, 0x11, 0x7D, 0x88, 0x1D, 0x2A, 0x48, 0x1F,
    0x45, 0x89, 0x47, 0xEE, 0xBB, 0xBE, 0x6E, 0xC3, 0x6C, 0xCE, 0x10, 0x5A, 0x2C, 0xCA, 0xFB, 0xB2,
    0xCB, 0x1C, 0x9C, 0xEC, 0x2E, 0x56, 0x59, 0x9B, 0xA6, 0x53, 0xAE, 0x17, 0x25, 0xC1, 0x3F, 0x6A,
    0x0F, 0x09, 0x01, 0xA3, 0xD6, 0xA0, 0xD8, 0x08, 0xE3, 0x74, 0x06, 0x6D, 0x19, 0x98, 0x1E, 0x77,
    0x76, 0xBC, 0xEB, 0x3C, 0xB0, 0xC4, 0xC8, 0x64, 0x0E, 0x86, 0x63, 0xD7, 0xDB, 0xBD, 0xA7, 0x82,
    0x39, 0x4F, 0x27, 0xD2, 0x5F, 0x73, 0xF4, 0x75, 0x6B, 0xC2, 0xD5, 0x67, 0x5D, 0x80, 0xAB, 0x81,
    0xDE, 0xF0, 0xAD, 0xAA, 0xCD, 0xB6, 0xF6, 0x7C, 0xFC, 0x33, 0x05, 0x14, 0x96, 0x15, 0xC9, 0x9E,
    0x35, 0x5C, 0x7E, 0x44, 0x54, 0x58, 0x3B, 0x40, 0x20, 0xA8, 0x8B, 0x5E, 0x4A, 0x24, 0x99, 0x8E,
    0xF5, 0xB5, 0x62, 0x00, 0x37, 0x5B, 0x18, 0x65, 0x8D, 0x32, 0xE2, 0xF9, 0xDA, 0x8A, 0xD4, 0xCC,
    0x26, 0xF2, 0xF1, 0xE7, 0x4B, 0xC6, 0xCF, 0xFF, 0x4C, 0x84, 0x61, 0xFA, 0xB8, 0x0C, 0xA9, 0x50
]

HASH58_FIXED = [
    0x67, 0x23, 0xFE, 0x30, 0x45, 0x33, 0xF8, 0x90, 0x99, 0x21, 0x07, 0xC1, 0xD0, 0x12, 0xB2, 0xA1, 0x07, 0x81
]

def _gcd(a: int, b: int) -> int:
    while b > 0: a, b = b, a % b
    return a

def _lcm(a: int, b: int) -> int:
    if a == 0 or b == 0: return 1
    return (a * b) // _gcd(a, b)

def update_itunesdb_hash58(db: bytearray, fwid_str: str) -> bool:
    if len(db) < 108 or db[:4] != b'mhbd':
        return False
    guid = fwid_str.strip().upper()
    if len(guid) != 16:
        return False

    db_mod = bytearray(db)
    db_mod[24:32] = b'\x00' * 8
    db_mod[48:50] = b'\x01\x00'
    db_mod[50:70] = b'\x00' * 20
    db_mod[88:108] = b'\x00' * 20

    fwid = [int(guid[i:i+2], 16) for i in range(0, 16, 2)]
    key = [0] * 16
    for i in (0, 2, 4, 6):
        l = _lcm(fwid[i], fwid[i+1])
        hi, lo = (l >> 8) & 0xFF, l & 0xFF
        j = i << 1
        key[j]   = ((HASH58_TABLE1[hi] * 0xB5) - 0x03) & 0xFF
        key[j|1] = ((HASH58_TABLE2[hi] * 0xB7) + 0x49) & 0xFF
        key[j|2] = ((HASH58_TABLE1[lo] * 0xB5) - 0x03) & 0xFF
        key[j|3] = ((HASH58_TABLE2[lo] * 0xB7) + 0x49) & 0xFF

    key = [HASH58_INV[k] for k in key]
    full_key = bytes(HASH58_FIXED + key)
    key_hash = hashlib.sha1(full_key).digest()

    key1 = bytes([(k ^ 0x36) for k in key_hash] + [0x36] * 44)
    h1 = hashlib.sha1(key1 + db_mod).digest()

    key2 = bytes([(k ^ 0x36 ^ 0x5C) for k in key1])
    h2 = hashlib.sha1(key2 + h1).digest()

    db[88:108] = h2
    return True

def get_ipod_firewire_guid(mount_path: str) -> str:
    for sub in ["iPod_Control", "ipod_control"]:
        p = os.path.join(mount_path, sub, "Device", "SysInfo")
        if not os.path.exists(p):
            p = os.path.join(mount_path, sub, "device", "sysinfo")
        if os.path.exists(p):
            try:
                with open(p, "r", encoding="utf-8", errors="ignore") as f:
                    for line in f:
                        if line.lower().startswith("firewireguid:"):
                            return line.split(":", 1)[1].strip()
            except Exception:
                pass
    return ""

def flush_ipod_volume(mount_path: str):
    if sys.platform == 'win32':
        import ctypes
        drive = mount_path[:2]
        vol = f"\\\\.\\{drive}"
        GENERIC_READ_WRITE = 0xC0000000
        FILE_SHARE_READ_WRITE = 0x00000003
        OPEN_EXISTING = 3
        try:
            h = ctypes.windll.kernel32.CreateFileW(
                vol, GENERIC_READ_WRITE, FILE_SHARE_READ_WRITE, None, OPEN_EXISTING, 0, None
            )
            if h != -1:
                ctypes.windll.kernel32.FlushFileBuffers(h)
                ctypes.windll.kernel32.CloseHandle(h)
            ctypes.windll.shell32.SHChangeNotify(0x00001000, 0x0005, mount_path, None)
        except Exception as e:
            print("Error flushing volume:", e)

def is_itunes_running() -> bool:
    if sys.platform == 'win32':
        try:
            out = subprocess.check_output(['tasklist', '/FI', 'IMAGENAME eq iTunes.exe'], text=True, creationflags=0x08000000)
            return 'iTunes.exe' in out
        except Exception:
            return False
    return False

def kill_itunes_process() -> bool:
    if sys.platform == 'win32':
        try:
            subprocess.run(['taskkill', '/F', '/IM', 'iTunes.exe'], check=False, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, creationflags=0x08000000)
            return True
        except Exception:
            return False
    return False


def remove_tracks_from_itunesdb(db_path: str, delete_uids: Set[int]) -> bool:
    """
    Modifica y reescribe la base de datos binaria iTunesDB en el almacenamiento del iPod,
    eliminando permanentemente las pistas de mhit y sus referencias mhip en las playlists,
    recalculando la firma criptográfica hash58 para Nano 3G / Classic y vaciando los búferes.
    """
    if not delete_uids or not os.path.exists(db_path):
        return False

    with open(db_path, "rb") as f:
        data = f.read()

    if len(data) < 100 or data[:4] != b'mhbd':
        return False

    # Copia de seguridad automática
    bak_path = db_path + ".bak"
    if not os.path.exists(bak_path):
        with open(bak_path, "wb") as bf:
            bf.write(data)

    mhbd_hdr_len = struct.unpack('<I', data[4:8])[0]
    out_mhbd = bytearray(data[:mhbd_hdr_len])

    pos = mhbd_hdr_len
    new_mhsd_blocks = []

    while pos + 16 <= len(data):
        if data[pos:pos+4] != b'mhsd':
            break
        hdr_len, tot_len, ds_type = struct.unpack('<III', data[pos+4:pos+16])
        child_pos = pos + hdr_len

        if ds_type == 1 and data[child_pos:child_pos+4] == b'mhlt':
            mhsd_hdr = bytearray(data[pos:child_pos])
            mhlt_hdr_len, num_tracks = struct.unpack('<II', data[child_pos+4:child_pos+12])
            mhlt_hdr = bytearray(data[child_pos:child_pos+mhlt_hdr_len])

            remaining_mhits = []
            tpos = child_pos + mhlt_hdr_len
            for _ in range(num_tracks):
                if tpos + 40 > len(data) or data[tpos:tpos+4] != b'mhit':
                    break
                thdr_len, ttot_len, num_mhod, uid = struct.unpack('<IIII', data[tpos+4:tpos+20])
                if uid not in delete_uids:
                    remaining_mhits.append(data[tpos:tpos+ttot_len])
                tpos += ttot_len

            struct.pack_into('<I', mhlt_hdr, 8, len(remaining_mhits))
            ds1_payload = mhlt_hdr + b''.join(remaining_mhits)
            struct.pack_into('<I', mhsd_hdr, 8, len(mhsd_hdr) + len(ds1_payload))
            new_mhsd_blocks.append(mhsd_hdr + ds1_payload)

        elif ds_type in (2, 3) and data[child_pos:child_pos+4] == b'mhlp':
            mhsd_hdr = bytearray(data[pos:child_pos])
            mhlp_hdr_len, num_pl = struct.unpack('<II', data[child_pos+4:child_pos+12])
            mhlp_hdr = bytearray(data[child_pos:child_pos+mhlp_hdr_len])

            remaining_mhyps = []
            pl_pos = child_pos + mhlp_hdr_len

            for pi in range(num_pl):
                if pl_pos + 20 > len(data) or data[pl_pos:pl_pos+4] != b'mhyp':
                    break
                phdr_len, ptot_len, pnum_mhod, pnum_tracks = struct.unpack('<IIII', data[pl_pos+4:pl_pos+20])
                mhyp_hdr = bytearray(data[pl_pos:pl_pos+phdr_len])

                mpos = pl_pos + phdr_len
                mhods_bytes = bytearray()
                for _ in range(pnum_mhod):
                    if mpos + 12 > len(data) or data[mpos:mpos+4] != b'mhod':
                        break
                    mtot_len = struct.unpack('<I', data[mpos+8:mpos+12])[0]
                    mhods_bytes.extend(data[mpos:mpos+mtot_len])
                    mpos += mtot_len

                remaining_mhips = []
                hip_pos = mpos
                for _ in range(pnum_tracks):
                    if hip_pos + 28 > len(data) or data[hip_pos:hip_pos+4] != b'mhip':
                        break
                    htot_len = struct.unpack('<I', data[hip_pos+8:hip_pos+12])[0]
                    track_uid = struct.unpack('<I', data[hip_pos+24:hip_pos+28])[0]
                    if track_uid not in delete_uids:
                        remaining_mhips.append(data[hip_pos:hip_pos+htot_len])
                    hip_pos += htot_len

                struct.pack_into('<I', mhyp_hdr, 16, len(remaining_mhips))
                mhyp_payload = mhods_bytes + b''.join(remaining_mhips)
                struct.pack_into('<I', mhyp_hdr, 8, len(mhyp_hdr) + len(mhyp_payload))
                remaining_mhyps.append(mhyp_hdr + mhyp_payload)
                pl_pos += ptot_len

            ds_payload = mhlp_hdr + b''.join(remaining_mhyps)
            struct.pack_into('<I', mhsd_hdr, 8, len(mhsd_hdr) + len(ds_payload))
            new_mhsd_blocks.append(mhsd_hdr + ds_payload)

        else:
            new_mhsd_blocks.append(data[pos:pos+tot_len])

        pos += tot_len

    full_body = b''.join(new_mhsd_blocks)
    struct.pack_into('<I', out_mhbd, 8, len(out_mhbd) + len(full_body))
    new_db = bytearray(out_mhbd + full_body)

    # Recalcular firma hash58 para iPod Nano 3G / Classic
    mount_dir = os.path.dirname(os.path.dirname(os.path.dirname(db_path)))
    guid = get_ipod_firewire_guid(mount_dir)
    if guid:
        update_itunesdb_hash58(new_db, guid)

    tmp_path = db_path + ".tmp"
    with open(tmp_path, "wb") as f:
        f.write(new_db)
        f.flush()
        os.fsync(f.fileno())

    if os.path.exists(db_path):
        os.remove(db_path)
    os.rename(tmp_path, db_path)

    flush_ipod_volume(mount_dir)
    return True


def make_mhod_string(type_id: int, text: str) -> bytearray:
    u16 = text.encode('utf-16le')
    hdr_len = 24
    tot_len = hdr_len + 16 + len(u16)
    mhod = bytearray(tot_len)
    mhod[0:4] = b'mhod'
    struct.pack_into('<IIII', mhod, 4, hdr_len, tot_len, type_id, 0)
    struct.pack_into('<II', mhod, 20, 1, 0)
    struct.pack_into('<III', mhod, 28, len(u16), 0, 0)
    mhod[40:40+len(u16)] = u16
    return mhod


def delete_playlists_from_itunesdb(db_path: str, playlist_indices: Set[int]) -> bool:
    if not playlist_indices or not os.path.exists(db_path):
        return False

    with open(db_path, "rb") as f:
        data = f.read()

    if len(data) < 100 or data[:4] != b'mhbd':
        return False

    bak_path = db_path + ".bak"
    if not os.path.exists(bak_path):
        with open(bak_path, "wb") as bf:
            bf.write(data)

    mhbd_hdr_len = struct.unpack('<I', data[4:8])[0]
    out_mhbd = bytearray(data[:mhbd_hdr_len])

    pos = mhbd_hdr_len
    new_mhsd_blocks = []

    while pos + 16 <= len(data):
        if data[pos:pos+4] != b'mhsd':
            break
        hdr_len, tot_len, ds_type = struct.unpack('<III', data[pos+4:pos+16])
        child_pos = pos + hdr_len

        if ds_type in (2, 3) and data[child_pos:child_pos+4] == b'mhlp':
            mhsd_hdr = bytearray(data[pos:child_pos])
            mhlp_hdr_len, num_pl = struct.unpack('<II', data[child_pos+4:child_pos+12])
            mhlp_hdr = bytearray(data[child_pos:child_pos+mhlp_hdr_len])

            remaining_mhyps = []
            pl_pos = child_pos + mhlp_hdr_len
            kept_count = 0

            for pi in range(num_pl):
                if pl_pos + 12 > len(data) or data[pl_pos:pl_pos+4] != b'mhyp':
                    break
                ptot_len = struct.unpack('<I', data[pl_pos+8:pl_pos+12])[0]
                if pi != 0 and pi in playlist_indices:
                    pass  # Omitir esta playlist
                else:
                    remaining_mhyps.append(data[pl_pos:pl_pos+ptot_len])
                    kept_count += 1
                if ptot_len == 0:
                    break
                pl_pos += ptot_len

            struct.pack_into('<I', mhlp_hdr, 8, kept_count)
            ds_payload = mhlp_hdr + b''.join(remaining_mhyps)
            struct.pack_into('<I', mhsd_hdr, 8, len(mhsd_hdr) + len(ds_payload))
            new_mhsd_blocks.append(mhsd_hdr + ds_payload)
        else:
            new_mhsd_blocks.append(data[pos:pos+tot_len])

        if tot_len == 0:
            break
        pos += tot_len

    full_body = b''.join(new_mhsd_blocks)
    struct.pack_into('<I', out_mhbd, 8, len(out_mhbd) + len(full_body))
    new_db = bytearray(out_mhbd + full_body)

    mount_dir = os.path.dirname(os.path.dirname(os.path.dirname(db_path)))
    guid = get_ipod_firewire_guid(mount_dir)
    if guid:
        update_itunesdb_hash58(new_db, guid)

    tmp_path = db_path + ".tmp"
    with open(tmp_path, "wb") as f:
        f.write(new_db)
        f.flush()
        os.fsync(f.fileno())

    if os.path.exists(db_path):
        os.remove(db_path)
    os.rename(tmp_path, db_path)

    flush_ipod_volume(mount_dir)
    return True


def rename_playlist_in_itunesdb(db_path: str, playlist_idx: int, new_name: str) -> bool:
    if playlist_idx <= 0 or not new_name.strip() or not os.path.exists(db_path):
        return False

    with open(db_path, "rb") as f:
        data = f.read()

    if len(data) < 100 or data[:4] != b'mhbd':
        return False

    bak_path = db_path + ".bak"
    if not os.path.exists(bak_path):
        with open(bak_path, "wb") as bf:
            bf.write(data)

    mhbd_hdr_len = struct.unpack('<I', data[4:8])[0]
    out_mhbd = bytearray(data[:mhbd_hdr_len])

    pos = mhbd_hdr_len
    new_mhsd_blocks = []

    while pos + 16 <= len(data):
        if data[pos:pos+4] != b'mhsd':
            break
        hdr_len, tot_len, ds_type = struct.unpack('<III', data[pos+4:pos+16])
        child_pos = pos + hdr_len

        if ds_type in (2, 3) and data[child_pos:child_pos+4] == b'mhlp':
            mhsd_hdr = bytearray(data[pos:child_pos])
            mhlp_hdr_len, num_pl = struct.unpack('<II', data[child_pos+4:child_pos+12])
            mhlp_hdr = bytearray(data[child_pos:child_pos+mhlp_hdr_len])

            remaining_mhyps = []
            pl_pos = child_pos + mhlp_hdr_len

            for pi in range(num_pl):
                if pl_pos + 16 > len(data) or data[pl_pos:pl_pos+4] != b'mhyp':
                    break
                phdr_len, ptot_len, pnum_mhod = struct.unpack('<III', data[pl_pos+4:pl_pos+16])

                if pi == playlist_idx:
                    mhyp_hdr = bytearray(data[pl_pos:pl_pos+phdr_len])
                    mpos = pl_pos + phdr_len
                    new_mhods = bytearray()
                    title_replaced = False

                    for _ in range(pnum_mhod):
                        if mpos + 16 > len(data) or data[mpos:mpos+4] != b'mhod':
                            break
                        mhdr_len, mtot_len, mtype = struct.unpack('<III', data[mpos+4:mpos+16])
                        if mtype == 1:
                            new_mhods.extend(make_mhod_string(1, new_name.strip()))
                            title_replaced = True
                        else:
                            new_mhods.extend(data[mpos:mpos+mtot_len])
                        if mtot_len == 0:
                            break
                        mpos += mtot_len

                    if not title_replaced:
                        title_bytes = make_mhod_string(1, new_name.strip())
                        new_mhods = title_bytes + new_mhods
                        struct.pack_into('<I', mhyp_hdr, 12, pnum_mhod + 1)

                    hips_len = ptot_len - (mpos - pl_pos)
                    hips_bytes = data[mpos:mpos+hips_len] if hips_len > 0 else b''

                    new_pl_tot_len = len(mhyp_hdr) + len(new_mhods) + len(hips_bytes)
                    struct.pack_into('<I', mhyp_hdr, 8, new_pl_tot_len)
                    remaining_mhyps.append(mhyp_hdr + new_mhods + hips_bytes)
                else:
                    remaining_mhyps.append(data[pl_pos:pl_pos+ptot_len])

                if ptot_len == 0:
                    break
                pl_pos += ptot_len

            ds_payload = mhlp_hdr + b''.join(remaining_mhyps)
            struct.pack_into('<I', mhsd_hdr, 8, len(mhsd_hdr) + len(ds_payload))
            new_mhsd_blocks.append(mhsd_hdr + ds_payload)
        else:
            new_mhsd_blocks.append(data[pos:pos+tot_len])

        if tot_len == 0:
            break
        pos += tot_len

    full_body = b''.join(new_mhsd_blocks)
    struct.pack_into('<I', out_mhbd, 8, len(out_mhbd) + len(full_body))
    new_db = bytearray(out_mhbd + full_body)

    mount_dir = os.path.dirname(os.path.dirname(os.path.dirname(db_path)))
    guid = get_ipod_firewire_guid(mount_dir)
    if guid:
        update_itunesdb_hash58(new_db, guid)

    tmp_path = db_path + ".tmp"
    with open(tmp_path, "wb") as f:
        f.write(new_db)
        f.flush()
        os.fsync(f.fileno())

    if os.path.exists(db_path):
        os.remove(db_path)
    os.rename(tmp_path, db_path)

    flush_ipod_volume(mount_dir)
    return True


def create_playlist_in_itunesdb(db_path: str, name: str) -> bool:
    if not name.strip() or not os.path.exists(db_path):
        return False

    with open(db_path, "rb") as f:
        data = f.read()

    if len(data) < 100 or data[:4] != b'mhbd':
        return False

    bak_path = db_path + ".bak"
    if not os.path.exists(bak_path):
        with open(bak_path, "wb") as bf:
            bf.write(data)

    mhbd_hdr_len = struct.unpack('<I', data[4:8])[0]
    out_mhbd = bytearray(data[:mhbd_hdr_len])

    pos = mhbd_hdr_len
    new_mhsd_blocks = []

    title_mhod = make_mhod_string(1, name.strip())
    pl_hdr_len = 24
    pl_tot_len = pl_hdr_len + len(title_mhod)
    new_mhyp = bytearray(pl_hdr_len)
    new_mhyp[:4] = b'mhyp'
    struct.pack_into('<IIII', new_mhyp, 4, pl_hdr_len, pl_tot_len, 1, 0)
    new_mhyp.extend(title_mhod)

    while pos + 16 <= len(data):
        if data[pos:pos+4] != b'mhsd':
            break
        hdr_len, tot_len, ds_type = struct.unpack('<III', data[pos+4:pos+16])
        child_pos = pos + hdr_len

        if ds_type in (2, 3) and data[child_pos:child_pos+4] == b'mhlp':
            mhsd_hdr = bytearray(data[pos:child_pos])
            mhlp_hdr_len, num_pl = struct.unpack('<II', data[child_pos+4:child_pos+12])
            mhlp_hdr = bytearray(data[child_pos:child_pos+mhlp_hdr_len])

            struct.pack_into('<I', mhlp_hdr, 8, num_pl + 1)
            playlists_len = tot_len - hdr_len - mhlp_hdr_len
            playlists_data = bytearray(data[child_pos+mhlp_hdr_len:child_pos+mhlp_hdr_len+playlists_len])
            playlists_data.extend(new_mhyp)

            ds_payload = mhlp_hdr + playlists_data
            struct.pack_into('<I', mhsd_hdr, 8, len(mhsd_hdr) + len(ds_payload))
            new_mhsd_blocks.append(mhsd_hdr + ds_payload)
        else:
            new_mhsd_blocks.append(data[pos:pos+tot_len])

        if tot_len == 0:
            break
        pos += tot_len

    full_body = b''.join(new_mhsd_blocks)
    struct.pack_into('<I', out_mhbd, 8, len(out_mhbd) + len(full_body))
    new_db = bytearray(out_mhbd + full_body)

    mount_dir = os.path.dirname(os.path.dirname(os.path.dirname(db_path)))
    guid = get_ipod_firewire_guid(mount_dir)
    if guid:
        update_itunesdb_hash58(new_db, guid)

    tmp_path = db_path + ".tmp"
    with open(tmp_path, "wb") as f:
        f.write(new_db)
        f.flush()
        os.fsync(f.fileno())

    if os.path.exists(db_path):
        os.remove(db_path)
    os.rename(tmp_path, db_path)

    flush_ipod_volume(mount_dir)
    return True


class CapacityBar(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setMouseTracking(True)
        self.setFixedHeight(22)
        self.total_bytes = 0
        self.audio_bytes = 0
        self.video_bytes = 0
        self.podcast_bytes = 0
        self.photo_bytes = 0
        self.other_bytes = 0
        self.free_bytes = 0
        self.segments = []

    def set_storage(self, total, audio, video, podcast, photo, other, free):
        self.total_bytes = total
        self.audio_bytes = audio
        self.video_bytes = video
        self.podcast_bytes = podcast
        self.photo_bytes = photo
        self.other_bytes = other
        self.free_bytes = free
        self.update()

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        self.segments.clear()

        w, h = float(self.width()), float(self.height())
        rect = QRectF(0, 0, w, h)
        clip = QPainterPath()
        clip.addRoundedRect(rect, 6.0, 6.0)
        painter.setClipPath(clip)

        painter.fillRect(rect, QColor(45, 55, 72))

        if self.total_bytes == 0:
            painter.setPen(QColor(160, 174, 192))
            painter.drawText(rect, Qt.AlignmentFlag.AlignCenter, "Sin almacenamiento detectado")
            return

        cats = [
            ("Música", self.audio_bytes, QColor(249, 115, 22), QColor(234, 88, 12)),
            ("Video", self.video_bytes, QColor(6, 182, 212), QColor(14, 116, 144)),
            ("Podcasts", self.podcast_bytes, QColor(168, 85, 247), QColor(126, 34, 206)),
            ("Fotos", self.photo_bytes, QColor(245, 158, 11), QColor(180, 83, 9)),
            ("Otros", self.other_bytes, QColor(100, 116, 139), QColor(71, 85, 105)),
        ]

        cur_x = 0.0
        for name, b, c1, c2 in cats:
            if b <= 0: continue
            frac = b / float(self.total_bytes)
            seg_w = max(1.0, frac * w)
            s_rect = QRectF(cur_x, 0, seg_w, h)
            grad = QLinearGradient(s_rect.topLeft(), s_rect.bottomLeft())
            grad.setColorAt(0.0, c1)
            grad.setColorAt(1.0, c2)
            painter.fillRect(s_rect, grad)
            self.segments.append((s_rect, name, b, frac * 100.0))
            cur_x += seg_w

        if self.free_bytes > 0 and cur_x < w:
            free_rect = QRectF(cur_x, 0, w - cur_x, h)
            grad = QLinearGradient(free_rect.topLeft(), free_rect.bottomLeft())
            grad.setColorAt(0.0, QColor(75, 85, 99))
            grad.setColorAt(1.0, QColor(55, 65, 81))
            painter.fillRect(free_rect, grad)
            self.segments.append((free_rect, "Libre", self.free_bytes, (self.free_bytes / self.total_bytes) * 100.0))

        painter.setClipping(False)
        painter.setPen(QPen(QColor(255, 255, 255, 25), 1.0))
        painter.setBrush(Qt.BrushStyle.NoBrush)
        painter.drawRoundedRect(rect.adjusted(0.5, 0.5, -0.5, -0.5), 6.0, 6.0)

    def mouseMoveEvent(self, event):
        pos = event.position()
        for r, name, b, pct in self.segments:
            if r.contains(pos):
                mb = b / (1024.0 * 1024.0)
                sz = f"{mb/1024.0:.2f} GB" if mb >= 1024.0 else f"{mb:.1f} MB"
                tip = f"{name}: {sz} ({pct:.1f}%)"
                QToolTip.showText(event.globalPosition().toPoint(), tip, self)
                return
        QToolTip.hideText()


class TrackTableModel(QAbstractTableModel):
    HEADERS = ["✓", "Título", "Artista", "Álbum", "Calidad", "Duración", "Formato", "Tamaño"]

    def __init__(self, parent=None):
        super().__init__(parent)
        self.tracks: List[Track] = []

    def rowCount(self, parent=QModelIndex()):
        return len(self.tracks)

    def columnCount(self, parent=QModelIndex()):
        return len(self.HEADERS)

    def data(self, index, role=Qt.ItemDataRole.DisplayRole):
        if not index.isValid() or not (0 <= index.row() < len(self.tracks)):
            return None
        t = self.tracks[index.row()]
        col = index.column()

        if role == Qt.ItemDataRole.CheckStateRole and col == 0:
            return Qt.CheckState.Checked if t.selected else Qt.CheckState.Unchecked

        if role == Qt.ItemDataRole.DisplayRole:
            if col == 1: return t.title
            if col == 2: return t.artist
            if col == 3: return t.album
            if col == 4: return t.formatted_bitrate()
            if col == 5: return t.formatted_duration()
            if col == 6: return t.file_type
            if col == 7: return t.formatted_size()

        if role == Qt.ItemDataRole.TextAlignmentRole:
            if col == 0: return Qt.AlignmentFlag.AlignCenter
            if col in (4, 6): return Qt.AlignmentFlag.AlignCenter
            if col in (5, 7): return int(Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter)
            return int(Qt.AlignmentFlag.AlignLeft | Qt.AlignmentFlag.AlignVCenter)

        return None

    def setData(self, index, value, role=Qt.ItemDataRole.EditRole):
        if index.isValid() and index.column() == 0 and role == Qt.ItemDataRole.CheckStateRole:
            self.tracks[index.row()].selected = (value == Qt.CheckState.Checked.value or value == Qt.CheckState.Checked)
            self.dataChanged.emit(index, index, [role])
            return True
        return False

    def headerData(self, section, orientation, role=Qt.ItemDataRole.DisplayRole):
        if orientation == Qt.Orientation.Horizontal and role == Qt.ItemDataRole.DisplayRole:
            return self.HEADERS[section]
        return None

    def flags(self, index):
        if not index.isValid(): return Qt.ItemFlag.NoItemFlags
        f = Qt.ItemFlag.ItemIsEnabled | Qt.ItemFlag.ItemIsSelectable
        if index.column() == 0:
            f |= Qt.ItemFlag.ItemIsUserCheckable
        return f

    def set_tracks(self, tracks):
        self.beginResetModel()
        self.tracks = tracks
        self.endResetModel()

    def select_all(self, select: bool):
        for t in self.tracks:
            t.selected = select
        if self.tracks:
            self.dataChanged.emit(self.index(0, 0), self.index(len(self.tracks)-1, 0), [Qt.ItemDataRole.CheckStateRole])

    def selected_ids(self) -> List[int]:
        return [t.id for t in self.tracks if t.selected]


class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("iPop - Gestor de iPod")
        self.resize(1180, 780)
        self.setMinimumSize(980, 640)

        self.current_mount = ""
        self.all_tracks: Dict[int, Track] = {}
        self.playlists: List[Playlist] = []
        self.active_tracks: List[Track] = []
        self.all_selected = False
        self.current_playlist_ref: Optional[Playlist] = None
        self.current_playing_track: Optional[Track] = None
        self.is_slider_seeking = False

        self.setAcceptDrops(True)
        self.setup_ui()

        self.itunes_timer = QTimer(self)
        self.itunes_timer.timeout.connect(self.check_itunes_status)
        self.itunes_timer.start(2000)
        self.check_itunes_status()

        self.refresh_devices()

    def check_itunes_status(self):
        if hasattr(self, 'itunes_warn_frame'):
            self.itunes_warn_frame.setVisible(is_itunes_running())

    def close_itunes_clicked(self):
        kill_itunes_process()
        self.check_itunes_status()

    def setup_ui(self):
        central = QWidget(self)
        self.setCentralWidget(central)
        root = QVBoxLayout(central)
        root.setContentsMargins(16, 14, 16, 14)
        root.setSpacing(12)

        # 1. Barra Superior
        top_bar = QFrame(self)
        top_bar.setStyleSheet("background-color: #1E293B; border-radius: 10px; padding: 6px 12px;")
        top_layout = QHBoxLayout(top_bar)

        brand = QLabel("iPop", self)
        brand.setStyleSheet("font-size: 20px; font-weight: bold; color: #38BDF8; letter-spacing: 1px;")
        top_layout.addWidget(brand)
        top_layout.addSpacing(14)

        lbl = QLabel("iPod:", self)
        lbl.setStyleSheet("color: #94A3B8; font-weight: 500;")
        top_layout.addWidget(lbl)

        self.device_combo = QComboBox(self)
        self.device_combo.setMinimumWidth(340)
        self.device_combo.setStyleSheet(
            "QComboBox { background-color: #0F172A; color: #F8FAFC; border: 1px solid #334155; "
            "border-radius: 6px; padding: 6px 12px; } "
            "QComboBox QAbstractItemView { background-color: #0F172A; color: #F8FAFC; selection-background-color: #2563EB; }"
        )
        self.device_combo.currentIndexChanged.connect(self.on_device_changed)
        top_layout.addWidget(self.device_combo, 1)

        ref_btn = QPushButton("Refrescar", self)
        ref_btn.setStyleSheet("background-color: #334155; color: white; border-radius: 6px; padding: 6px 14px;")
        ref_btn.clicked.connect(self.refresh_devices)
        top_layout.addWidget(ref_btn)

        browse_btn = QPushButton("Elegir carpeta...", self)
        browse_btn.setStyleSheet("background-color: #334155; color: white; border-radius: 6px; padding: 6px 14px;")
        browse_btn.clicked.connect(self.browse_mount)
        top_layout.addWidget(browse_btn)

        eject_btn = QPushButton("Expulsar", self)
        eject_btn.setStyleSheet("background-color: #DC2626; color: white; font-weight: bold; border-radius: 6px; padding: 6px 16px;")
        eject_btn.clicked.connect(self.eject_device)
        top_layout.addWidget(eject_btn)

        root.addWidget(top_bar)

        # Banner de advertencia de iTunes
        self.itunes_warn_frame = QFrame(self)
        self.itunes_warn_frame.setStyleSheet("background-color: #7C2D12; border: 1px solid #EA580C; border-radius: 8px; padding: 6px 12px;")
        warn_layout = QHBoxLayout(self.itunes_warn_frame)
        warn_layout.setContentsMargins(10, 4, 10, 4)
        warn_layout.setSpacing(10)
        warn_lbl = QLabel("⚠️ iTunes está abierto en segundo plano. iTunes mantiene la base de datos en RAM y no detecta cambios externos ni vacía los archivos borrados.", self.itunes_warn_frame)
        warn_lbl.setStyleSheet("color: #FFEDD5; font-size: 12px; font-weight: 500;")
        warn_layout.addWidget(warn_lbl, 1)
        close_itunes_btn = QPushButton("Cerrar iTunes", self.itunes_warn_frame)
        close_itunes_btn.setStyleSheet("background-color: #DC2626; color: white; border-radius: 4px; padding: 4px 12px; font-weight: bold; font-size: 11px;")
        close_itunes_btn.clicked.connect(self.close_itunes_clicked)
        warn_layout.addWidget(close_itunes_btn)
        self.itunes_warn_frame.setVisible(False)
        root.addWidget(self.itunes_warn_frame)

        # 1.5 Reproductor Integrado (Estilo iTunes)
        self.setup_player_ui(root)

        # 2. Splitter Central
        splitter = QSplitter(Qt.Orientation.Horizontal, self)
        splitter.setHandleWidth(4)

        # 2.1 Sidebar Izquierda (Árbol Organizador)
        tree_frame = QFrame(self)
        tree_frame.setStyleSheet("background-color: #1E293B; border-radius: 10px;")
        tree_layout = QVBoxLayout(tree_frame)
        tree_layout.setContentsMargins(10, 12, 10, 12)
        tree_layout.setSpacing(8)

        tree_title = QLabel("EXPLORADOR DEL DISPOSITIVO", self)
        tree_title.setStyleSheet("color: #64748B; font-size: 11px; font-weight: bold; letter-spacing: 1px; padding-left: 6px;")
        tree_layout.addWidget(tree_title)

        # Barra de acciones para listas de reproducción
        pl_tools_grid = QGridLayout()
        pl_tools_grid.setSpacing(6)
        pl_tools_grid.setContentsMargins(2, 0, 2, 4)

        self.btn_new_pl = QPushButton("➕ Nueva", self)
        self.btn_new_pl.setToolTip("Crear una nueva lista de reproducción en el iPod")
        self.btn_new_pl.setStyleSheet(
            "QPushButton { background-color: #334155; color: #F8FAFC; border-radius: 6px; padding: 5px 8px; font-size: 11px; font-weight: 500; } "
            "QPushButton:hover { background-color: #475569; }"
        )
        self.btn_new_pl.clicked.connect(self.on_new_playlist)
        pl_tools_grid.addWidget(self.btn_new_pl, 0, 0)

        self.btn_select_all_pl = QPushButton("☑️ Todas", self)
        self.btn_select_all_pl.setToolTip("Seleccionar o deseleccionar todas las listas de reproducción")
        self.btn_select_all_pl.setStyleSheet(
            "QPushButton { background-color: #334155; color: #F8FAFC; border-radius: 6px; padding: 5px 8px; font-size: 11px; font-weight: 500; } "
            "QPushButton:hover { background-color: #475569; }"
        )
        self.btn_select_all_pl.clicked.connect(self.on_toggle_select_all_playlists)
        pl_tools_grid.addWidget(self.btn_select_all_pl, 0, 1)

        self.btn_edit_pl = QPushButton("✏️ Editar", self)
        self.btn_edit_pl.setToolTip("Editar o renombrar la lista (habilitado si seleccionas exactamente una lista)")
        self.btn_edit_pl.setEnabled(False)
        self.btn_edit_pl.setStyleSheet(
            "QPushButton { background-color: #1E293B; color: #475569; border: 1px solid #334155; border-radius: 6px; padding: 5px 8px; font-size: 11px; font-weight: 500; } "
            "QPushButton:hover { background-color: #475569; color: white; } "
            "QPushButton:disabled { background-color: #1E293B; color: #475569; border: 1px solid #334155; }"
        )
        self.btn_edit_pl.clicked.connect(self.on_edit_selected_playlist)
        pl_tools_grid.addWidget(self.btn_edit_pl, 1, 0)

        self.btn_delete_pl = QPushButton("🗑️ Eliminar", self)
        self.btn_delete_pl.setToolTip("Eliminar las listas de reproducción seleccionadas")
        self.btn_delete_pl.setEnabled(False)
        self.btn_delete_pl.setStyleSheet(
            "QPushButton { background-color: #1E293B; color: #475569; border: 1px solid #334155; border-radius: 6px; padding: 5px 8px; font-size: 11px; font-weight: 500; } "
            "QPushButton:hover { background-color: #B91C1C; color: white; } "
            "QPushButton:disabled { background-color: #1E293B; color: #475569; border: 1px solid #334155; }"
        )
        self.btn_delete_pl.clicked.connect(self.on_delete_selected_playlists)
        pl_tools_grid.addWidget(self.btn_delete_pl, 1, 1)

        tree_layout.addLayout(pl_tools_grid)

        self.tree = QTreeWidget(self)
        self.tree.setHeaderHidden(True)
        self.tree.setAnimated(True)
        self.tree.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)
        self.tree.setStyleSheet(
            "QTreeWidget { background: transparent; border: none; color: #E2E8F0; font-size: 13px; } "
            "QTreeWidget::item { padding: 6px 8px; border-radius: 6px; margin: 1px 0; } "
            "QTreeWidget::item:hover { background-color: #334155; } "
            "QTreeWidget::item:selected { background-color: #2563EB; color: white; font-weight: 500; } "
            "QTreeWidget::indicator { width: 14px; height: 14px; border-radius: 3px; border: 1px solid #475569; background-color: #0F172A; } "
            "QTreeWidget::indicator:hover { border: 1px solid #38BDF8; } "
            "QTreeWidget::indicator:checked { background-color: #2563EB; border: 1px solid #38BDF8; }"
        )
        self.tree.itemClicked.connect(self.on_tree_item_clicked)
        self.tree.itemChanged.connect(self.on_tree_item_changed)
        self.tree.customContextMenuRequested.connect(self.on_tree_context_menu)
        self.tree.currentItemChanged.connect(lambda cur, prev: self.update_playlist_action_buttons())
        tree_layout.addWidget(self.tree, 1)

        splitter.addWidget(tree_frame)
        tree_frame.setMinimumWidth(240)
        tree_frame.setMaximumWidth(320)

        # 2.2 Panel Derecho
        songs_frame = QFrame(self)
        songs_frame.setStyleSheet("background-color: #1E293B; border-radius: 10px;")
        songs_layout = QVBoxLayout(songs_frame)
        songs_layout.setContentsMargins(16, 14, 16, 14)
        songs_layout.setSpacing(10)

        header_row = QHBoxLayout()
        t_box = QVBoxLayout()
        self.view_title_lbl = QLabel("Música", self)
        self.view_title_lbl.setStyleSheet("font-size: 20px; font-weight: bold; color: #F8FAFC;")
        t_box.addWidget(self.view_title_lbl)

        self.view_sum_lbl = QLabel("0 canciones", self)
        self.view_sum_lbl.setStyleSheet("color: #94A3B8; font-size: 12px;")
        t_box.addWidget(self.view_sum_lbl)
        header_row.addLayout(t_box)
        header_row.addStretch()

        self.search_edit = QLineEdit(self)
        self.search_edit.setPlaceholderText("Buscar canción, artista o álbum...")
        self.search_edit.setClearButtonEnabled(True)
        self.search_edit.setFixedWidth(260)
        self.search_edit.setStyleSheet("background-color: #0F172A; color: white; border: 1px solid #334155; border-radius: 6px; padding: 6px 12px;")
        self.search_edit.textChanged.connect(self.filter_tracks)
        header_row.addWidget(self.search_edit)

        songs_layout.addLayout(header_row)

        tools_row = QHBoxLayout()
        self.sel_all_btn = QPushButton("Seleccionar todas", self)
        self.sel_all_btn.setStyleSheet("background-color: #334155; color: white; border-radius: 6px; padding: 6px 12px; font-size: 12px;")
        self.sel_all_btn.clicked.connect(self.toggle_select_all)
        tools_row.addWidget(self.sel_all_btn)

        self.del_btn = QPushButton("Eliminar seleccionadas", self)
        self.del_btn.setStyleSheet("background-color: #DC2626; color: white; font-weight: bold; border-radius: 6px; padding: 6px 14px; font-size: 12px;")
        self.del_btn.clicked.connect(self.delete_selected)
        tools_row.addWidget(self.del_btn)

        tools_row.addStretch()
        songs_layout.addLayout(tools_row)

        self.table = QTableView(self)
        self.model = TrackTableModel(self)
        self.table.setModel(self.model)
        self.table.verticalHeader().setVisible(False)
        self.table.setShowGrid(False)
        self.table.setAlternatingRowColors(True)
        self.table.setSelectionBehavior(QTableView.SelectionBehavior.SelectRows)
        self.table.setStyleSheet(
            "QTableView { background-color: #0F172A; alternate-background-color: #142036; color: white; border: 1px solid #334155; border-radius: 6px; font-size: 13px; } "
            "QTableView::item { padding: 6px; } "
            "QHeaderView::section { background-color: #1E293B; color: #94A3B8; font-weight: bold; padding: 6px; border: none; border-bottom: 1px solid #334155; }"
        )

        self.table.horizontalHeader().setSectionResizeMode(0, QHeaderView.ResizeMode.Fixed)
        self.table.setColumnWidth(0, 34)
        self.table.horizontalHeader().setSectionResizeMode(1, QHeaderView.ResizeMode.Stretch)
        self.table.setColumnWidth(2, 170)
        self.table.setColumnWidth(3, 170)
        self.table.setColumnWidth(4, 90)
        self.table.setColumnWidth(5, 75)
        self.table.setColumnWidth(6, 75)
        self.table.setColumnWidth(7, 85)
        self.table.doubleClicked.connect(self.on_table_double_clicked)
        songs_layout.addWidget(self.table, 1)

        splitter.addWidget(songs_frame)
        splitter.setStretchFactor(1, 1)
        root.addWidget(splitter, 1)

        # 3. Panel Inferior
        bottom = QFrame(self)
        bottom.setStyleSheet("background-color: #1E293B; border-radius: 10px; padding: 8px 14px;")
        b_layout = QVBoxLayout(bottom)
        b_layout.setSpacing(6)

        b_row = QHBoxLayout()
        self.icon_lbl = QLabel("📱", self)
        self.icon_lbl.setStyleSheet("font-size: 24px;")
        b_row.addWidget(self.icon_lbl)

        self.model_lbl = QLabel("Sin dispositivo", self)
        self.model_lbl.setStyleSheet("font-size: 14px; font-weight: bold; color: white;")
        b_row.addWidget(self.model_lbl)
        b_row.addSpacing(12)

        self.sn_lbl = QLabel("", self)
        self.sn_lbl.setStyleSheet("color: #64748B; font-size: 12px;")
        b_row.addWidget(self.sn_lbl)
        b_row.addStretch()

        b_row.addWidget(QLabel("🔋", self))
        self.batt_lbl = QLabel("100% (Cargando)", self)
        self.batt_lbl.setStyleSheet("color: #10B981; font-weight: bold; font-size: 12px;")
        b_row.addWidget(self.batt_lbl)
        b_row.addSpacing(16)

        self.free_lbl = QLabel("Libre: —", self)
        self.free_lbl.setStyleSheet("color: #94A3B8; font-size: 12px;")
        b_row.addWidget(self.free_lbl)

        self.tot_lbl = QLabel("Total: —", self)
        self.tot_lbl.setStyleSheet("color: white; font-size: 12px; font-weight: bold;")
        b_row.addWidget(self.tot_lbl)
        b_layout.addLayout(b_row)

        self.cap_bar = CapacityBar(self)
        b_layout.addWidget(self.cap_bar)
        root.addWidget(bottom)

    def setup_player_ui(self, root):
        player_frame = QFrame(self)
        player_frame.setStyleSheet(
            "QFrame { background-color: #1E293B; border: 1px solid #334155; border-radius: 10px; padding: 4px 10px; }"
        )
        p_layout = QHBoxLayout(player_frame)
        p_layout.setContentsMargins(10, 4, 10, 4)
        p_layout.setSpacing(12)

        # Controles
        ctrl_layout = QHBoxLayout()
        ctrl_layout.setSpacing(8)

        self.btn_prev = QPushButton("⏮", self)
        self.btn_prev.setFixedSize(36, 36)
        self.btn_prev.setStyleSheet("background-color: #334155; color: white; border-radius: 18px; font-size: 14px; border: 1px solid #475569;")
        self.btn_prev.clicked.connect(self.play_prev_track)
        ctrl_layout.addWidget(self.btn_prev)

        self.btn_play = QPushButton("▶", self)
        self.btn_play.setFixedSize(44, 44)
        self.btn_play.setStyleSheet(
            "QPushButton { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #38BDF8, stop:1 #2563EB); "
            "color: white; border-radius: 22px; font-size: 18px; font-weight: bold; border: 1px solid #60A5FA; } "
            "QPushButton:hover { background: #38BDF8; }"
        )
        self.btn_play.clicked.connect(self.toggle_playback)
        ctrl_layout.addWidget(self.btn_play)

        self.btn_next = QPushButton("⏭", self)
        self.btn_next.setFixedSize(36, 36)
        self.btn_next.setStyleSheet("background-color: #334155; color: white; border-radius: 18px; font-size: 14px; border: 1px solid #475569;")
        self.btn_next.clicked.connect(self.play_next_track)
        ctrl_layout.addWidget(self.btn_next)
        p_layout.addLayout(ctrl_layout)

        # LCD Display (iTunes Style)
        lcd_frame = QFrame(self)
        lcd_frame.setStyleSheet("background-color: #020617; border: 1px solid #1E293B; border-radius: 8px; padding: 4px 8px;")
        lcd_layout = QVBoxLayout(lcd_frame)
        lcd_layout.setContentsMargins(8, 2, 8, 2)
        lcd_layout.setSpacing(2)

        top_info = QHBoxLayout()
        self.lcd_title = QLabel(" iPop Media Player", self)
        self.lcd_title.setStyleSheet("color: #F8FAFC; font-weight: bold; font-size: 13px;")
        top_info.addWidget(self.lcd_title)
        top_info.addStretch()
        self.lcd_sub = QLabel("Listo para reproducir", self)
        self.lcd_sub.setStyleSheet("color: #94A3B8; font-size: 11px;")
        top_info.addWidget(self.lcd_sub)
        lcd_layout.addLayout(top_info)

        scrub_layout = QHBoxLayout()
        scrub_layout.setSpacing(6)
        self.lcd_cur_time = QLabel("0:00", self)
        self.lcd_cur_time.setFixedWidth(36)
        self.lcd_cur_time.setStyleSheet("color: #64748B; font-size: 11px; font-family: monospace;")
        scrub_layout.addWidget(self.lcd_cur_time)

        self.lcd_slider = QSlider(Qt.Orientation.Horizontal, self)
        self.lcd_slider.setRange(0, 1000)
        self.lcd_slider.setValue(0)
        self.lcd_slider.setStyleSheet(
            "QSlider::groove:horizontal { height: 4px; background: #334155; border-radius: 2px; } "
            "QSlider::sub-page:horizontal { background: #38BDF8; border-radius: 2px; } "
            "QSlider::handle:horizontal { background: #FFFFFF; width: 10px; height: 10px; margin: -3px 0; border-radius: 5px; }"
        )
        self.lcd_slider.sliderPressed.connect(self.on_slider_pressed)
        self.lcd_slider.sliderReleased.connect(self.on_slider_released)
        self.lcd_slider.sliderMoved.connect(self.on_slider_moved)
        scrub_layout.addWidget(self.lcd_slider, 1)

        self.lcd_tot_time = QLabel("0:00", self)
        self.lcd_tot_time.setFixedWidth(40)
        self.lcd_tot_time.setAlignment(Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter)
        self.lcd_tot_time.setStyleSheet("color: #64748B; font-size: 11px; font-family: monospace;")
        scrub_layout.addWidget(self.lcd_tot_time)

        lcd_layout.addLayout(scrub_layout)
        p_layout.addWidget(lcd_frame, 1)

        # Volumen
        vol_layout = QHBoxLayout()
        vol_layout.setSpacing(6)
        lbl_vdown = QLabel("🔈", self)
        lbl_vdown.setStyleSheet("color: #94A3B8; font-size: 13px;")
        vol_layout.addWidget(lbl_vdown)

        self.vol_slider = QSlider(Qt.Orientation.Horizontal, self)
        self.vol_slider.setRange(0, 100)
        self.vol_slider.setValue(80)
        self.vol_slider.setFixedWidth(80)
        self.vol_slider.setStyleSheet(
            "QSlider::groove:horizontal { height: 4px; background: #334155; border-radius: 2px; } "
            "QSlider::sub-page:horizontal { background: #94A3B8; border-radius: 2px; } "
            "QSlider::handle:horizontal { background: #E2E8F0; width: 10px; height: 10px; margin: -3px 0; border-radius: 5px; }"
        )
        self.vol_slider.valueChanged.connect(self.on_vol_changed)
        vol_layout.addWidget(self.vol_slider)

        lbl_vup = QLabel("🔊", self)
        lbl_vup.setStyleSheet("color: #94A3B8; font-size: 13px;")
        vol_layout.addWidget(lbl_vup)
        p_layout.addLayout(vol_layout)

        root.addWidget(player_frame)

        # Inicializar multimedia si está disponible
        self.media_player = None
        self.audio_output = None
        if HAS_MULTIMEDIA:
            self.media_player = QMediaPlayer(self)
            self.audio_output = QAudioOutput(self)
            self.media_player.setAudioOutput(self.audio_output)
            self.audio_output.setVolume(0.8)
            self.media_player.positionChanged.connect(self.on_player_pos_changed)
            self.media_player.durationChanged.connect(self.on_player_dur_changed)
            self.media_player.mediaStatusChanged.connect(self.on_player_status_changed)
            self.media_player.playbackStateChanged.connect(self.on_player_state_changed)

    def on_table_double_clicked(self, index):
        if not index.isValid(): return
        row = index.row()
        if 0 <= row < len(self.active_tracks):
            self.play_track(self.active_tracks[row])

    def play_track(self, track: Track):
        self.current_playing_track = track
        self.lcd_title.setText(track.title)
        self.lcd_sub.setText(f"{track.artist} — {track.album}")
        self.lcd_cur_time.setText("0:00")
        self.lcd_tot_time.setText(track.formatted_duration())

        phys = resolve_physical_path(self.current_mount, track.file_path)
        if not os.path.exists(phys):
            return

        if self.media_player:
            self.media_player.setSource(QUrl.fromLocalFile(phys))
            self.media_player.play()
        else:
            self.statusBar().showMessage(f"Reproduciendo: {track.title} ({track.artist})")

    def toggle_playback(self):
        if self.media_player:
            if self.media_player.playbackState() == QMediaPlayer.PlaybackState.PlayingState:
                self.media_player.pause()
            elif self.media_player.playbackState() == QMediaPlayer.PlaybackState.PausedState:
                self.media_player.play()
            else:
                self.play_next_track()
        else:
            self.play_next_track()

    def play_next_track(self):
        if not self.active_tracks: return
        cur_id = self.current_playing_track.id if self.current_playing_track else -1
        idx = -1
        for i, t in enumerate(self.active_tracks):
            if t.id == cur_id:
                idx = i
                break
        next_idx = (idx + 1) % len(self.active_tracks)
        self.play_track(self.active_tracks[next_idx])
        self.table.selectRow(next_idx)

    def play_prev_track(self):
        if not self.active_tracks: return
        cur_id = self.current_playing_track.id if self.current_playing_track else -1
        idx = -1
        for i, t in enumerate(self.active_tracks):
            if t.id == cur_id:
                idx = i
                break
        prev_idx = (idx - 1 + len(self.active_tracks)) % len(self.active_tracks)
        self.play_track(self.active_tracks[prev_idx])
        self.table.selectRow(prev_idx)

    def on_player_pos_changed(self, pos):
        if not self.is_slider_seeking and self.current_playing_track and self.current_playing_track.duration_ms > 0:
            tot = self.current_playing_track.duration_ms
            self.lcd_cur_time.setText(self.fmt_ms(pos))
            val = int((pos * 1000) / tot)
            self.lcd_slider.blockSignals(True)
            self.lcd_slider.setValue(val)
            self.lcd_slider.blockSignals(False)

    def on_player_dur_changed(self, dur):
        if dur > 0:
            self.lcd_tot_time.setText(self.fmt_ms(dur))

    def on_player_status_changed(self, status):
        if hasattr(QMediaPlayer.MediaStatus, 'EndOfMedia') and status == QMediaPlayer.MediaStatus.EndOfMedia:
            self.play_next_track()

    def on_player_state_changed(self, state):
        if hasattr(QMediaPlayer.PlaybackState, 'PlayingState') and state == QMediaPlayer.PlaybackState.PlayingState:
            self.btn_play.setText("⏸")
        else:
            self.btn_play.setText("▶")

    def on_slider_pressed(self):
        self.is_slider_seeking = True

    def on_slider_released(self):
        self.is_slider_seeking = False
        if self.media_player and self.current_playing_track and self.current_playing_track.duration_ms > 0:
            pos = int((self.lcd_slider.value() * self.current_playing_track.duration_ms) / 1000)
            self.media_player.setPosition(pos)

    def on_slider_moved(self, val):
        if self.current_playing_track and self.current_playing_track.duration_ms > 0:
            pos = int((val * self.current_playing_track.duration_ms) / 1000)
            self.lcd_cur_time.setText(self.fmt_ms(pos))

    def on_vol_changed(self, val):
        if self.audio_output:
            self.audio_output.setVolume(val / 100.0)

    @staticmethod
    def fmt_ms(ms):
        secs = max(0, int(ms // 1000))
        m = secs // 60
        s = secs % 60
        return f"{m}:{s:02d}"

    def dragEnterEvent(self, event):
        if event.mimeData().hasUrls():
            event.acceptProposedAction()

    def dragMoveEvent(self, event):
        if event.mimeData().hasUrls():
            event.acceptProposedAction()

    def dropEvent(self, event):
        paths = []
        for url in event.mimeData().urls():
            if url.isLocalFile():
                paths.append(url.toLocalFile())
        if paths:
            self.handle_dropped_files(paths)

    def handle_dropped_files(self, paths):
        if not self.current_mount or self.current_mount == "demo" or not os.path.exists(self.current_mount):
            QMessageBox.warning(self, "Sin iPod", "Conecta o selecciona un iPod antes de transferir canciones.")
            return

        supported_exts = {
            "mp3", "m4a", "aac", "wav", "aif", "aiff",
            "flac", "ogg", "opus", "wma", "ape", "wv"
        }

        audio_files = []
        for p in paths:
            if os.path.isdir(p):
                for r, _, files in os.walk(p):
                    for f in files:
                        ext = f.split('.')[-1].lower()
                        if ext in supported_exts:
                            audio_files.append(os.path.join(r, f))
            elif os.path.isfile(p):
                ext = p.split('.')[-1].lower()
                if ext in supported_exts:
                    audio_files.append(p)

        if not audio_files:
            QMessageBox.information(self, "Sin audio", "No se detectaron archivos de audio válidos para transferir.")
            return

        incompat = []
        compat = []
        for f in audio_files:
            ext = f.split('.')[-1].lower()
            if ext in ("flac", "ogg", "opus", "wma", "ape", "wv"):
                incompat.append(f)
            else:
                compat.append(f)

        to_import = list(compat)

        if incompat:
            items_str = "\n".join([f"• {os.path.basename(x)}" for x in incompat[:6]])
            res = QMessageBox.question(
                self,
                "Compatibilidad de Formatos",
                f"Se detectaron {len(incompat)} canciones en formato incompatible con el iPod:\n\n{items_str}\n\n"
                "¿Deseas convertirlas automáticamente a formato compatible (M4A / AAC) con FFmpeg?",
                QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No
            )
            if res == QMessageBox.StandardButton.Yes:
                ffmpeg_bin = os.path.join(os.getcwd(), "tools", "ffmpeg.exe")
                if not os.path.exists(ffmpeg_bin):
                    ffmpeg_bin = "ffmpeg"

                temp_dir = os.path.join(os.environ.get("TEMP", "."), "iPop_Converted")
                os.makedirs(temp_dir, exist_ok=True)

                for src in incompat:
                    base = os.path.splitext(os.path.basename(src))[0]
                    dest = os.path.join(temp_dir, f"{base}.m4a")
                    cmd = [ffmpeg_bin, "-y", "-i", src, "-vn", "-c:a", "aac", "-b:a", "256k", "-ar", "44100", dest]
                    try:
                        r = subprocess.run(cmd, capture_output=True, timeout=60)
                        if r.returncode == 0 and os.path.exists(dest):
                            to_import.append(dest)
                    except Exception as ex:
                        print("Error convirtiendo:", ex)

        if not to_import:
            return

        QMessageBox.information(
            self,
            "Transferencia",
            f"Se prepararon {len(to_import)} canciones para transferir al iPod.\n"
            "Para una sincronización física completa y binaria con recálculo de hash58, utiliza la versión nativa compilada (ipop.exe)."
        )

    def refresh_devices(self):
        self.device_combo.blockSignals(True)
        self.device_combo.clear()

        found = []
        for letter in string.ascii_uppercase:
            drive = f"{letter}:\\"
            if os.path.exists(drive):
                ctrl = os.path.join(drive, "iPod_Control")
                if os.path.exists(ctrl) and os.path.isdir(ctrl):
                    found.append((f"iPod ({drive})", drive))

        if not found:
            self.device_combo.addItem("Ningún iPod detectado (modo demostración)", "demo")
        else:
            for label, p in found:
                self.device_combo.addItem(label, p)

        self.device_combo.blockSignals(False)
        self.on_device_changed(0)

    def browse_mount(self):
        d = QFileDialog.getExistingDirectory(self, "Seleccionar carpeta del iPod")
        if d:
            self.device_combo.addItem(f"Manual: {d}", d)
            self.device_combo.setCurrentIndex(self.device_combo.count() - 1)

    def on_device_changed(self, idx):
        if idx < 0: return
        data = self.device_combo.itemData(idx)
        self.current_mount = data
        if data == "demo":
            self.load_demo_data()
        else:
            self.load_real_device(data)

    def load_demo_data(self):
        self.model_lbl.setText("iPod Classic (6G / 7G 160GB)")
        self.sn_lbl.setText("SN: 000A27001B3646C4")
        self.batt_lbl.setText("100% (Cargando)")
        tot = 160 * 1024 * 1024 * 1024
        audio = 42 * 1024 * 1024 * 1024
        free = tot - audio
        self.tot_lbl.setText("Total: 148.9 GB")
        self.free_lbl.setText(f"Libre: {free / (1024**3):.1f} GB")
        self.cap_bar.set_storage(tot, audio, 0, 0, 0, 0, free)

    def load_real_device(self, mount_path: str):
        # 1. Leer SysInfo de hardware
        sysinfo_p = os.path.join(mount_path, "iPod_Control", "Device", "SysInfo")
        model = "iPod"
        sn = ""
        if os.path.exists(sysinfo_p):
            with open(sysinfo_p, "r", errors="ignore") as f:
                for line in f:
                    if line.startswith("ModelNumStr:"):
                        model_num = line.split(":", 1)[1].strip()
                        model = f"iPod Nano ({model_num})" if "A978" in model_num or "B249" in model_num else f"iPod ({model_num})"
                    elif line.startswith("FirewireGuid:"):
                        sn = line.split(":", 1)[1].strip()

        self.model_lbl.setText(model)
        self.sn_lbl.setText(f"SN: {sn}" if sn else "")
        self.batt_lbl.setText("100% (Cargando)")

        # 2. Base de datos iTunesDB
        itunesdb_p = os.path.join(mount_path, "iPod_Control", "iTunes", "iTunesDB")
        self.all_tracks, self.playlists = parse_itunesdb(itunesdb_p)

        # 3. Almacenamiento real
        st = QStorageInfo(mount_path)
        tot = st.bytesTotal()
        free = st.bytesAvailable()
        audio_bytes = sum(t.size_bytes for t in self.all_tracks.values())
        used = max(0, tot - free)
        other_bytes = max(0, used - audio_bytes)

        self.tot_lbl.setText(f"Total: {tot / (1024**3):.2f} GB")
        self.free_lbl.setText(f"Libre: {free / (1024**3):.2f} GB")
        self.cap_bar.set_storage(tot, audio_bytes, 0, 0, 0, other_bytes, free)

        # 4. Construir el Árbol Organizador
        self.populate_tree(model, sn)

    def populate_tree(self, device_name: str, sn: str):
        self.tree.clear()

        root_item = QTreeWidgetItem(self.tree)
        root_item.setText(0, f"📱 {device_name}")
        root_font = QFont()
        root_font.setBold(True)
        root_item.setFont(0, root_font)

        dev_branch = QTreeWidgetItem(root_item)
        dev_branch.setText(0, "📂 En mi dispositivo")

        master_pl = next((p for p in self.playlists if p.is_master), None)
        all_count = len(self.all_tracks)
        item_music = QTreeWidgetItem(dev_branch)
        item_music.setText(0, f"🎵 Música ({all_count})")
        item_music.setData(0, Qt.ItemDataRole.UserRole, ("playlist", master_pl or Playlist(1, "Música", is_master=True, tracks=list(self.all_tracks.values()))))

        for name, icon in [("Películas", "🎬"), ("Programas de TV", "📺"), ("Audiolibros", "📖"), ("Podcasts", "🎙️")]:
            it = QTreeWidgetItem(dev_branch)
            it.setText(0, f"{icon} {name} (0)")
            it.setData(0, Qt.ItemDataRole.UserRole, ("empty", name))

        user_pls = [p for p in self.playlists if not p.is_master]
        pl_branch = QTreeWidgetItem(root_item)
        pl_branch.setText(0, f"📑 Listas de reproducción ({len(user_pls)})")

        self.tree.blockSignals(True)
        for pl in user_pls:
            pl_item = QTreeWidgetItem(pl_branch)
            pl_item.setText(0, f"🎶 {pl.name} ({len(pl.tracks)})")
            pl_item.setData(0, Qt.ItemDataRole.UserRole, ("playlist", pl))
            pl_item.setFlags(pl_item.flags() | Qt.ItemFlag.ItemIsUserCheckable)
            pl_item.setCheckState(0, Qt.CheckState.Unchecked)

        root_item.setExpanded(True)
        dev_branch.setExpanded(True)
        pl_branch.setExpanded(True)
        self.tree.blockSignals(False)

        if user_pls:
            target_item = None
            for i in range(pl_branch.childCount()):
                child = pl_branch.child(i)
                if "REGGAETON VIEJO" in child.text(0):
                    target_item = child
                    break
            if not target_item:
                target_item = pl_branch.child(0)
            self.tree.setCurrentItem(target_item)
            self.on_tree_item_clicked(target_item, 0)
        else:
            self.tree.setCurrentItem(item_music)
            self.on_tree_item_clicked(item_music, 0)
        self.update_playlist_action_buttons()

    def on_tree_item_clicked(self, item: QTreeWidgetItem, column: int):
        data = item.data(0, Qt.ItemDataRole.UserRole)
        if not data: return

        kind, payload = data
        if kind == "playlist" and payload is not None:
            pl: Playlist = payload
            self.current_playlist_ref = pl
            self.view_title_lbl.setText(pl.name)
            self.view_sum_lbl.setText(pl.formatted_summary())
            self.active_tracks = list(pl.tracks)
            self.model.set_tracks(self.active_tracks)
            self.all_selected = False
            self.sel_all_btn.setText("Seleccionar todas")
        elif kind == "empty":
            self.current_playlist_ref = None
            self.view_title_lbl.setText(str(payload))
            self.view_sum_lbl.setText("0 elementos")
            self.active_tracks = []
            self.model.set_tracks([])
        self.update_playlist_action_buttons()

    def get_checked_playlist_indices(self) -> List[int]:
        res = []
        root = self.tree.invisibleRootItem()
        def search(item):
            d = item.data(0, Qt.ItemDataRole.UserRole)
            if d and d[0] == "playlist" and d[1] and not d[1].is_master:
                if item.checkState(0) == Qt.CheckState.Checked:
                    try:
                        idx = self.playlists.index(d[1])
                        res.append(idx)
                    except ValueError:
                        pass
            for i in range(item.childCount()):
                search(item.child(i))
        search(root)
        return res

    def update_playlist_action_buttons(self):
        checked = self.get_checked_playlist_indices()
        count = len(checked)

        cur_item = self.tree.currentItem()
        cur_data = cur_item.data(0, Qt.ItemDataRole.UserRole) if cur_item else None
        has_single_focused_user_pl = False
        if count == 0 and cur_data and cur_data[0] == "playlist" and cur_data[1] and not cur_data[1].is_master:
            has_single_focused_user_pl = True

        user_pls = [p for p in self.playlists if not p.is_master]
        total_user_pls = len(user_pls)

        if hasattr(self, 'btn_select_all_pl'):
            all_checked = (total_user_pls > 0 and count == total_user_pls)
            self.btn_select_all_pl.setText("☑️ Deselec." if all_checked else "☑️ Todas")
            self.btn_select_all_pl.setEnabled(total_user_pls > 0)

        can_edit = (count == 1) or has_single_focused_user_pl
        if hasattr(self, 'btn_edit_pl'):
            self.btn_edit_pl.setEnabled(can_edit)
            if can_edit:
                self.btn_edit_pl.setStyleSheet(
                    "QPushButton { background-color: #2563EB; color: white; border-radius: 6px; padding: 5px 8px; font-size: 11px; font-weight: bold; } "
                    "QPushButton:hover { background-color: #1D4ED8; }"
                )
            else:
                self.btn_edit_pl.setStyleSheet(
                    "QPushButton { background-color: #1E293B; color: #475569; border: 1px solid #334155; border-radius: 6px; padding: 5px 8px; font-size: 11px; font-weight: 500; } "
                    "QPushButton:disabled { background-color: #1E293B; color: #475569; border: 1px solid #334155; }"
                )

        can_delete = (count >= 1) or has_single_focused_user_pl
        if hasattr(self, 'btn_delete_pl'):
            self.btn_delete_pl.setEnabled(can_delete)
            if can_delete:
                self.btn_delete_pl.setText(f"🗑️ Eliminar ({count})" if count > 1 else "🗑️ Eliminar")
                self.btn_delete_pl.setStyleSheet(
                    "QPushButton { background-color: #DC2626; color: white; border-radius: 6px; padding: 5px 8px; font-size: 11px; font-weight: bold; } "
                    "QPushButton:hover { background-color: #B91C1C; }"
                )
            else:
                self.btn_delete_pl.setText("🗑️ Eliminar")
                self.btn_delete_pl.setStyleSheet(
                    "QPushButton { background-color: #1E293B; color: #475569; border: 1px solid #334155; border-radius: 6px; padding: 5px 8px; font-size: 11px; font-weight: 500; } "
                    "QPushButton:disabled { background-color: #1E293B; color: #475569; border: 1px solid #334155; }"
                )

    def on_tree_item_changed(self, item: QTreeWidgetItem, column: int):
        self.update_playlist_action_buttons()

    def on_tree_context_menu(self, pos):
        if not self.current_mount or self.current_mount == "demo" or not os.path.exists(self.current_mount):
            return

        item = self.tree.itemAt(pos)
        menu = QMenu(self)
        menu.setStyleSheet(
            "QMenu { background-color: #1E293B; color: #F8FAFC; border: 1px solid #334155; padding: 4px; border-radius: 6px; font-size: 12px; } "
            "QMenu::item { padding: 6px 20px 6px 12px; border-radius: 4px; } "
            "QMenu::item:selected { background-color: #2563EB; color: white; } "
            "QMenu::item:disabled { color: #64748B; }"
        )

        menu.addAction("➕ Nueva lista de reproducción...", self.on_new_playlist)

        data = item.data(0, Qt.ItemDataRole.UserRole) if item else None
        if data and data[0] == "playlist" and data[1] and not data[1].is_master:
            pl = data[1]
            try:
                idx = self.playlists.index(pl)
            except ValueError:
                idx = -1

            if idx > 0:
                menu.addSeparator()
                menu.addAction("✏️ Renombrar lista...", lambda: self.rename_playlist_by_index(idx))

                checked = self.get_checked_playlist_indices()
                if len(checked) > 1 and idx in checked:
                    menu.addAction(f"🗑️ Eliminar {len(checked)} listas seleccionadas...", lambda: self.delete_playlists_by_indices(checked))
                else:
                    menu.addAction("🗑️ Eliminar lista...", lambda: self.delete_playlists_by_indices([idx]))

        menu.exec(self.tree.viewport().mapToGlobal(pos))

    def on_toggle_select_all_playlists(self):
        checked = self.get_checked_playlist_indices()
        user_pls = [p for p in self.playlists if not p.is_master]
        select_all = (len(checked) < len(user_pls))

        self.tree.blockSignals(True)
        root = self.tree.invisibleRootItem()
        def set_checks(item):
            d = item.data(0, Qt.ItemDataRole.UserRole)
            if d and d[0] == "playlist" and d[1] and not d[1].is_master:
                item.setCheckState(0, Qt.CheckState.Checked if select_all else Qt.CheckState.Unchecked)
            for i in range(item.childCount()):
                set_checks(item.child(i))
        set_checks(root)
        self.tree.blockSignals(False)

        self.update_playlist_action_buttons()

    def on_edit_selected_playlist(self):
        checked = self.get_checked_playlist_indices()
        target_idx = -1
        if len(checked) == 1:
            target_idx = checked[0]
        elif len(checked) == 0:
            cur = self.tree.currentItem()
            d = cur.data(0, Qt.ItemDataRole.UserRole) if cur else None
            if d and d[0] == "playlist" and d[1] and not d[1].is_master:
                try:
                    target_idx = self.playlists.index(d[1])
                except ValueError:
                    pass

        if target_idx > 0:
            self.rename_playlist_by_index(target_idx)

    def rename_playlist_by_index(self, idx: int):
        if not self.current_mount or idx <= 0 or idx >= len(self.playlists):
            return

        pl = self.playlists[idx]
        new_name, ok = QInputDialog.getText(
            self,
            "Renombrar lista de reproducción",
            f"Nuevo nombre para '{pl.name}':",
            QLineEdit.EchoMode.Normal,
            pl.name
        )

        if not ok:
            return
        new_name = new_name.strip()
        if not new_name or new_name == pl.name:
            return

        itunesdb_p = os.path.join(self.current_mount, "iPod_Control", "iTunes", "iTunesDB")
        if not os.path.exists(itunesdb_p):
            itunesdb_p = os.path.join(self.current_mount, "ipod_control", "itunes", "itunesdb")

        if os.path.exists(itunesdb_p):
            success = rename_playlist_in_itunesdb(itunesdb_p, idx, new_name)
            if success:
                self.load_real_device(self.current_mount)
                self.select_playlist_by_id(pl.id)
                self.statusBar().showMessage(f"Lista renombrada a '{new_name}'", 3000)
            else:
                QMessageBox.warning(self, "Error", "No se pudo renombrar la lista en la base de datos.")

    def on_delete_selected_playlists(self):
        checked = self.get_checked_playlist_indices()
        if not checked:
            cur = self.tree.currentItem()
            d = cur.data(0, Qt.ItemDataRole.UserRole) if cur else None
            if d and d[0] == "playlist" and d[1] and not d[1].is_master:
                try:
                    checked.append(self.playlists.index(d[1]))
                except ValueError:
                    pass

        if checked:
            self.delete_playlists_by_indices(checked)

    def delete_playlists_by_indices(self, indices: List[int]):
        valid_indices = [i for i in indices if i > 0 and i < len(self.playlists)]
        if not valid_indices or not self.current_mount:
            return

        if len(valid_indices) == 1:
            name = self.playlists[valid_indices[0]].name
            confirm_text = f"¿Estás seguro de que deseas eliminar la lista de reproducción \"{name}\" del iPod?\n\n(Las canciones permanecerán intactas en el iPod)."
        else:
            confirm_text = f"¿Estás seguro de que deseas eliminar las {len(valid_indices)} listas de reproducción seleccionadas del iPod?\n\n(Las canciones permanecerán intactas en el iPod)."

        reply = QMessageBox.question(
            self,
            "Eliminar listas de reproducción",
            confirm_text,
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
            QMessageBox.StandardButton.No
        )
        if reply != QMessageBox.StandardButton.Yes:
            return

        itunesdb_p = os.path.join(self.current_mount, "iPod_Control", "iTunes", "iTunesDB")
        if not os.path.exists(itunesdb_p):
            itunesdb_p = os.path.join(self.current_mount, "ipod_control", "itunes", "itunesdb")

        if os.path.exists(itunesdb_p):
            success = delete_playlists_from_itunesdb(itunesdb_p, set(valid_indices))
            if success:
                self.load_real_device(self.current_mount)
                master_pl = next((p for p in self.playlists if p.is_master), None)
                if master_pl:
                    self.select_playlist_by_id(master_pl.id)
                self.statusBar().showMessage(f"{len(valid_indices)} lista(s) de reproducción eliminada(s).", 3000)
            else:
                QMessageBox.critical(self, "Error", "No se pudieron eliminar las listas de reproducción de la base de datos.")

    def on_new_playlist(self):
        if not self.current_mount or self.current_mount == "demo" or not os.path.exists(self.current_mount):
            QMessageBox.information(self, "Aviso", "Conecta o selecciona un iPod primero.")
            return

        name, ok = QInputDialog.getText(
            self,
            "Nueva lista de reproducción",
            "Nombre de la nueva lista:",
            QLineEdit.EchoMode.Normal,
            "Nueva lista"
        )
        if not ok:
            return
        name = name.strip()
        if not name:
            return

        itunesdb_p = os.path.join(self.current_mount, "iPod_Control", "iTunes", "iTunesDB")
        if not os.path.exists(itunesdb_p):
            itunesdb_p = os.path.join(self.current_mount, "ipod_control", "itunes", "itunesdb")

        if os.path.exists(itunesdb_p):
            success = create_playlist_in_itunesdb(itunesdb_p, name)
            if success:
                self.load_real_device(self.current_mount)
                for pl in reversed(self.playlists):
                    if pl.name == name:
                        self.select_playlist_by_id(pl.id)
                        break
                self.statusBar().showMessage(f"Lista \"{name}\" creada con éxito.", 3000)
            else:
                QMessageBox.warning(self, "Error", "No se pudo crear la nueva lista en la base de datos.")

    def toggle_select_all(self):
        self.all_selected = not self.all_selected
        self.model.select_all(self.all_selected)
        self.sel_all_btn.setText("Deseleccionar todas" if self.all_selected else "Seleccionar todas")

    def filter_tracks(self, query: str):
        if not self.current_playlist_ref: return
        q = query.strip().lower()
        if not q:
            self.model.set_tracks(self.active_tracks)
        else:
            filtered = [
                t for t in self.active_tracks
                if q in t.title.lower() or q in t.artist.lower() or q in t.album.lower()
            ]
            self.model.set_tracks(filtered)

    def select_playlist_by_id(self, pl_id: int):
        root = self.tree.invisibleRootItem()
        def search(item):
            d = item.data(0, Qt.ItemDataRole.UserRole)
            if d and d[0] == "playlist" and d[1] and d[1].id == pl_id:
                self.tree.setCurrentItem(item)
                self.on_tree_item_clicked(item, 0)
                return True
            for i in range(item.childCount()):
                if search(item.child(i)):
                    return True
            return False
        search(root)

    def delete_selected(self):
        ids = self.model.selected_ids()
        if not ids:
            QMessageBox.information(self, "Aviso", "No has seleccionado ninguna canción.")
            return

        # Detectar conflicto con iTunes
        if is_itunes_running():
            itunes_reply = QMessageBox.warning(
                self,
                "Conflicto con iTunes detectado",
                "⚠️ iTunes está abierto en segundo plano.\n\n"
                "iTunes mantiene la base de datos del iPod en su memoria RAM y NO detecta cambios externos en tiempo real.\n"
                "Si no se cierra iTunes antes de borrar, continuará mostrando las canciones y podría volver a escribir el estado anterior en el iPod al cerrarse.\n\n"
                "¿Deseas que iPop cierre iTunes automáticamente ahora?",
                QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No | QMessageBox.StandardButton.Cancel,
                QMessageBox.StandardButton.Yes
            )
            if itunes_reply == QMessageBox.StandardButton.Cancel:
                return
            if itunes_reply == QMessageBox.StandardButton.Yes:
                kill_itunes_process()
                self.check_itunes_status()

        pl = self.current_playlist_ref
        pl_name = pl.name if pl else "Biblioteca"
        reply = QMessageBox.question(
            self, "Confirmar eliminación física",
            f"¿Deseas ELIMINAR {len(ids)} canciones permanentemente del iPod?\n\n"
            "Esto borrará los archivos de audio del disco, actualizará la base de datos iTunesDB y vaciará los búferes del almacenamiento.",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No
        )
        if reply != QMessageBox.StandardButton.Yes:
            return

        deleted_files = 0
        id_set = set(ids)
        saved_pl_id = self.current_playlist_ref.id if self.current_playlist_ref else None

        total_bytes = sum(self.all_tracks[uid].size_bytes for uid in ids if uid in self.all_tracks)
        tot_mb = total_bytes / (1024.0 * 1024.0)

        # Diálogo de progreso elegante con barra completando desde 0.0 MB a X.X MB
        dlg = QDialog(self)
        dlg.setWindowTitle("Eliminando canciones del iPod")
        dlg.setWindowFlags(Qt.WindowType.Dialog | Qt.WindowType.CustomizeWindowHint | Qt.WindowType.WindowTitleHint)
        dlg.setFixedSize(500, 200)
        dlg.setStyleSheet("background-color: #1E293B; border: 1px solid #334155; border-radius: 12px;")

        d_layout = QVBoxLayout(dlg)
        d_layout.setContentsMargins(22, 18, 22, 18)
        d_layout.setSpacing(12)

        header_layout = QHBoxLayout()
        header_layout.setSpacing(10)
        icon_lbl = QLabel("🗑️", dlg)
        icon_lbl.setStyleSheet("font-size: 22px;")
        header_layout.addWidget(icon_lbl)

        title_lbl = QLabel("Eliminando del iPod...", dlg)
        title_lbl.setStyleSheet("color: #F8FAFC; font-size: 16px; font-weight: bold;")
        header_layout.addWidget(title_lbl, 1)

        badge_lbl = QLabel(f"0.0 MB a {tot_mb:.1f} MB", dlg)
        badge_lbl.setStyleSheet(
            "background-color: #0F172A; color: #38BDF8; font-size: 12px; font-weight: bold; "
            "padding: 4px 10px; border-radius: 6px; border: 1px solid #334155;"
        )
        header_layout.addWidget(badge_lbl)
        d_layout.addLayout(header_layout)

        status_lbl = QLabel(f"Iniciando eliminación de {len(ids)} canciones...", dlg)
        status_lbl.setStyleSheet("color: #94A3B8; font-size: 13px;")
        status_lbl.setWordWrap(True)
        d_layout.addWidget(status_lbl)

        pbar = QProgressBar(dlg)
        pbar.setRange(0, 1000)
        pbar.setValue(0)
        pbar.setFixedHeight(22)
        pbar.setTextVisible(False)
        pbar.setStyleSheet(
            "QProgressBar { background-color: #0F172A; border: 1px solid #334155; border-radius: 6px; } "
            "QProgressBar::chunk { background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #2563EB, stop:1 #38BDF8); border-radius: 5px; }"
        )
        d_layout.addWidget(pbar)

        info_layout = QHBoxLayout()
        mb_lbl = QLabel(f"Progreso: 0.0 MB / {tot_mb:.1f} MB (0%)", dlg)
        mb_lbl.setStyleSheet("color: #F8FAFC; font-size: 12px; font-weight: 600;")
        info_layout.addWidget(mb_lbl)
        info_layout.addStretch()
        d_layout.addLayout(info_layout)

        dlg.show()
        QApplication.processEvents()

        current_bytes = 0
        track_counter = 0

        # 1. Borrar archivos físicos del almacenamiento masivo del iPod
        for uid in ids:
            track_counter += 1
            t = self.all_tracks.get(uid)
            track_title = t.title if t else f"ID {uid}"
            status_lbl.setText(f"Borrando ({track_counter}/{len(ids)}): {track_title}")
            QApplication.processEvents()

            if t and t.file_path:
                clean = t.file_path.lstrip(':')
                parts = clean.split(':')
                real_path = os.path.join(self.current_mount, *parts)
                if os.path.exists(real_path):
                    try:
                        os.remove(real_path)
                        deleted_files += 1
                    except Exception as e:
                        print(f"Error borrando {real_path}: {e}")

            if t:
                current_bytes += t.size_bytes

            curr_mb = current_bytes / (1024.0 * 1024.0)
            frac = (current_bytes / total_bytes) if total_bytes > 0 else 1.0
            frac = min(1.0, max(0.0, frac))
            pbar.setValue(int(frac * 1000))
            pct = int(frac * 100)
            mb_lbl.setText(f"Progreso: {curr_mb:.1f} MB / {tot_mb:.1f} MB ({pct}%)")
            QApplication.processEvents()
            time.sleep(0.035)

        # 2. Modificar, firmar con hash58 y guardar físicamente el archivo iTunesDB en el iPod
        status_lbl.setText("Actualizando base de datos iTunesDB y firma hash58...")
        QApplication.processEvents()

        itunesdb_p = os.path.join(self.current_mount, "iPod_Control", "iTunes", "iTunesDB")
        if os.path.exists(itunesdb_p):
            remove_tracks_from_itunesdb(itunesdb_p, id_set)

        status_lbl.setText("Vaciando búferes a la memoria flash del iPod...")
        QApplication.processEvents()
        pbar.setValue(1000)
        mb_lbl.setText(f"Progreso: {tot_mb:.1f} MB / {tot_mb:.1f} MB (100%)")
        title_lbl.setText("¡Completado!")
        status_lbl.setText("¡Canciones eliminadas físicamente del disco con éxito!")
        QApplication.processEvents()
        time.sleep(0.4)
        dlg.close()

        # 3. Recargar el iPod y restaurar la vista seleccionada
        self.load_real_device(self.current_mount)
        if saved_pl_id is not None:
            self.select_playlist_by_id(saved_pl_id)

        QMessageBox.information(
            self, "Eliminación completada",
            f"Se eliminaron {len(ids)} canciones físicamente ({deleted_files} archivos eliminados del disco) "
            "y se sincronizó la base de datos iTunesDB en el iPod."
        )

    def eject_device(self):
        mount = getattr(self, "current_mount", None)
        if not mount or mount == "demo" or not os.path.exists(mount):
            QMessageBox.information(self, "Expulsar", "No hay ningún iPod físico conectado para expulsar.")
            return

        reply = QMessageBox.question(
            self,
            "Expulsar iPod",
            f"¿Deseas expulsar con seguridad el iPod en '{mount}'?\n\n"
            "La base de datos se sincronizará y el iPod cambiará su pantalla a 'Aceptable desconectar' (OK to disconnect) o al menú principal de música.",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No
        )
        if reply != QMessageBox.StandardButton.Yes:
            return

        self.statusBar().showMessage("Sincronizando y expulsando dispositivo...")
        QApplication.processEvents()

        ok, err_msg = self.perform_eject(mount)
        if ok:
            QMessageBox.information(
                self,
                "iPod Expulsado",
                "El iPod ha sido expulsado con seguridad.\n\n"
                "En la pantalla del iPod aparecerá 'Aceptable desconectar' (OK to disconnect) o el menú principal de música. Ya puedes retirar el cable con total seguridad."
            )
            self.refresh_devices()
        else:
            QMessageBox.warning(
                self,
                "Error al expulsar",
                f"No se pudo expulsar automáticamente el iPod:\n{err_msg}\n\n"
                "Asegúrate de que iTunes no esté en ejecución ni haya carpetas abiertas en el Explorador de archivos."
            )

    def perform_eject(self, mount_path: str):
        import platform
        if platform.system() != "Windows":
            import subprocess
            try:
                subprocess.run(["sync"], check=False)
                res = subprocess.run(["eject", mount_path], check=False)
                if res.returncode == 0:
                    return True, ""
                res2 = subprocess.run(["umount", mount_path], check=False)
                return res2.returncode == 0, ""
            except Exception as e:
                return False, str(e)

        import ctypes
        from ctypes import wintypes
        import time

        clean_path = mount_path.strip()
        letter = clean_path[0].upper()
        vol_path = f"\\\\.\\{letter}:"

        kernel32 = ctypes.WinDLL('kernel32', use_last_error=True)
        GENERIC_READ = 0x80000000
        GENERIC_WRITE = 0x40000000
        FILE_SHARE_READ = 1
        FILE_SHARE_WRITE = 2
        OPEN_EXISTING = 3

        FSCTL_LOCK_VOLUME = 0x00090018
        FSCTL_DISMOUNT_VOLUME = 0x00090020
        IOCTL_STORAGE_MEDIA_REMOVAL = 0x002D4804
        IOCTL_STORAGE_EJECT_MEDIA = 0x002D4808

        h = kernel32.CreateFileW(
            vol_path,
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            None,
            OPEN_EXISTING,
            0,
            None
        )
        if h == -1:
            h = kernel32.CreateFileW(
                vol_path,
                GENERIC_READ,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                None,
                OPEN_EXISTING,
                0,
                None
            )

        if h == -1:
            err = ctypes.get_last_error()
            return False, f"No se pudo obtener acceso exclusivo al volumen {letter}: (Código de error {err})"

        kernel32.FlushFileBuffers(h)

        dw = wintypes.DWORD(0)
        for _ in range(5):
            if kernel32.DeviceIoControl(h, FSCTL_LOCK_VOLUME, None, 0, None, 0, ctypes.byref(dw), None):
                break
            time.sleep(0.2)

        kernel32.DeviceIoControl(h, FSCTL_DISMOUNT_VOLUME, None, 0, None, 0, ctypes.byref(dw), None)

        class PREVENT_MEDIA_REMOVAL(ctypes.Structure):
            _fields_ = [('PreventMediaRemoval', ctypes.c_bool)]
        pmr = PREVENT_MEDIA_REMOVAL(False)
        kernel32.DeviceIoControl(h, IOCTL_STORAGE_MEDIA_REMOVAL, ctypes.byref(pmr), ctypes.sizeof(pmr), None, 0, ctypes.byref(dw), None)

        # Enviar comando de expulsión SCSI (START STOP UNIT LoEj=1)
        kernel32.DeviceIoControl(h, IOCTL_STORAGE_EJECT_MEDIA, None, 0, None, 0, ctypes.byref(dw), None)
        kernel32.CloseHandle(h)

        # Notificar también al Shell de Windows
        import subprocess
        ps_cmd = (
            f"$sh = New-Object -ComObject Shell.Application; "
            f"$item = $sh.Namespace(17).ParseName('{letter}:'); "
            f"if ($item) {{ foreach ($v in $item.Verbs()) {{ if ($v.Name.Replace('&','') -match '(?i)expulsar|eject') {{ $v.DoIt(); break }} }} }}"
        )
        try:
            subprocess.run(["powershell", "-NoProfile", "-NonInteractive", "-Command", ps_cmd], capture_output=True, timeout=3)
        except Exception:
            pass

        return True, ""


def main():
    app = QApplication(sys.argv)
    palette = QPalette()
    palette.setColor(QPalette.ColorRole.Window, QColor(15, 23, 42))
    palette.setColor(QPalette.ColorRole.WindowText, QColor(241, 245, 249))
    palette.setColor(QPalette.ColorRole.Base, QColor(30, 41, 59))
    palette.setColor(QPalette.ColorRole.Text, QColor(241, 245, 249))
    palette.setColor(QPalette.ColorRole.Button, QColor(30, 41, 59))
    palette.setColor(QPalette.ColorRole.ButtonText, QColor(241, 245, 249))
    palette.setColor(QPalette.ColorRole.Highlight, QColor(37, 99, 235))
    palette.setColor(QPalette.ColorRole.HighlightedText, Qt.GlobalColor.white)
    app.setPalette(palette)

    win = MainWindow()
    win.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
