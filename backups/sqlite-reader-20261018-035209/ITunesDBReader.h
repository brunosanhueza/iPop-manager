#pragma once

#include <QString>
#include <QList>
#include <QSet>
#include "Playlist.h"
#include "Track.h"

class ITunesDBReader {
public:
    explicit ITunesDBReader(const QString& ipodMountPath);

    bool read(QList<Playlist>& outPlaylists);

    // Elimina físicamente pistas de la base de datos binaria iTunesDB en el disco del iPod y recalcula hash58
    static bool removeTracksFromDatabase(const QString& dbPath, const QSet<uint32_t>& deleteUids, const QString& firewireGuid = QString());

    // Elimina playlists por su índice en la base de datos (omitiendo la lista maestra 0)
    static bool deletePlaylistsFromDatabase(const QString& dbPath, const QSet<int>& playlistIndices, const QString& firewireGuid = QString());

    // Renombra una playlist existente en la base de datos binaria
    static bool renamePlaylistInDatabase(const QString& dbPath, int playlistIndex, const QString& newName, const QString& firewireGuid = QString());

    // Crea una nueva playlist en la base de datos binaria
    static bool createPlaylistInDatabase(const QString& dbPath, const QString& name, const QString& firewireGuid = QString());

    // Asegura que todas las tablas FAT32 y búferes del sistema operativo se escriban inmediatamente al iPod
    static void flushVolumeBuffers(const QString& mountPath);

    // Resuelve la ruta física real en disco a partir del path de Apple (ej. ":iPod_Control:Music:F05:FNCX.mp3")
    static QString resolvePhysicalPath(const QString& mountPath, const QString& ipodPath);

private:
    bool parseBinaryDatabase(const QString& dbPath, QList<Playlist>& outPlaylists);
    bool parseSqliteDatabase(const QString& dbPath, QList<Playlist>& outPlaylists);
    void scanFilesFallback(QList<Playlist>& outPlaylists);

    QString m_mountPath;
};
