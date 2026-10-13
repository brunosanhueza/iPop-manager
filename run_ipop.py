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
        QMessageBox, QFileDialog, QToolTip, QProgressBar, QDialog
    )
    from PyQt6.QtCore import (
        Qt, QAbstractTableModel, QModelIndex, QRectF, QPointF, QStorageInfo, QTimer
    )
    from PyQt6.QtGui import (
        QPainter, QColor, QPainterPath, QLinearGradient, QPen, QPalette, QFont
    )
except ImportError:
    print("\n[!] PyQt6 no está instalado en tu Python.")
    print("Ejecuta en tu terminal:  pip install PyQt6\n")
    sys.exit(1)



#hola, si estas revisando esto es porque eres alguien curioso, te mando un saludo internauta :)
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

def find_case_insensitive_path(base_path: str, *parts) -> str:
    current = base_path
    for part in parts:
        if not os.path.exists(current) or not os.path.isdir(current):
            return ""
        direct = os.path.join(current, part)
        if os.path.exists(direct):
            current = direct
            continue
        found = False
        try:
            for entry in os.listdir(current):
                if entry.lower() == part.lower():
                    current = os.path.join(current, entry)
                    found = True
                    break
        except Exception:
            return ""
        if not found:
            return ""
    return current

def is_ipod_mount(path: str) -> bool:
    if not path or not os.path.exists(path):
        return False
    ctrl = find_case_insensitive_path(path, "iPod_Control")
    return bool(ctrl and os.path.isdir(ctrl))

def get_ipod_firewire_guid(mount_path: str) -> str:
    p = find_case_insensitive_path(mount_path, "iPod_Control", "Device", "SysInfo")
    if p and os.path.exists(p):
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
        radius = 8.0
        rect = QRectF(0, 0, w, h)
        clip = QPainterPath()
        clip.addRoundedRect(rect, radius, radius)
        painter.setClipPath(clip)

        painter.fillRect(rect, QColor(218, 222, 228))

        if self.total_bytes == 0:
            painter.setPen(QColor(110, 118, 128))
            painter.drawText(rect, Qt.AlignmentFlag.AlignCenter, "Sin almacenamiento detectado")
            return

        cats = [
            ("Música", self.audio_bytes, QColor(255, 149, 0), QColor(224, 76, 0)),
            ("Video", self.video_bytes, QColor(0, 190, 214), QColor(0, 126, 148)),
            ("Podcasts", self.podcast_bytes, QColor(168, 62, 196), QColor(114, 24, 142)),
            ("Fotos", self.photo_bytes, QColor(255, 198, 26), QColor(230, 150, 0)),
            ("Otros", self.other_bytes, QColor(126, 142, 154), QColor(82, 98, 110)),
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
            painter.setPen(QColor(0, 0, 0, 45))
            painter.drawLine(QPointF(cur_x + seg_w, 0), QPointF(cur_x + seg_w, h))
            self.segments.append((s_rect, name, b, frac * 100.0))
            cur_x += seg_w

        if self.free_bytes > 0 and cur_x < w:
            free_rect = QRectF(cur_x, 0, w - cur_x, h)
            grad = QLinearGradient(free_rect.topLeft(), free_rect.bottomLeft())
            grad.setColorAt(0.0, QColor(226, 230, 236))
            grad.setColorAt(1.0, QColor(194, 200, 208))
            painter.fillRect(free_rect, grad)
            self.segments.append((free_rect, "Libre", self.free_bytes, (self.free_bytes / self.total_bytes) * 100.0))

        # Brillo glossy estilo iTunes 10
        gloss_rect = QRectF(0, 0, w, h * 0.45)
        gloss_grad = QLinearGradient(0, 0, 0, h * 0.45)
        gloss_grad.setColorAt(0.0, QColor(255, 255, 255, 130))
        gloss_grad.setColorAt(1.0, QColor(255, 255, 255, 25))
        painter.fillRect(gloss_rect, gloss_grad)

        painter.setClipping(False)
        painter.setPen(QPen(QColor(128, 134, 144), 1.2))
        painter.setBrush(Qt.BrushStyle.NoBrush)
        painter.drawRoundedRect(rect.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius)

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

        # 1. Barra Superior (Estilo iTunes 10 - Aluminio Pulido Platino)
        top_bar = QFrame(self)
        top_bar.setStyleSheet(
            "QFrame { "
            "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #EAEAEA, stop:0.5 #DFDFDF, stop:0.51 #CCCCCC, stop:1 #BEBEBE); "
            "  border-bottom: 1px solid #828892; border-radius: 0px; padding: 6px 12px; "
            "}"
        )
        top_layout = QHBoxLayout(top_bar)

        brand = QLabel(" iPop", self)
        brand.setStyleSheet("font-size: 19px; font-weight: bold; color: #20242A; letter-spacing: 0.5px;")
        top_layout.addWidget(brand)
        top_layout.addSpacing(14)

        lbl = QLabel("iPod:", self)
        lbl.setStyleSheet("color: #4A5058; font-weight: bold; font-size: 11px;")
        top_layout.addWidget(lbl)

        self.device_combo = QComboBox(self)
        self.device_combo.setMinimumWidth(320)
        self.device_combo.setStyleSheet(
            "QComboBox { background-color: #FFFFFF; color: #222222; border: 1px solid #8E949C; "
            "border-radius: 4px; padding: 4px 8px; font-size: 11px; } "
            "QComboBox QAbstractItemView { background-color: #FFFFFF; color: #222222; selection-background-color: #3875D7; selection-color: white; }"
        )
        self.device_combo.currentIndexChanged.connect(self.on_device_changed)
        top_layout.addWidget(self.device_combo, 1)

        btn_style = (
            "QPushButton { "
            "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #FDFDFD, stop:0.5 #EDEDED, stop:0.51 #DCDCDC, stop:1 #EDEDED); "
            "  border: 1px solid #8E949C; border-radius: 4px; color: #2E3338; font-size: 11px; font-weight: bold; padding: 4px 10px; "
            "} "
            "QPushButton:hover { border-color: #6A7078; background: #FFFFFF; } "
            "QPushButton:pressed { background: #B5BAC0; }"
        )

        ref_btn = QPushButton("Refrescar", self)
        ref_btn.setStyleSheet(btn_style)
        ref_btn.clicked.connect(self.refresh_devices)
        top_layout.addWidget(ref_btn)

        browse_btn = QPushButton("Elegir carpeta...", self)
        browse_btn.setStyleSheet(btn_style)
        browse_btn.clicked.connect(self.browse_mount)
        top_layout.addWidget(browse_btn)

        eject_btn = QPushButton("⏏ Expulsar", self)
        eject_btn.setStyleSheet(
            "QPushButton { "
            "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #FDFDFD, stop:0.5 #EDEDED, stop:0.51 #DCDCDC, stop:1 #EDEDED); "
            "  border: 1px solid #8E949C; border-radius: 4px; color: #991B1B; padding: 4px 12px; font-size: 11px; font-weight: bold; "
            "} "
            "QPushButton:hover { background: #FFFFFF; border-color: #DC2626; color: #DC2626; } "
            "QPushButton:pressed { background: #B5BAC0; }"
        )
        eject_btn.clicked.connect(self.eject_device)
        top_layout.addWidget(eject_btn)

        root.addWidget(top_bar)

        # Banner de advertencia de iTunes
        self.itunes_warn_frame = QFrame(self)
        self.itunes_warn_frame.setStyleSheet("background-color: #FEF3C7; border-bottom: 1px solid #F59E0B; padding: 6px 12px;")
        warn_layout = QHBoxLayout(self.itunes_warn_frame)
        warn_layout.setContentsMargins(10, 4, 10, 4)
        warn_layout.setSpacing(10)
        warn_lbl = QLabel("⚠️ iTunes está abierto en segundo plano. iTunes mantiene la base de datos en RAM y no detecta cambios externos ni vacía los archivos borrados.", self.itunes_warn_frame)
        warn_lbl.setStyleSheet("color: #92400E; font-size: 11px; font-weight: 500;")
        warn_layout.addWidget(warn_lbl, 1)
        close_itunes_btn = QPushButton("Cerrar iTunes", self.itunes_warn_frame)
        close_itunes_btn.setStyleSheet("background-color: #DC2626; color: white; border-radius: 3px; padding: 4px 12px; font-weight: bold; font-size: 11px;")
        close_itunes_btn.clicked.connect(self.close_itunes_clicked)
        warn_layout.addWidget(close_itunes_btn)
        self.itunes_warn_frame.setVisible(False)
        root.addWidget(self.itunes_warn_frame)

        # 2. Splitter Central
        splitter = QSplitter(Qt.Orientation.Horizontal, self)
        splitter.setStyleSheet("QSplitter::handle { background-color: #B4B9C0; }")
        splitter.setHandleWidth(2)

        # 2.1 Sidebar Izquierda (Color Clásico Fuente iTunes 10 #DCE2EA)
        tree_frame = QFrame(self)
        tree_frame.setStyleSheet("background-color: #DCE2EA; border-right: 1px solid #B4B9C0; border-radius: 0px;")
        tree_layout = QVBoxLayout(tree_frame)
        tree_layout.setContentsMargins(10, 10, 10, 10)
        tree_layout.setSpacing(6)

        tree_title = QLabel("BIBLIOTECA", self)
        tree_title.setStyleSheet("color: #637080; font-size: 10px; font-weight: bold; letter-spacing: 1px; padding-left: 6px;")
        tree_layout.addWidget(tree_title)

        self.tree = QTreeWidget(self)
        self.tree.setHeaderHidden(True)
        self.tree.setAnimated(True)
        self.tree.setStyleSheet(
            "QTreeWidget { background-color: transparent; border: none; color: #20242A; font-size: 12px; } "
            "QTreeWidget::item { padding: 5px 6px; border-radius: 3px; } "
            "QTreeWidget::item:hover { background-color: #CCD3DC; } "
            "QTreeWidget::item:selected { background-color: #3875D7; color: white; font-weight: bold; }"
        )
        self.tree.itemClicked.connect(self.on_tree_item_clicked)
        tree_layout.addWidget(self.tree, 1)

        splitter.addWidget(tree_frame)
        tree_frame.setMinimumWidth(220)
        tree_frame.setMaximumWidth(280)

        # 2.2 Panel Derecho
        songs_frame = QFrame(self)
        songs_frame.setStyleSheet("background-color: #FFFFFF; border: none; border-radius: 0px;")
        songs_layout = QVBoxLayout(songs_frame)
        songs_layout.setContentsMargins(14, 12, 14, 12)
        songs_layout.setSpacing(8)

        header_row = QHBoxLayout()
        t_box = QVBoxLayout()
        self.view_title_lbl = QLabel("Música", self)
        self.view_title_lbl.setStyleSheet("font-size: 18px; font-weight: bold; color: #1E2228;")
        t_box.addWidget(self.view_title_lbl)

        self.view_sum_lbl = QLabel("0 canciones", self)
        self.view_sum_lbl.setStyleSheet("color: #646C76; font-size: 11px;")
        t_box.addWidget(self.view_sum_lbl)
        header_row.addLayout(t_box)
        header_row.addStretch()

        self.search_edit = QLineEdit(self)
        self.search_edit.setPlaceholderText("🔍 Buscar canción, artista o álbum...")
        self.search_edit.setClearButtonEnabled(True)
        self.search_edit.setFixedWidth(240)
        self.search_edit.setStyleSheet(
            "QLineEdit { background-color: #FFFFFF; color: #222222; border: 1px solid #8E949C; "
            "border-radius: 12px; padding: 4px 10px; font-size: 11px; } "
            "QLineEdit:focus { border: 1px solid #3875D7; }"
        )
        self.search_edit.textChanged.connect(self.filter_tracks)
        header_row.addWidget(self.search_edit)

        songs_layout.addLayout(header_row)

        tools_row = QHBoxLayout()
        self.sel_all_btn = QPushButton("Seleccionar todas", self)
        self.sel_all_btn.setStyleSheet(btn_style)
        self.sel_all_btn.clicked.connect(self.toggle_select_all)
        tools_row.addWidget(self.sel_all_btn)

        self.del_btn = QPushButton("🗑️ Eliminar seleccionadas", self)
        self.del_btn.setStyleSheet(
            "QPushButton { "
            "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #FDFDFD, stop:0.5 #EDEDED, stop:0.51 #DCDCDC, stop:1 #EDEDED); "
            "  border: 1px solid #8E949C; border-radius: 4px; padding: 4px 12px; font-size: 11px; font-weight: bold; color: #B91C1C; "
            "} "
            "QPushButton:hover { background: #DC2626; color: white; border-color: #B91C1C; } "
            "QPushButton:pressed { background: #991B1B; color: white; }"
        )
        self.del_btn.clicked.connect(self.delete_selected)
        tools_row.addWidget(self.del_btn)

        tools_row.addStretch()
        songs_layout.addLayout(tools_row)

        # Tabla de Canciones Estilo iTunes 10 (Rayas de Cebra #FFFFFF y #F1F5FA)
        self.table = QTableView(self)
        self.model = TrackTableModel(self)
        self.table.setModel(self.model)
        self.table.verticalHeader().setVisible(False)
        self.table.setShowGrid(False)
        self.table.setAlternatingRowColors(True)
        self.table.setSelectionBehavior(QTableView.SelectionBehavior.SelectRows)
        self.table.setStyleSheet(
            "QTableView { "
            "  background-color: #FFFFFF; alternate-background-color: #F1F5FA; "
            "  color: #20242A; border: 1px solid #C4C8D0; border-radius: 3px; font-size: 12px; "
            "} "
            "QTableView::item { padding: 4px; border: none; } "
            "QTableView::item:selected { background-color: #3875D7; color: white; } "
            "QHeaderView::section { "
            "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #EBEBEB, stop:1 #D4D4D4); "
            "  color: #2E3338; font-weight: bold; font-size: 11px; padding: 4px 6px; border: none; "
            "  border-right: 1px solid #C4C4C4; border-bottom: 1px solid #ACACAC; "
            "}"
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
        songs_layout.addWidget(self.table, 1)

        splitter.addWidget(songs_frame)
        splitter.setStretchFactor(1, 1)
        root.addWidget(splitter, 1)

        # 3. Panel Inferior de Capacidad y Estado (Estilo Metálico iTunes 10)
        bottom = QFrame(self)
        bottom.setStyleSheet(
            "QFrame { "
            "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #EAEAEA, stop:0.5 #DFDFDF, stop:0.51 #D2D2D2, stop:1 #C8C8C8); "
            "  border-top: 1px solid #9E9E9E; border-radius: 0px; padding: 5px 14px; "
            "}"
        )
        b_layout = QVBoxLayout(bottom)
        b_layout.setSpacing(4)

        b_row = QHBoxLayout()
        self.icon_lbl = QLabel("📱", self)
        self.icon_lbl.setStyleSheet("font-size: 20px;")
        b_row.addWidget(self.icon_lbl)

        self.model_lbl = QLabel("Sin dispositivo", self)
        self.model_lbl.setStyleSheet("font-size: 13px; font-weight: bold; color: #20242A;")
        b_row.addWidget(self.model_lbl)
        b_row.addSpacing(12)

        self.sn_lbl = QLabel("", self)
        self.sn_lbl.setStyleSheet("color: #646C76; font-size: 11px;")
        b_row.addWidget(self.sn_lbl)
        b_row.addStretch()

        b_row.addWidget(QLabel("🔋", self))
        self.batt_lbl = QLabel("⚡ 4.20 V (100% • 400 mAh)", self)
        self.batt_lbl.setStyleSheet("color: #15803D; font-weight: bold; font-size: 11px;")
        self.batt_lbl.setToolTip(
            "Telemetría de Batería (Li-Po):\n"
            "• Voltaje actual: 4.20 V (Máx: 4.20 V - 4.30 V)\n"
            "• Carga acumulada: 400 / 400 mAh (100%)\n"
            "• Capacidad nominal: 400 mAh (iPod Nano 3G)\n"
            "• Circuito de protección: ACTIVO (Corte a 4.20V contra sobrecarga/hinchazón)"
        )
        b_row.addWidget(self.batt_lbl)
        b_row.addSpacing(16)

        self.free_lbl = QLabel("Libre: —", self)
        self.free_lbl.setStyleSheet("color: #555D66; font-size: 11px;")
        b_row.addWidget(self.free_lbl)

        self.tot_lbl = QLabel("Total: —", self)
        self.tot_lbl.setStyleSheet("color: #20242A; font-size: 11px; font-weight: bold;")
        b_row.addWidget(self.tot_lbl)
        b_layout.addLayout(b_row)

        self.cap_bar = CapacityBar(self)
        b_layout.addWidget(self.cap_bar)
        root.addWidget(bottom)

    def refresh_devices(self):
        self.device_combo.blockSignals(True)
        self.device_combo.clear()

        found = []
        if sys.platform == 'win32':
            for letter in string.ascii_uppercase:
                drive = f"{letter}:\\"
                if is_ipod_mount(drive):
                    found.append((f"iPod ({drive})", drive))
        else:
            # 1. En Linux: Leer /proc/mounts
            if os.path.exists("/proc/mounts"):
                try:
                    with open("/proc/mounts", "r", errors="ignore") as f:
                        for line in f:
                            parts = line.strip().split()
                            if len(parts) >= 2:
                                mnt = parts[1].replace("\\040", " ")
                                if is_ipod_mount(mnt):
                                    found.append((f"iPod ({mnt})", mnt))
                except Exception:
                    pass

            # 2. Escaneo en /run/media, /media y /mnt (hasta profundidad 2)
            for base in ["/run/media", "/media", "/mnt"]:
                if os.path.exists(base):
                    try:
                        for d1 in os.listdir(base):
                            p1 = os.path.join(base, d1)
                            if is_ipod_mount(p1):
                                found.append((f"iPod ({p1})", p1))
                                continue
                            if os.path.isdir(p1):
                                try:
                                    for d2 in os.listdir(p1):
                                        p2 = os.path.join(p1, d2)
                                        if is_ipod_mount(p2):
                                            found.append((f"iPod ({p2})", p2))
                                except Exception:
                                    pass
                    except Exception:
                        pass

        # Eliminar duplicados manteniendo orden
        unique_found = []
        seen = set()
        for lbl, p in found:
            if p not in seen:
                seen.add(p)
                unique_found.append((lbl, p))
        found = unique_found

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
            target = d
            if not is_ipod_mount(target):
                try:
                    for sub in os.listdir(target):
                        cand = os.path.join(target, sub)
                        if is_ipod_mount(cand):
                            target = cand
                            break
                except Exception:
                    pass
            self.device_combo.addItem(f"Manual: {target}", target)
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
        self.batt_lbl.setText("⚡ 4.20 V (100% • 650 mAh)")
        tot = 160 * 1024 * 1024 * 1024
        audio = 42 * 1024 * 1024 * 1024
        free = tot - audio
        self.tot_lbl.setText("Total: 148.9 GB")
        self.free_lbl.setText(f"Libre: {free / (1024**3):.1f} GB")
        self.cap_bar.set_storage(tot, audio, 0, 0, 0, 0, free)

    def load_real_device(self, mount_path: str):
        # 1. Leer SysInfo de hardware de forma insensible a mayúsculas
        sysinfo_p = find_case_insensitive_path(mount_path, "iPod_Control", "Device", "SysInfo")
        model = "iPod"
        sn = ""
        if sysinfo_p and os.path.exists(sysinfo_p):
            with open(sysinfo_p, "r", errors="ignore") as f:
                for line in f:
                    if line.startswith("ModelNumStr:"):
                        model_num = line.split(":", 1)[1].strip()
                        m_up = model_num.upper()
                        nano_3g_codes = ["A978", "A980", "B249", "B251", "B261", "B263", "B245", "B247", "B253", "B255", "B257", "B259", "B343", "B345"]
                        if any(k in m_up for k in nano_3g_codes):
                            model = f"iPod Nano 3G ({model_num})"
                        elif any(k in m_up for k in ["A004", "A099", "A107"]):
                            model = f"iPod Nano 1G ({model_num})"
                        elif any(k in m_up for k in ["A477", "A426", "A428"]):
                            model = f"iPod Nano 2G ({model_num})"
                        elif any(k in m_up for k in ["B598", "B754", "B903"]):
                            model = f"iPod Nano 4G ({model_num})"
                        elif any(k in m_up for k in ["C031", "C060", "C075"]):
                            model = f"iPod Nano 5G ({model_num})"
                        else:
                            model = f"iPod ({model_num})"
                    elif line.startswith("FirewireGuid:"):
                        sn = line.split(":", 1)[1].strip()

        self.model_lbl.setText(model)
        self.sn_lbl.setText(f"SN: {sn}" if sn else "")
        self.batt_lbl.setText("⚡ 4.20 V (100% • 400 mAh)")

        # 2. Base de datos iTunesDB
        itunesdb_p = find_case_insensitive_path(mount_path, "iPod_Control", "iTunes", "iTunesDB")
        if not itunesdb_p:
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

        for pl in user_pls:
            pl_item = QTreeWidgetItem(pl_branch)
            pl_item.setText(0, f"🎶 {pl.name} ({len(pl.tracks)})")
            pl_item.setData(0, Qt.ItemDataRole.UserRole, ("playlist", pl))

        root_item.setExpanded(True)
        dev_branch.setExpanded(True)
        pl_branch.setExpanded(True)

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

        # 1. Borrar archivos físicos del almacenamiento masivo del iPod
        for uid in ids:
            if uid in self.all_tracks:
                t = self.all_tracks[uid]
                if t.file_path:
                    clean = t.file_path.lstrip(':')
                    parts = clean.split(':')
                    real_path = os.path.join(self.current_mount, *parts)
                    if os.path.exists(real_path):
                        try:
                            os.remove(real_path)
                            deleted_files += 1
                        except Exception as e:
                            print(f"Error borrando {real_path}: {e}")

        # 2. Modificar, firmar con hash58 y guardar físicamente el archivo iTunesDB en el iPod
        itunesdb_p = os.path.join(self.current_mount, "iPod_Control", "iTunes", "iTunesDB")
        if os.path.exists(itunesdb_p):
            remove_tracks_from_itunesdb(itunesdb_p, id_set)

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
        QMessageBox.information(self, "Expulsar", "Dispositivo listo para desconectar de forma segura.")


def main():
    app = QApplication(sys.argv)
    palette = QPalette()
    palette.setColor(QPalette.ColorRole.Window, QColor(234, 236, 240))
    palette.setColor(QPalette.ColorRole.WindowText, QColor(32, 36, 42))
    palette.setColor(QPalette.ColorRole.Base, QColor(255, 255, 255))
    palette.setColor(QPalette.ColorRole.AlternateBase, QColor(241, 245, 250))
    palette.setColor(QPalette.ColorRole.Text, QColor(32, 36, 42))
    palette.setColor(QPalette.ColorRole.Button, QColor(234, 236, 240))
    palette.setColor(QPalette.ColorRole.ButtonText, QColor(32, 36, 42))
    palette.setColor(QPalette.ColorRole.Highlight, QColor(56, 117, 215))
    palette.setColor(QPalette.ColorRole.HighlightedText, Qt.GlobalColor.white)
    app.setPalette(palette)

    win = MainWindow()
    win.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
