#include "TrackImporter.h"
#include "AudioConverter.h"
#include "ITunesDBReader.h"
#include "ITunesHash58.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QDateTime>
#include <QDebug>
#include <cstring>

namespace {
    inline uint32_t readLE32(const char* ptr) {
        uint32_t val;
        std::memcpy(&val, ptr, 4);
        return val;
    }

    inline void writeLE32(char* ptr, uint32_t val) {
        std::memcpy(ptr, &val, 4);
    }

    QByteArray makeMhodString(uint32_t type, const QString& text) {
        int charCount = text.length();
        int byteCount = charCount * 2;
        const ushort* utf16 = text.utf16();

        QByteArray mhod(40, '\0');
        std::memcpy(mhod.data(), "mhod", 4);
        writeLE32(mhod.data() + 4, 24); // header len
        writeLE32(mhod.data() + 8, static_cast<uint32_t>(40 + byteCount)); // total len
        writeLE32(mhod.data() + 12, type);
        writeLE32(mhod.data() + 16, 1);
        writeLE32(mhod.data() + 20, 0);
        writeLE32(mhod.data() + 24, 0);
        writeLE32(mhod.data() + 28, static_cast<uint32_t>(byteCount));
        writeLE32(mhod.data() + 32, 0);
        writeLE32(mhod.data() + 36, 0);

        mhod.append(reinterpret_cast<const char*>(utf16), byteCount);
        return mhod;
    }

    QByteArray makeMhit(uint32_t trackId, const Track& track, const QString& ipodApplePath) {
        QList<QByteArray> mhods;
        if (!track.title.isEmpty()) mhods.append(makeMhodString(1, track.title));
        if (!ipodApplePath.isEmpty()) mhods.append(makeMhodString(2, ipodApplePath));
        if (!track.album.isEmpty()) mhods.append(makeMhodString(3, track.album));
        if (!track.artist.isEmpty()) mhods.append(makeMhodString(4, track.artist));
        if (!track.genre.isEmpty()) mhods.append(makeMhodString(5, track.genre));
        if (!track.fileType.isEmpty()) mhods.append(makeMhodString(6, track.fileType));

        uint32_t mhodsTotalSize = 0;
        for (const auto& m : mhods) mhodsTotalSize += m.size();

        uint32_t headerLen = 144;
        uint32_t totalLen = headerLen + mhodsTotalSize;

        QByteArray mhit(static_cast<int>(headerLen), '\0');
        std::memcpy(mhit.data(), "mhit", 4);
        writeLE32(mhit.data() + 4, headerLen);
        writeLE32(mhit.data() + 8, totalLen);
        writeLE32(mhit.data() + 12, static_cast<uint32_t>(mhods.size()));
        writeLE32(mhit.data() + 16, trackId);
        writeLE32(mhit.data() + 20, 1); // visible

        // FourCC
        const char* fourcc = ipodApplePath.endsWith(".m4a", Qt::CaseInsensitive) || ipodApplePath.endsWith(".aac", Qt::CaseInsensitive) ? "AAC " : "MP3 ";
        std::memcpy(mhit.data() + 24, fourcc, 4);

        writeLE32(mhit.data() + 32, static_cast<uint32_t>(QDateTime::currentSecsSinceEpoch()));
        writeLE32(mhit.data() + 36, static_cast<uint32_t>(track.sizeBytes));
        writeLE32(mhit.data() + 40, static_cast<uint32_t>(track.durationMs));
        writeLE32(mhit.data() + 44, static_cast<uint32_t>(track.trackNumber > 0 ? track.trackNumber : 1));
        writeLE32(mhit.data() + 48, static_cast<uint32_t>(track.trackNumber > 0 ? track.trackNumber : 1));
        writeLE32(mhit.data() + 52, static_cast<uint32_t>(track.year > 0 ? track.year : QDate::currentDate().year()));
        writeLE32(mhit.data() + 56, static_cast<uint32_t>(track.bitrate > 0 ? track.bitrate : 256));
        writeLE32(mhit.data() + 60, static_cast<uint32_t>(track.sampleRate > 0 ? track.sampleRate : 44100));

        for (const auto& m : mhods) {
            mhit.append(m);
        }
        return mhit;
    }

    QByteArray makeMhip(uint32_t trackId) {
        QByteArray mhip(28, '\0');
        std::memcpy(mhip.data(), "mhip", 4);
        writeLE32(mhip.data() + 4, 28);
        writeLE32(mhip.data() + 8, 28);
        writeLE32(mhip.data() + 12, 0);
        writeLE32(mhip.data() + 16, 0);
        writeLE32(mhip.data() + 20, 0);
        writeLE32(mhip.data() + 24, trackId);
        return mhip;
    }
}

QString TrackImporter::allocateIpodPath(const QString& mountPath, const QString& extension, QString& outApplePath) {
    QDir musicDir(mountPath);
    QString musicRel = "iPod_Control/Music";
    if (!musicDir.exists(musicRel)) {
        musicDir.mkpath(musicRel);
    }

    // Buscar la subcarpeta F00..F19 con menor cantidad de archivos
    int bestF = 0;
    int minFiles = 999999;
    for (int i = 0; i < 20; ++i) {
        QString fName = QString("F%1").arg(i, 2, 10, QChar('0'));
        QDir sub(musicDir.filePath(musicRel + "/" + fName));
        if (!sub.exists()) {
            sub.mkpath(".");
            bestF = i;
            break;
        }
        int count = sub.entryList(QDir::Files).size();
        if (count < minFiles) {
            minFiles = count;
            bestF = i;
        }
    }

    QString folderStr = QString("F%1").arg(bestF, 2, 10, QChar('0'));
    QDir targetDir(musicDir.filePath(musicRel + "/" + folderStr));
    targetDir.mkpath(".");

    // Generar nombre aleatorio de 4 letras mayúsculas
    QString randomLetters;
    QString physicalTarget;
    do {
        randomLetters.clear();
        for (int j = 0; j < 4; ++j) {
            char c = static_cast<char>('A' + QRandomGenerator::global()->bounded(26));
            randomLetters.append(c);
        }
        QString filename = randomLetters + "." + extension.toLower();
        physicalTarget = targetDir.filePath(filename);
    } while (QFile::exists(physicalTarget));

    outApplePath = QString(":iPod_Control:Music:%1:%2.%3")
        .arg(folderStr)
        .arg(randomLetters)
        .arg(extension.toLower());

    return physicalTarget;
}

bool TrackImporter::importTracksToDevice(
    const QString& mountPath,
    const QString& firewireGuid,
    const QList<QString>& sourceFilePaths,
    std::function<void(int current, int total, uint64_t currentBytes, uint64_t totalBytes, const QString& status)> progressCb
) {
    if (sourceFilePaths.isEmpty() || mountPath.isEmpty()) return false;

    QDir dir(mountPath);
    QString dbPath = dir.filePath("iPod_Control/iTunes/iTunesDB");
    if (!QFile::exists(dbPath)) {
        dbPath = dir.filePath("ipod_control/itunes/itunesdb");
    }

    uint64_t totalBytesToCopy = 0;
    for (const auto& p : sourceFilePaths) {
        totalBytesToCopy += QFileInfo(p).size();
    }

    QList<QPair<Track, QString>> importedList;
    uint64_t copiedBytes = 0;
    int current = 0;
    int total = sourceFilePaths.size();

    for (const auto& src : sourceFilePaths) {
        current++;
        QFileInfo fi(src);
        uint64_t fSize = fi.size();

        if (progressCb) {
            progressCb(current, total, copiedBytes, totalBytesToCopy,
                       QString("Copiando (%1/%2): %3...").arg(current).arg(total).arg(fi.fileName()));
        }

        Track track = AudioConverter::extractMetadata(src);

        QString applePath;
        QString physicalTarget = allocateIpodPath(mountPath, fi.suffix(), applePath);

        if (!QFile::copy(src, physicalTarget)) {
            qWarning() << "No se pudo copiar el archivo al iPod:" << src << "->" << physicalTarget;
            continue;
        }

        track.sizeBytes = QFileInfo(physicalTarget).size();
        track.filePath = applePath;
        importedList.append({track, applePath});

        copiedBytes += fSize;
        if (progressCb) {
            progressCb(current, total, copiedBytes, totalBytesToCopy,
                       QString("Guardada: %1").arg(track.title));
        }
    }

    if (importedList.isEmpty()) {
        return false;
    }

    if (progressCb) {
        progressCb(total, total, copiedBytes, totalBytesToCopy,
                   "Actualizando base de datos iTunesDB y calculando hash de hardware...");
    }

    bool dbOk = insertTracksIntoDatabase(dbPath, firewireGuid, importedList);

    ITunesDBReader::flushVolumeBuffers(mountPath);

    if (progressCb) {
        progressCb(total, total, totalBytesToCopy, totalBytesToCopy,
                   "¡Canciones añadidas con éxito al iPod!");
    }

    return dbOk;
}

bool TrackImporter::insertTracksIntoDatabase(
    const QString& dbPath,
    const QString& firewireGuid,
    const QList<QPair<Track, QString>>& importedTracks
) {
    if (importedTracks.isEmpty()) return false;

    QFile file(dbPath);
    if (!file.open(QIODevice::ReadOnly)) return false;
    QByteArray data = file.readAll();
    file.close();

    if (data.size() < 100 || std::memcmp(data.constData(), "mhbd", 4) != 0) return false;

    // Respaldo de seguridad
    QString bakPath = dbPath + ".bak";
    if (!QFile::exists(bakPath)) {
        QFile::copy(dbPath, bakPath);
    }

    // 1. Encontrar el max trackId existente
    uint32_t mhbdHdrLen = readLE32(data.constData() + 4);
    int offset = static_cast<int>(mhbdHdrLen);
    const int totalSize = data.size();
    uint32_t maxTrackId = 100;

    int ds1Offset = -1;
    int ds2Offset = -1;

    while (offset + 16 <= totalSize) {
        const char* tag = data.constData() + offset;
        if (std::memcmp(tag, "mhsd", 4) != 0) break;
        uint32_t hdrLen = readLE32(tag + 4);
        uint32_t totLen = readLE32(tag + 8);
        uint32_t dsType = readLE32(tag + 12);
        int childOffset = offset + static_cast<int>(hdrLen);

        if (dsType == 1) {
            ds1Offset = offset;
            if (childOffset + 12 <= totalSize) {
                const char* mhltTag = data.constData() + childOffset;
                if (std::memcmp(mhltTag, "mhlt", 4) == 0) {
                    uint32_t numTracks = readLE32(mhltTag + 8);
                    uint32_t mhltHdr = readLE32(mhltTag + 4);
                    int trackOffset = childOffset + static_cast<int>(mhltHdr);
                    for (uint32_t t = 0; t < numTracks && trackOffset + 20 <= totalSize; ++t) {
                        const char* mhitTag = data.constData() + trackOffset;
                        if (std::memcmp(mhitTag, "mhit", 4) != 0) break;
                        uint32_t tId = readLE32(mhitTag + 16);
                        if (tId > maxTrackId) maxTrackId = tId;
                        uint32_t mhitTotLen = readLE32(mhitTag + 8);
                        if (mhitTotLen == 0) break;
                        trackOffset += static_cast<int>(mhitTotLen);
                    }
                }
            }
        } else if (dsType == 2) {
            ds2Offset = offset;
        }

        if (totLen == 0) break;
        offset += static_cast<int>(totLen);
    }

    // 2. Construir los mhits e mhips para las nuevas canciones
    QByteArray newMhitsData;
    QByteArray newMhipsData;
    uint32_t currentNewId = maxTrackId + 1;

    for (const auto& pair : importedTracks) {
        const Track& t = pair.first;
        const QString& applePath = pair.second;

        newMhitsData.append(makeMhit(currentNewId, t, applePath));
        newMhipsData.append(makeMhip(currentNewId));
        currentNewId++;
    }

    // 3. Reconstruir la base de datos insertando los nuevos datos en ds1 y ds2
    QByteArray newFile = data.left(static_cast<int>(mhbdHdrLen));
    offset = static_cast<int>(mhbdHdrLen);

    while (offset + 16 <= totalSize) {
        const char* tag = data.constData() + offset;
        if (std::memcmp(tag, "mhsd", 4) != 0) break;

        uint32_t hdrLen = readLE32(tag + 4);
        uint32_t totLen = readLE32(tag + 8);
        uint32_t dsType = readLE32(tag + 12);
        int childOffset = offset + static_cast<int>(hdrLen);

        if (dsType == 1 && childOffset + 12 <= totalSize) {
            const char* mhltTag = data.constData() + childOffset;
            uint32_t mhltHdrLen = readLE32(mhltTag + 4);
            uint32_t numTracks = readLE32(mhltTag + 8);

            QByteArray mhsdHdr = data.mid(offset, static_cast<int>(hdrLen));
            QByteArray mhltHdr = data.mid(childOffset, static_cast<int>(mhltHdrLen));

            // Actualizar número de canciones
            writeLE32(mhltHdr.data() + 8, numTracks + importedTracks.size());

            // Copiar mhits existentes y agregar los nuevos
            int existingTracksLen = static_cast<int>(totLen) - static_cast<int>(hdrLen) - static_cast<int>(mhltHdrLen);
            QByteArray tracksData = data.mid(childOffset + static_cast<int>(mhltHdrLen), existingTracksLen);
            tracksData.append(newMhitsData);

            QByteArray dsPayload = mhltHdr + tracksData;
            writeLE32(mhsdHdr.data() + 8, static_cast<uint32_t>(mhsdHdr.size() + dsPayload.size()));

            newFile.append(mhsdHdr + dsPayload);
        }
        else if (dsType == 2 && childOffset + 12 <= totalSize) {
            // Playlists: agregar las pistas a la Master Playlist (playlist 0)
            const char* mhlpTag = data.constData() + childOffset;
            uint32_t mhlpHdrLen = readLE32(mhlpTag + 4);

            QByteArray mhsdHdr = data.mid(offset, static_cast<int>(hdrLen));
            QByteArray mhlpHdr = data.mid(childOffset, static_cast<int>(mhlpHdrLen));

            int plOffset = childOffset + static_cast<int>(mhlpHdrLen);
            const char* mhypTag = data.constData() + plOffset;

            uint32_t mhypHdrLen = readLE32(mhypTag + 4);
            uint32_t mhypTotLen = readLE32(mhypTag + 8);
            uint32_t numSongs = readLE32(mhypTag + 16);

            // Modificar el encabezado de la primera playlist (Master)
            QByteArray mhypHdr = data.mid(plOffset, static_cast<int>(mhypHdrLen));
            writeLE32(mhypHdr.data() + 16, numSongs + importedTracks.size());
            writeLE32(mhypHdr.data() + 8, mhypTotLen + static_cast<uint32_t>(newMhipsData.size()));

            // El resto de la playlist 0 (mhods + mhips existentes) + nuevas mhips
            int mhypRestLen = static_cast<int>(mhypTotLen) - static_cast<int>(mhypHdrLen);
            QByteArray mhypRest = data.mid(plOffset + static_cast<int>(mhypHdrLen), mhypRestLen);
            mhypRest.append(newMhipsData);

            // Playlists restantes sin modificar
            int remainingPlOffset = plOffset + static_cast<int>(mhypTotLen);
            int remainingPlLen = static_cast<int>(totLen) - (remainingPlOffset - offset);
            QByteArray remainingPlaylists = data.mid(remainingPlOffset, remainingPlLen);

            QByteArray dsPayload = mhlpHdr + mhypHdr + mhypRest + remainingPlaylists;
            writeLE32(mhsdHdr.data() + 8, static_cast<uint32_t>(mhsdHdr.size() + dsPayload.size()));

            newFile.append(mhsdHdr + dsPayload);
        }
        else {
            // Otros datasets se copian idénticos
            newFile.append(data.mid(offset, static_cast<int>(totLen)));
        }

        if (totLen == 0) break;
        offset += static_cast<int>(totLen);
    }

    // Actualizar mhbd total length
    writeLE32(newFile.data() + 8, static_cast<uint32_t>(newFile.size()));

    // Recalcular hash58 si el modelo lo requiere (ej. iPod Nano 3G, Classic)
    if (!firewireGuid.isEmpty()) {
        ITunesHash58::updateHash(newFile, firewireGuid);
    }

    QString tmpPath = dbPath + ".tmp";
    QFile tmpFile(tmpPath);
    if (!tmpFile.open(QIODevice::WriteOnly)) {
        return false;
    }
    tmpFile.write(newFile);
    tmpFile.flush();
    tmpFile.close();

    QFile::remove(dbPath);
    return QFile::rename(tmpPath, dbPath);
}
