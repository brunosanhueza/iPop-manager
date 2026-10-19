#pragma once

#include <QString>
#include <QList>
#include <functional>

// Puente hacia las herramientas de tools/ipod (gestor + nochmod.so + openssl-legacy.cnf).
// Se usa con iPod Touch / iOS 4: la biblioteca vive en iTunesCDB + "iTunes Library.itlp"
// (SQLite) y solo libgpod sabe escribirla con la firma que el iPod acepta.
class IpodTouchBridge {
public:
    using ProgressCb = std::function<void(int current, int total, uint64_t currentBytes,
                                          uint64_t totalBytes, const QString& status)>;

    // true si el montaje tiene la estructura de un iPod Touch (iTunes Library.itlp)
    static bool isTouchLayout(const QString& mountPath);

    // Carpeta donde estan gestor / nochmod.so / openssl-legacy.cnf (vacia si no se encuentran)
    static QString toolsDir();
    static bool toolsAvailable(QString* errorMsg = nullptr);

    // Copia los archivos y escribe la base UNA sola vez. Devuelve true si se anadio al menos uno.
    static bool addTracks(const QString& mountPath,
                          const QList<QString>& sourceFiles,
                          ProgressCb progressCb = nullptr,
                          QString* errorMsg = nullptr,
                          int* addedCount = nullptr,
                          const QString& playlistName = QString());

    // Borra por dbid (cancion + archivo mp3) en una sola escritura. Todo o nada si un id no existe.
    static bool deleteTracks(const QString& mountPath,
                             const QList<uint64_t>& dbids,
                             QString* errorMsg = nullptr);

    // Listas de reproduccion (se identifican por nombre; la lista maestra no se puede tocar)
    static bool createPlaylist(const QString& mountPath, const QString& name, QString* errorMsg = nullptr);
    static bool renamePlaylist(const QString& mountPath, const QString& oldName, const QString& newName, QString* errorMsg = nullptr);
    static bool deletePlaylists(const QString& mountPath, const QStringList& names, QString* errorMsg = nullptr);

    // Desmonta un montaje FUSE de ifuse (sync + fusermount -u)
    static bool unmount(const QString& mountPath, QString* errorMsg = nullptr);

    // Entorno para las herramientas libimobiledevice/libgpod (OPENSSL_CONF legado)
    static QStringList environmentWithLegacyTls();
};
