#pragma once

#include <QString>
#include <QStringList>

// Convierte texto japones (kanji, hiragana, katakana) a romaji ASCII usando tools/ipod/romanizar.py
// (requiere python3; con el modulo pykakasi tambien convierte kanji). Se usa como CLAVE DE ORDEN:
// el iPod agrupa por letra segun sort_title/sort_artist/sort_album, y todo lo que no es ASCII cae en "#".
class Romanizer {
public:
    // true si el texto tiene algun caracter no ASCII
    static bool needsRomanization(const QString& s);

    // Ruta del script, o vacio si no se encuentra
    static QString scriptPath();
    static bool available(QString* why = nullptr);

    // Devuelve una entrada por texto: la version ASCII, o "" si no hace falta o no se pudo.
    // engine: "pykakasi" (kanji + kana) o "kana" (solo kana; los kanji se descartan).
    static QStringList romanize(const QStringList& texts, QString* engine = nullptr, QString* error = nullptr);
};
