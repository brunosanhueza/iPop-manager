#!/usr/bin/env python3
"""Convierte texto japones a romaji ASCII para usarlo como clave de ORDEN en el iPod.

Entrada : una linea de texto por linea (stdin, UTF-8).
Salida  : primera linea "#ENGINE <motor>", y despues una linea por cada entrada.
Motor   : "pykakasi" (kanji + kana; pip install --user pykakasi) o "kana" (solo hiragana/katakana,
          los kanji se descartan). Con ROMANIZAR_FORCE_FALLBACK=1 se fuerza el motor "kana".
"""
import os
import sys
import unicodedata


def is_ascii(s):
    return all(ord(c) < 128 for c in s)


def to_ascii(s):
    s = unicodedata.normalize("NFKD", s)
    s = "".join(c for c in s if ord(c) < 128)
    return " ".join(s.split())


# ---------------- motor "kana" (respaldo sin dependencias) ----------------
_BASE = ("あa いi うu えe おo かka きki くku けke こko さsa しshi すsu せse そso たta ちchi つtsu てte とto "
         "なna にni ぬnu ねne のno はha ひhi ふfu へhe ほho まma みmi むmu めme もmo やya ゆyu よyo "
         "らra りri るru れre ろro わwa をo んn がga ぎgi ぐgu げge ごgo ざza じji ずzu ぜze ぞzo "
         "だda ぢji づzu でde どdo ばba びbi ぶbu べbe ぼbo ぱpa ぴpi ぷpu ぺpe ぽpo ぁa ぃi ぅu ぇe ぉo "
         "ゔvu ゐi ゑe ゎwa")
_DIGRAPH = ("きゃkya きゅkyu きょkyo しゃsha しゅshu しょsho ちゃcha ちゅchu ちょcho にゃnya にゅnyu にょnyo "
            "ひゃhya ひゅhyu ひょhyo みゃmya みゅmyu みょmyo りゃrya りゅryu りょryo ぎゃgya ぎゅgyu ぎょgyo "
            "じゃja じゅju じょjo びゃbya びゅbyu びょbyo ぴゃpya ぴゅpyu ぴょpyo ふぁfa ふぃfi ふぇfe ふぉfo "
            "てぃti でぃdi とぅtu どぅdu うぃwi うぇwe うぉwo ゔぁva ゔぃvi ゔぇve ゔぉvo しぇshe じぇje ちぇche")


def _table(spec):
    d = {}
    for item in spec.split():
        n = 2 if item[1] in "ゃゅょぁぃぅぇぉ" else 1
        d[item[:n]] = item[n:]
    return d


_B = _table(_BASE)
_D = _table(_DIGRAPH)


def _kata_to_hira(c):
    o = ord(c)
    return chr(o - 0x60) if 0x30A1 <= o <= 0x30F6 else c


def romanize_kana(text):
    s = "".join(_kata_to_hira(c) for c in text)
    out, i, geminate = [], 0, False
    while i < len(s):
        c = s[i]
        if c == "っ":
            geminate = True
            i += 1
            continue
        if c == "ー" and out:
            last = out[-1]
            out.append(last[-1] if last and last[-1] in "aiueo" else "")
            i += 1
            continue
        rom = None
        if s[i:i + 2] in _D:
            rom, step = _D[s[i:i + 2]], 2
        elif c in _B:
            rom, step = _B[c], 1
        if rom is None:
            out.append(c if ord(c) < 128 or c.isspace() else "")  # kanji y otros: se descartan
            geminate = False
            i += 1
            continue
        if geminate and rom[0] not in "aiueon":
            rom = ("t" if rom.startswith("ch") else rom[0]) + rom
        geminate = False
        out.append(rom)
        i += step
    return to_ascii("".join(out))


# ---------------- motor "pykakasi" ----------------
_kks = None
ENGINE = "kana"
if os.environ.get("ROMANIZAR_FORCE_FALLBACK") != "1":
    try:
        import pykakasi
        _kks = pykakasi.kakasi()
        ENGINE = "pykakasi"
    except Exception:
        _kks = None


def romanize_pykakasi(text):
    out, prev_rom = [], False
    for tok in _kks.convert(text):
        orig = tok["orig"]
        if is_ascii(orig):
            out.append(orig)
            prev_rom = False
        else:
            if prev_rom:
                out.append(" ")
            out.append(tok["hepburn"])
            prev_rom = True
    return to_ascii("".join(out))


def romanize(text):
    try:
        return romanize_pykakasi(text) if _kks else romanize_kana(text)
    except Exception:
        return romanize_kana(text)


def main():
    sys.stdin.reconfigure(encoding="utf-8")
    sys.stdout.reconfigure(encoding="utf-8")
    lines = sys.stdin.read().split("\n")
    if lines and lines[-1] == "":
        lines.pop()
    sys.stdout.write("#ENGINE %s\n" % ENGINE)
    for line in lines:
        sys.stdout.write(romanize(line) + "\n")


if __name__ == "__main__":
    main()
