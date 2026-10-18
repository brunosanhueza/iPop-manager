#include "ITunesDBReader.h"
#include "ITunesHash58.h"

#include <QFile>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QMap>
#include <cstring>
#include <sqlite3.h>
#include <QHash>
#include <QSet>

#ifdef Q_OS_WIN
#include <windows.h>
#include <shlobj.h>
#include <io.h>
#else
#include <unistd.h>
#endif

namespace {
    inline uint32_t readLE32(const char* ptr) {
        uint32_t val;
        std::memcpy(&val, ptr, 4);
        return val;
    }

    inline void writeLE32(char* ptr, uint32_t val) {
        std::memcpy(ptr, &val, 4);
    }

    QString parseMhodString(const QByteArray& data, int mhodOffset, int mhodLen) {
        if (mhodOffset + 40 > data.size() || mhodLen < 40) return "";

        const char* ptr = data.constData() + mhodOffset;
        uint32_t strLenBytes = readLE32(ptr + 28);
        int strStart = mhodOffset + 40;

        if (strLenBytes == 0 || strStart + static_cast<int>(strLenBytes) > data.size()) {
            return "";
        }

        const ushort* u16ptr = reinterpret_cast<const ushort*>(data.constData() + strStart);
        int charCount = static_cast<int>(strLenBytes / 2);
        return QString::fromUtf16(u16ptr, charCount).trimmed();
    }

    QByteArray makeMhodString(uint32_t type, const QString& text) {
        int charCount = text.length();
        int byteCount = charCount * 2;
        const ushort* utf16 = text.utf16();

        QByteArray mhod(40, '\0');
        std::memcpy(mhod.data(), "mhod", 4);
        writeLE32(mhod.data() + 4, 24);
        writeLE32(mhod.data() + 8, static_cast<uint32_t>(40 + byteCount));
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
}

ITunesDBReader::ITunesDBReader(const QString& ipodMountPath)
    : m_mountPath(ipodMountPath)
{
}

QString ITunesDBReader::resolvePhysicalPath(const QString& mountPath, const QString& ipodPath) {
    if (ipodPath.isEmpty()) return "";
    QString clean = ipodPath;
    if (clean.startsWith(':')) {
        clean.remove(0, 1);
    }
    QStringList parts = clean.split(':', Qt::SkipEmptyParts);
    QString relPath = parts.join('/');

    QDir dir(mountPath);
    return dir.filePath(relPath);
}

bool ITunesDBReader::removeTracksFromDatabase(const QString& dbPath, const QSet<uint32_t>& deleteUids, const QString& firewireGuid) {
    if (deleteUids.isEmpty()) return true;

    QFile file(dbPath);
    if (!file.open(QIODevice::ReadOnly)) return false;
    QByteArray data = file.readAll();
    file.close();

    if (data.size() < 100 || std::memcmp(data.constData(), "mhbd", 4) != 0) return false;

    // Crear respaldo de seguridad si no existe
    QString bakPath = dbPath + ".bak";
    if (!QFile::exists(bakPath)) {
        QFile::copy(dbPath, bakPath);
    }

    uint32_t mhbdHdrLen = readLE32(data.constData() + 4);
    QByteArray newFile = data.left(static_cast<int>(mhbdHdrLen));

    int offset = static_cast<int>(mhbdHdrLen);
    const int totalSize = data.size();

    while (offset + 16 <= totalSize) {
        const char* tag = data.constData() + offset;
        if (std::memcmp(tag, "mhsd", 4) != 0) break;

        uint32_t hdrLen = readLE32(tag + 4);
        uint32_t totalLen = readLE32(tag + 8);
        uint32_t dsType = readLE32(tag + 12);
        int childOffset = offset + static_cast<int>(hdrLen);

        // Dataset 1: Pistas (mhlt -> mhit)
        if (dsType == 1 && childOffset + 12 <= totalSize) {
            const char* mhltTag = data.constData() + childOffset;
            if (std::memcmp(mhltTag, "mhlt", 4) == 0) {
                uint32_t mhltHdrLen = readLE32(mhltTag + 4);
                uint32_t numTracks = readLE32(mhltTag + 8);

                QByteArray mhsdHdr = data.mid(offset, static_cast<int>(hdrLen));
                QByteArray mhltHdr = data.mid(childOffset, static_cast<int>(mhltHdrLen));
                QByteArray remainingHits;

                int trackOffset = childOffset + static_cast<int>(mhltHdrLen);
                uint32_t remainingCount = 0;

                for (uint32_t t = 0; t < numTracks && trackOffset + 40 <= totalSize; ++t) {
                    const char* mhitTag = data.constData() + trackOffset;
                    if (std::memcmp(mhitTag, "mhit", 4) != 0) break;

                    uint32_t mhitTotLen = readLE32(mhitTag + 8);
                    uint32_t trackId = readLE32(mhitTag + 16);

                    if (!deleteUids.contains(trackId)) {
                        remainingHits.append(data.mid(trackOffset, static_cast<int>(mhitTotLen)));
                        remainingCount++;
                    }

                    if (mhitTotLen == 0) break;
                    trackOffset += static_cast<int>(mhitTotLen);
                }

                // Actualizar número de pistas en mhlt
                writeLE32(mhltHdr.data() + 8, remainingCount);

                QByteArray dsPayload = mhltHdr + remainingHits;
                uint32_t newDsTotLen = static_cast<uint32_t>(mhsdHdr.size() + dsPayload.size());
                writeLE32(mhsdHdr.data() + 8, newDsTotLen);

                newFile.append(mhsdHdr + dsPayload);
            }
        }
        // Datasets 2 y 3: Playlists (mhlp -> mhyp -> mhip)
        else if ((dsType == 2 || dsType == 3) && childOffset + 12 <= totalSize) {
            const char* mhlpTag = data.constData() + childOffset;
            if (std::memcmp(mhlpTag, "mhlp", 4) == 0) {
                uint32_t mhlpHdrLen = readLE32(mhlpTag + 4);
                uint32_t numPlaylists = readLE32(mhlpTag + 8);

                QByteArray mhsdHdr = data.mid(offset, static_cast<int>(hdrLen));
                QByteArray mhlpHdr = data.mid(childOffset, static_cast<int>(mhlpHdrLen));
                QByteArray remainingHyps;

                int plOffset = childOffset + static_cast<int>(mhlpHdrLen);

                for (uint32_t p = 0; p < numPlaylists && plOffset + 24 <= totalSize; ++p) {
                    const char* mhypTag = data.constData() + plOffset;
                    if (std::memcmp(mhypTag, "mhyp", 4) != 0) break;

                    uint32_t plHdrLen = readLE32(mhypTag + 4);
                    uint32_t plTotLen = readLE32(mhypTag + 8);
                    uint32_t numMhods = readLE32(mhypTag + 12);
                    uint32_t numSongs = readLE32(mhypTag + 16);

                    QByteArray mhypHdr = data.mid(plOffset, static_cast<int>(plHdrLen));

                    // Copiar mhods de la playlist
                    int mhodOffset = plOffset + static_cast<int>(plHdrLen);
                    QByteArray mhodsBytes;
                    for (uint32_t m = 0; m < numMhods && mhodOffset + 12 <= totalSize; ++m) {
                        const char* mhodTag = data.constData() + mhodOffset;
                        if (std::memcmp(mhodTag, "mhod", 4) != 0) break;
                        uint32_t mhodTotLen = readLE32(mhodTag + 8);
                        mhodsBytes.append(data.mid(mhodOffset, static_cast<int>(mhodTotLen)));
                        if (mhodTotLen == 0) break;
                        mhodOffset += static_cast<int>(mhodTotLen);
                    }

                    // Filtrar canciones de la playlist (mhip)
                    int hipOffset = mhodOffset;
                    QByteArray remainingHips;
                    uint32_t songsCount = 0;

                    for (uint32_t s = 0; s < numSongs && hipOffset + 28 <= totalSize; ++s) {
                        const char* mhipTag = data.constData() + hipOffset;
                        if (std::memcmp(mhipTag, "mhip", 4) != 0) break;

                        uint32_t hTotLen = readLE32(mhipTag + 8);
                        uint32_t trackUid = readLE32(mhipTag + 24);

                        if (!deleteUids.contains(trackUid)) {
                            remainingHips.append(data.mid(hipOffset, static_cast<int>(hTotLen)));
                            songsCount++;
                        }

                        if (hTotLen == 0) break;
                        hipOffset += static_cast<int>(hTotLen);
                    }

                    // Actualizar num_tracks en mhyp
                    writeLE32(mhypHdr.data() + 16, songsCount);

                    QByteArray mhypPayload = mhodsBytes + remainingHips;
                    uint32_t newPlTotLen = static_cast<uint32_t>(mhypHdr.size() + mhypPayload.size());
                    writeLE32(mhypHdr.data() + 8, newPlTotLen);

                    remainingHyps.append(mhypHdr + mhypPayload);

                    if (plTotLen == 0) break;
                    plOffset += static_cast<int>(plTotLen);
                }

                QByteArray dsPayload = mhlpHdr + remainingHyps;
                uint32_t newDsTotLen = static_cast<uint32_t>(mhsdHdr.size() + dsPayload.size());
                writeLE32(mhsdHdr.data() + 8, newDsTotLen);

                newFile.append(mhsdHdr + dsPayload);
            }
        }
        else {
            // Otros datasets (tipo 4, 5, etc.) se copian tal cual
            newFile.append(data.mid(offset, static_cast<int>(totalLen)));
        }

        if (totalLen == 0) break;
        offset += static_cast<int>(totalLen);
    }

    // Actualizar total_len de mhbd en el encabezado general
    writeLE32(newFile.data() + 8, static_cast<uint32_t>(newFile.size()));

    // Recalcular hash58 para modelos compatibles (Nano 3G, Classic, etc.)
    QString guid = firewireGuid.trimmed();
    if (guid.isEmpty()) {
        QFileInfo fi(dbPath);
        QDir mountDir = fi.dir();
        mountDir.cdUp(); // hacia iPod_Control
        mountDir.cdUp(); // hacia raíz del iPod
        guid = ITunesHash58::readFirewireGuid(mountDir.absolutePath());
    }

    if (!guid.isEmpty()) {
        ITunesHash58::updateHash(newFile, guid);
    }

    // Escritura atómica en el disco del iPod
    QString tmpPath = dbPath + ".tmp";
    QFile outFile(tmpPath);
    if (!outFile.open(QIODevice::WriteOnly)) return false;
    outFile.write(newFile);
    outFile.flush();
#ifdef Q_OS_WIN
    _commit(outFile.handle());
#endif
    outFile.close();

    QFile::remove(dbPath);
    bool ok = QFile::rename(tmpPath, dbPath);

    // Forzar vaciado de buffers del volumen hacia el almacenamiento físico del iPod
    QFileInfo fi(dbPath);
    QDir mountDir = fi.dir();
    mountDir.cdUp();
    mountDir.cdUp();
    flushVolumeBuffers(mountDir.absolutePath());

    return ok;
}

bool ITunesDBReader::deletePlaylistsFromDatabase(const QString& dbPath, const QSet<int>& playlistIndices, const QString& firewireGuid) {
    if (playlistIndices.isEmpty()) return true;

    QFile file(dbPath);
    if (!file.open(QIODevice::ReadOnly)) return false;
    QByteArray data = file.readAll();
    file.close();

    if (data.size() < 100 || std::memcmp(data.constData(), "mhbd", 4) != 0) return false;

    QString bakPath = dbPath + ".bak";
    if (!QFile::exists(bakPath)) {
        QFile::copy(dbPath, bakPath);
    }

    uint32_t mhbdHdrLen = readLE32(data.constData() + 4);
    QByteArray newFile = data.left(static_cast<int>(mhbdHdrLen));

    int offset = static_cast<int>(mhbdHdrLen);
    const int totalSize = data.size();

    while (offset + 16 <= totalSize) {
        const char* tag = data.constData() + offset;
        if (std::memcmp(tag, "mhsd", 4) != 0) break;

        uint32_t hdrLen = readLE32(tag + 4);
        uint32_t totLen = readLE32(tag + 8);
        uint32_t dsType = readLE32(tag + 12);
        int childOffset = offset + static_cast<int>(hdrLen);

        if (dsType == 2 && childOffset + 12 <= totalSize) {
            const char* mhlpTag = data.constData() + childOffset;
            if (std::memcmp(mhlpTag, "mhlp", 4) == 0) {
                uint32_t mhlpHdrLen = readLE32(mhlpTag + 4);
                uint32_t numPlaylists = readLE32(mhlpTag + 8);

                QByteArray mhsdHdr = data.mid(offset, static_cast<int>(hdrLen));
                QByteArray mhlpHdr = data.mid(childOffset, static_cast<int>(mhlpHdrLen));
                QByteArray remainingHyps;

                int plOffset = childOffset + static_cast<int>(mhlpHdrLen);
                uint32_t keptCount = 0;

                for (uint32_t p = 0; p < numPlaylists && plOffset + 24 <= totalSize; ++p) {
                    const char* mhypTag = data.constData() + plOffset;
                    if (std::memcmp(mhypTag, "mhyp", 4) != 0) break;
                    uint32_t plTotLen = readLE32(mhypTag + 8);

                    if (p != 0 && playlistIndices.contains(static_cast<int>(p))) {
                        // Omitir esta playlist seleccionada
                    } else {
                        remainingHyps.append(data.mid(plOffset, static_cast<int>(plTotLen)));
                        keptCount++;
                    }

                    if (plTotLen == 0) break;
                    plOffset += static_cast<int>(plTotLen);
                }

                writeLE32(mhlpHdr.data() + 8, keptCount);

                QByteArray dsPayload = mhlpHdr + remainingHyps;
                uint32_t newDsTotLen = static_cast<uint32_t>(mhsdHdr.size() + dsPayload.size());
                writeLE32(mhsdHdr.data() + 8, newDsTotLen);

                newFile.append(mhsdHdr + dsPayload);
            } else {
                newFile.append(data.mid(offset, static_cast<int>(totLen)));
            }
        } else {
            newFile.append(data.mid(offset, static_cast<int>(totLen)));
        }

        if (totLen == 0) break;
        offset += static_cast<int>(totLen);
    }

    writeLE32(newFile.data() + 8, static_cast<uint32_t>(newFile.size()));

    if (!firewireGuid.isEmpty()) {
        ITunesHash58::updateHash(newFile, firewireGuid);
    }

    QString tmpPath = dbPath + ".tmp";
    QFile tmpFile(tmpPath);
    if (!tmpFile.open(QIODevice::WriteOnly)) return false;
    tmpFile.write(newFile);
    tmpFile.flush();
    tmpFile.close();

    QFile::remove(dbPath);
    bool ok = QFile::rename(tmpPath, dbPath);

    QFileInfo fi(dbPath);
    QDir mountDir = fi.dir();
    mountDir.cdUp();
    mountDir.cdUp();
    flushVolumeBuffers(mountDir.absolutePath());

    return ok;
}

bool ITunesDBReader::renamePlaylistInDatabase(const QString& dbPath, int playlistIndex, const QString& newName, const QString& firewireGuid) {
    if (playlistIndex <= 0 || newName.trimmed().isEmpty()) return false;

    QFile file(dbPath);
    if (!file.open(QIODevice::ReadOnly)) return false;
    QByteArray data = file.readAll();
    file.close();

    if (data.size() < 100 || std::memcmp(data.constData(), "mhbd", 4) != 0) return false;

    QString bakPath = dbPath + ".bak";
    if (!QFile::exists(bakPath)) {
        QFile::copy(dbPath, bakPath);
    }

    uint32_t mhbdHdrLen = readLE32(data.constData() + 4);
    QByteArray newFile = data.left(static_cast<int>(mhbdHdrLen));

    int offset = static_cast<int>(mhbdHdrLen);
    const int totalSize = data.size();

    while (offset + 16 <= totalSize) {
        const char* tag = data.constData() + offset;
        if (std::memcmp(tag, "mhsd", 4) != 0) break;

        uint32_t hdrLen = readLE32(tag + 4);
        uint32_t totLen = readLE32(tag + 8);
        uint32_t dsType = readLE32(tag + 12);
        int childOffset = offset + static_cast<int>(hdrLen);

        if (dsType == 2 && childOffset + 12 <= totalSize) {
            const char* mhlpTag = data.constData() + childOffset;
            if (std::memcmp(mhlpTag, "mhlp", 4) == 0) {
                uint32_t mhlpHdrLen = readLE32(mhlpTag + 4);
                uint32_t numPlaylists = readLE32(mhlpTag + 8);

                QByteArray mhsdHdr = data.mid(offset, static_cast<int>(hdrLen));
                QByteArray mhlpHdr = data.mid(childOffset, static_cast<int>(mhlpHdrLen));
                QByteArray remainingHyps;

                int plOffset = childOffset + static_cast<int>(mhlpHdrLen);

                for (uint32_t p = 0; p < numPlaylists && plOffset + 24 <= totalSize; ++p) {
                    const char* mhypTag = data.constData() + plOffset;
                    if (std::memcmp(mhypTag, "mhyp", 4) != 0) break;

                    uint32_t plHdrLen = readLE32(mhypTag + 4);
                    uint32_t plTotLen = readLE32(mhypTag + 8);
                    uint32_t numMhods = readLE32(mhypTag + 12);

                    if (static_cast<int>(p) == playlistIndex) {
                        QByteArray mhypHdr = data.mid(plOffset, static_cast<int>(plHdrLen));
                        int mhodOffset = plOffset + static_cast<int>(plHdrLen);
                        QByteArray newMhods;

                        bool titleReplaced = false;
                        for (uint32_t m = 0; m < numMhods && mhodOffset + 12 <= totalSize; ++m) {
                            const char* mhodTag = data.constData() + mhodOffset;
                            if (std::memcmp(mhodTag, "mhod", 4) != 0) break;
                            uint32_t mhodTotLen = readLE32(mhodTag + 8);
                            uint32_t mhodType = readLE32(mhodTag + 12);

                            if (mhodType == 1) {
                                newMhods.append(makeMhodString(1, newName.trimmed()));
                                titleReplaced = true;
                            } else {
                                newMhods.append(data.mid(mhodOffset, static_cast<int>(mhodTotLen)));
                            }

                            if (mhodTotLen == 0) break;
                            mhodOffset += static_cast<int>(mhodTotLen);
                        }

                        if (!titleReplaced) {
                            newMhods.prepend(makeMhodString(1, newName.trimmed()));
                            writeLE32(mhypHdr.data() + 12, numMhods + 1);
                        }

                        int hipsLen = static_cast<int>(plTotLen) - (mhodOffset - plOffset);
                        QByteArray hipsData = (hipsLen > 0) ? data.mid(mhodOffset, hipsLen) : QByteArray();

                        uint32_t newPlTotLen = static_cast<uint32_t>(mhypHdr.size() + newMhods.size() + hipsData.size());
                        writeLE32(mhypHdr.data() + 8, newPlTotLen);

                        remainingHyps.append(mhypHdr + newMhods + hipsData);
                    } else {
                        remainingHyps.append(data.mid(plOffset, static_cast<int>(plTotLen)));
                    }

                    if (plTotLen == 0) break;
                    plOffset += static_cast<int>(plTotLen);
                }

                QByteArray dsPayload = mhlpHdr + remainingHyps;
                uint32_t newDsTotLen = static_cast<uint32_t>(mhsdHdr.size() + dsPayload.size());
                writeLE32(mhsdHdr.data() + 8, newDsTotLen);

                newFile.append(mhsdHdr + dsPayload);
            } else {
                newFile.append(data.mid(offset, static_cast<int>(totLen)));
            }
        } else {
            newFile.append(data.mid(offset, static_cast<int>(totLen)));
        }

        if (totLen == 0) break;
        offset += static_cast<int>(totLen);
    }

    writeLE32(newFile.data() + 8, static_cast<uint32_t>(newFile.size()));

    if (!firewireGuid.isEmpty()) {
        ITunesHash58::updateHash(newFile, firewireGuid);
    }

    QString tmpPath = dbPath + ".tmp";
    QFile tmpFile(tmpPath);
    if (!tmpFile.open(QIODevice::WriteOnly)) return false;
    tmpFile.write(newFile);
    tmpFile.flush();
    tmpFile.close();

    QFile::remove(dbPath);
    bool ok = QFile::rename(tmpPath, dbPath);

    QFileInfo fi(dbPath);
    QDir mountDir = fi.dir();
    mountDir.cdUp();
    mountDir.cdUp();
    flushVolumeBuffers(mountDir.absolutePath());

    return ok;
}

bool ITunesDBReader::createPlaylistInDatabase(const QString& dbPath, const QString& name, const QString& firewireGuid) {
    if (name.trimmed().isEmpty()) return false;

    QFile file(dbPath);
    if (!file.open(QIODevice::ReadOnly)) return false;
    QByteArray data = file.readAll();
    file.close();

    if (data.size() < 100 || std::memcmp(data.constData(), "mhbd", 4) != 0) return false;

    QString bakPath = dbPath + ".bak";
    if (!QFile::exists(bakPath)) {
        QFile::copy(dbPath, bakPath);
    }

    uint32_t mhbdHdrLen = readLE32(data.constData() + 4);
    QByteArray newFile = data.left(static_cast<int>(mhbdHdrLen));

    int offset = static_cast<int>(mhbdHdrLen);
    const int totalSize = data.size();

    QByteArray titleMhod = makeMhodString(1, name.trimmed());
    uint32_t plHdrLen = 24;
    uint32_t plTotLen = plHdrLen + static_cast<uint32_t>(titleMhod.size());

    QByteArray newMhyp(static_cast<int>(plHdrLen), '\0');
    std::memcpy(newMhyp.data(), "mhyp", 4);
    writeLE32(newMhyp.data() + 4, plHdrLen);
    writeLE32(newMhyp.data() + 8, plTotLen);
    writeLE32(newMhyp.data() + 12, 1);
    writeLE32(newMhyp.data() + 16, 0);
    newMhyp.append(titleMhod);

    while (offset + 16 <= totalSize) {
        const char* tag = data.constData() + offset;
        if (std::memcmp(tag, "mhsd", 4) != 0) break;

        uint32_t hdrLen = readLE32(tag + 4);
        uint32_t totLen = readLE32(tag + 8);
        uint32_t dsType = readLE32(tag + 12);
        int childOffset = offset + static_cast<int>(hdrLen);

        if (dsType == 2 && childOffset + 12 <= totalSize) {
            const char* mhlpTag = data.constData() + childOffset;
            if (std::memcmp(mhlpTag, "mhlp", 4) == 0) {
                uint32_t mhlpHdrLen = readLE32(mhlpTag + 4);
                uint32_t numPlaylists = readLE32(mhlpTag + 8);

                QByteArray mhsdHdr = data.mid(offset, static_cast<int>(hdrLen));
                QByteArray mhlpHdr = data.mid(childOffset, static_cast<int>(mhlpHdrLen));
                writeLE32(mhlpHdr.data() + 8, numPlaylists + 1);

                int playlistsLen = static_cast<int>(totLen) - static_cast<int>(hdrLen) - static_cast<int>(mhlpHdrLen);
                QByteArray playlistsData = data.mid(childOffset + static_cast<int>(mhlpHdrLen), playlistsLen);
                playlistsData.append(newMhyp);

                QByteArray dsPayload = mhlpHdr + playlistsData;
                uint32_t newDsTotLen = static_cast<uint32_t>(mhsdHdr.size() + dsPayload.size());
                writeLE32(mhsdHdr.data() + 8, newDsTotLen);

                newFile.append(mhsdHdr + dsPayload);
            } else {
                newFile.append(data.mid(offset, static_cast<int>(totLen)));
            }
        } else {
            newFile.append(data.mid(offset, static_cast<int>(totLen)));
        }

        if (totLen == 0) break;
        offset += static_cast<int>(totLen);
    }

    writeLE32(newFile.data() + 8, static_cast<uint32_t>(newFile.size()));

    if (!firewireGuid.isEmpty()) {
        ITunesHash58::updateHash(newFile, firewireGuid);
    }

    QString tmpPath = dbPath + ".tmp";
    QFile tmpFile(tmpPath);
    if (!tmpFile.open(QIODevice::WriteOnly)) return false;
    tmpFile.write(newFile);
    tmpFile.flush();
    tmpFile.close();

    QFile::remove(dbPath);
    bool ok = QFile::rename(tmpPath, dbPath);

    QFileInfo fi(dbPath);
    QDir mountDir = fi.dir();
    mountDir.cdUp();
    mountDir.cdUp();
    flushVolumeBuffers(mountDir.absolutePath());

    return ok;
}

void ITunesDBReader::flushVolumeBuffers(const QString& mountPath) {
#ifdef Q_OS_WIN
    QString cleanMount = QDir::toNativeSeparators(mountPath);
    if (!cleanMount.isEmpty()) {
        QString driveLetter = cleanMount.left(1);
        QString volPath = QString("\\\\.\\%1:").arg(driveLetter);
        HANDLE hVol = CreateFileW(reinterpret_cast<LPCWSTR>(volPath.utf16()),
                                  GENERIC_READ | GENERIC_WRITE,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  NULL, OPEN_EXISTING, 0, NULL);
        if (hVol != INVALID_HANDLE_VALUE) {
            FlushFileBuffers(hVol);
            CloseHandle(hVol);
        }

        if (!cleanMount.endsWith('\\')) cleanMount += '\\';
        SHChangeNotify(SHCNE_UPDATEDIR, SHCNF_PATHW, cleanMount.utf16(), NULL);
    }
#else
    Q_UNUSED(mountPath);
    sync();
#endif
}

bool ITunesDBReader::read(QList<Playlist>& outPlaylists) {
    outPlaylists.clear();

    QDir dir(m_mountPath);
    const QStringList sqliteCandidates = {
        dir.filePath("iTunes_Control/iTunes/iTunes Library.itlp/Library.itdb"),
        dir.filePath("itunes_control/itunes/itunes library.itlp/library.itdb")
    };

    for (const QString& path : sqliteCandidates) {
        if (QFile::exists(path) && parseSqliteDatabase(path, outPlaylists))
            return true;
    }

    const QStringList binaryCandidates = {
        dir.filePath("iPod_Control/iTunes/iTunesDB"),
        dir.filePath("ipod_control/itunes/itunesdb")
    };

    for (const QString& path : binaryCandidates) {
        if (QFile::exists(path) && parseBinaryDatabase(path, outPlaylists))
            return true;
    }

    scanFilesFallback(outPlaylists);
    return !outPlaylists.isEmpty();
}

bool ITunesDBReader::parseBinaryDatabase(const QString& dbPath, QList<Playlist>& outPlaylists) {
    QFile file(dbPath);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    QByteArray data = file.readAll();
    file.close();

    if (data.size() < 100 || std::memcmp(data.constData(), "mhbd", 4) != 0) {
        return false;
    }

    uint32_t mhbdHdrLen = readLE32(data.constData() + 4);
    int offset = static_cast<int>(mhbdHdrLen);
    const int totalSize = data.size();

    QMap<uint32_t, Track> trackMap;

    while (offset + 16 <= totalSize) {
        const char* tag = data.constData() + offset;
        if (std::memcmp(tag, "mhsd", 4) != 0) break;

        uint32_t hdrLen = readLE32(tag + 4);
        uint32_t totalLen = readLE32(tag + 8);
        uint32_t dsType = readLE32(tag + 12);
        int childOffset = offset + static_cast<int>(hdrLen);

        // Dataset 1: Pistas (mhlt -> mhit)
        if (dsType == 1 && childOffset + 12 <= totalSize) {
            const char* mhltTag = data.constData() + childOffset;
            if (std::memcmp(mhltTag, "mhlt", 4) == 0) {
                uint32_t mhltHdr = readLE32(mhltTag + 4);
                uint32_t numTracks = readLE32(mhltTag + 8);
                int trackOffset = childOffset + static_cast<int>(mhltHdr);

                for (uint32_t t = 0; t < numTracks && trackOffset + 40 <= totalSize; ++t) {
                    const char* mhitTag = data.constData() + trackOffset;
                    if (std::memcmp(mhitTag, "mhit", 4) != 0) break;

                    uint32_t mhitHdrLen = readLE32(mhitTag + 4);
                    uint32_t mhitTotLen = readLE32(mhitTag + 8);
                    uint32_t numMhods = readLE32(mhitTag + 12);
                    uint32_t trackId = readLE32(mhitTag + 16);

                    Track track;
                    track.id = trackId;

                    char fourccBuf[5] = {0};
                    std::memcpy(fourccBuf, mhitTag + 24, 4);
                    QString fourcc = QString::fromLatin1(fourccBuf).trimmed();
                    track.fileType = fourcc.isEmpty() ? "MP3" : fourcc;

                    if (mhitHdrLen >= 60) {
                        track.sizeBytes = readLE32(mhitTag + 36);
                        track.durationMs = static_cast<int>(readLE32(mhitTag + 40));
                        track.bitrate = static_cast<int>(readLE32(mhitTag + 56));
                    }

                    int mhodOffset = trackOffset + static_cast<int>(mhitHdrLen);
                    for (uint32_t m = 0; m < numMhods && mhodOffset + 40 <= totalSize; ++m) {
                        const char* mhodTag = data.constData() + mhodOffset;
                        if (std::memcmp(mhodTag, "mhod", 4) != 0) break;

                        uint32_t mhodTotLen = readLE32(mhodTag + 8);
                        uint32_t mhodType = readLE32(mhodTag + 12);

                        QString val = parseMhodString(data, mhodOffset, static_cast<int>(mhodTotLen));
                        switch (mhodType) {
                            case 1: track.title = val; break;
                            case 2: track.filePath = val; break;
                            case 3: track.album = val; break;
                            case 4: track.artist = val; break;
                            case 5: track.genre = val; break;
                            case 6: if (!val.isEmpty()) track.fileType = val; break;
                            default: break;
                        }

                        if (mhodTotLen == 0) break;
                        mhodOffset += static_cast<int>(mhodTotLen);
                    }

                    if (track.title.isEmpty()) {
                        track.title = QString("Pista #%1").arg(track.id);
                    }
                    if (track.artist.isEmpty()) {
                        track.artist = "Artista Desconocido";
                    }
                    if (track.album.isEmpty()) {
                        track.album = "Álbum Desconocido";
                    }

                    trackMap[trackId] = track;
                    if (mhitTotLen == 0) break;
                    trackOffset += static_cast<int>(mhitTotLen);
                }
            }
        }
        // Dataset 2: Playlists (mhlp -> mhyp -> mhip)
        else if (dsType == 2 && childOffset + 12 <= totalSize) {
            const char* mhlpTag = data.constData() + childOffset;
            if (std::memcmp(mhlpTag, "mhlp", 4) == 0) {
                uint32_t mhlpHdr = readLE32(mhlpTag + 4);
                uint32_t numPlaylists = readLE32(mhlpTag + 8);
                int plOffset = childOffset + static_cast<int>(mhlpHdr);

                for (uint32_t p = 0; p < numPlaylists && plOffset + 24 <= totalSize; ++p) {
                    const char* mhypTag = data.constData() + plOffset;
                    if (std::memcmp(mhypTag, "mhyp", 4) != 0) break;

                    uint32_t plHdrLen = readLE32(mhypTag + 4);
                    uint32_t plTotLen = readLE32(mhypTag + 8);
                    uint32_t numMhods = readLE32(mhypTag + 12);
                    uint32_t numSongs = readLE32(mhypTag + 16);

                    Playlist pl;
                    pl.id = p + 1;
                    pl.isMaster = (p == 0);

                    int mhodOffset = plOffset + static_cast<int>(plHdrLen);
                    for (uint32_t m = 0; m < numMhods && mhodOffset + 40 <= totalSize; ++m) {
                        const char* mhodTag = data.constData() + mhodOffset;
                        if (std::memcmp(mhodTag, "mhod", 4) != 0) break;
                        uint32_t mhodTotLen = readLE32(mhodTag + 8);
                        uint32_t mhodType = readLE32(mhodTag + 12);

                        if (mhodType == 1) {
                            pl.name = parseMhodString(data, mhodOffset, static_cast<int>(mhodTotLen));
                        }
                        if (mhodTotLen == 0) break;
                        mhodOffset += static_cast<int>(mhodTotLen);
                    }

                    int hipOffset = mhodOffset;
                    for (uint32_t s = 0; s < numSongs && hipOffset + 28 <= totalSize; ++s) {
                        const char* mhipTag = data.constData() + hipOffset;
                        if (std::memcmp(mhipTag, "mhip", 4) != 0) break;

                        uint32_t hTotLen = readLE32(mhipTag + 8);
                        uint32_t trackUid = readLE32(mhipTag + 24);

                        if (trackMap.contains(trackUid)) {
                            pl.tracks.append(trackMap[trackUid]);
                        }

                        if (hTotLen == 0) break;
                        hipOffset += static_cast<int>(hTotLen);
                    }

                    outPlaylists.append(pl);
                    if (plTotLen == 0) break;
                    plOffset += static_cast<int>(plTotLen);
                }
            }
        }

        if (totalLen == 0) break;
        offset += static_cast<int>(totalLen);
    }

    return !outPlaylists.isEmpty();
}


bool ITunesDBReader::parseSqliteDatabase(
    const QString& dbPath, QList<Playlist>& outPlaylists)
{
    sqlite3* db = nullptr;
    const QByteArray pathBytes = QFile::encodeName(dbPath);

    if (sqlite3_open_v2(pathBytes.constData(), &db,
                        SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        if (db) sqlite3_close(db);
        return false;
    }

    auto closeDb = [&]() { sqlite3_close(db); };
    auto textColumn = [](sqlite3_stmt* s, int col) -> QString {
        const auto* value = sqlite3_column_text(s, col);
        return value ? QString::fromUtf8(
            reinterpret_cast<const char*>(value)) : QString();
    };

    QHash<qint64, Track> tracksByPid;
    QList<qint64> trackOrder;
    sqlite3_stmt* stmt = nullptr;

    const char* tracksSql =
        "SELECT i.pid, i.title, i.artist, i.album, i.genre_id, "
        "i.total_time_ms, i.track_number, i.year, "
        "a.bit_rate, a.sample_rate "
        "FROM item i "
        "LEFT JOIN avformat_info a ON a.item_pid=i.pid "
        "WHERE i.is_song=1 ORDER BY i.title COLLATE NOCASE";

    if (sqlite3_prepare_v2(db, tracksSql, -1, &stmt, nullptr) != SQLITE_OK) {
        closeDb();
        return false;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const qint64 pid = sqlite3_column_int64(stmt, 0);
        Track track;
        track.id = static_cast<uint32_t>(trackOrder.size() + 1);

        const QString title = textColumn(stmt, 1).trimmed();
        const QString artist = textColumn(stmt, 2).trimmed();
        const QString album = textColumn(stmt, 3).trimmed();

        track.title = title.isEmpty() ? QStringLiteral("Título desconocido") : title;
        track.artist = artist.isEmpty() ? QStringLiteral("Artista desconocido") : artist;
        track.album = album.isEmpty() ? QStringLiteral("Álbum desconocido") : album;
        track.durationMs = static_cast<int>(sqlite3_column_double(stmt, 5));
        track.trackNumber = sqlite3_column_int(stmt, 6);
        const int year = sqlite3_column_int(stmt, 7);
        if (year > 0) track.year = year;

        const int bitrate = sqlite3_column_int(stmt, 8);
        if (bitrate > 0) track.bitrate = bitrate;

        const double sampleRate = sqlite3_column_double(stmt, 9);
        if (sampleRate > 0) track.sampleRate = static_cast<int>(sampleRate);

        // La ruta física aún no se conoce; no inventamos una.
        track.filePath.clear();

        tracksByPid.insert(pid, track);
        trackOrder.append(pid);
    }
    sqlite3_finalize(stmt);
    stmt = nullptr;

    if (tracksByPid.isEmpty()) {
        closeDb();
        return false;
    }

    QHash<qint64, int> playlistIndexes;
    const char* containersSql =
        "SELECT pid, name FROM container ORDER BY name COLLATE NOCASE";

    if (sqlite3_prepare_v2(db, containersSql, -1, &stmt, nullptr) != SQLITE_OK) {
        closeDb();
        return false;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const qint64 pid = sqlite3_column_int64(stmt, 0);
        const QString name = textColumn(stmt, 1).trimmed();
        if (name.isEmpty()) continue;

        Playlist playlist;
        playlist.id = static_cast<uint32_t>(outPlaylists.size() + 1);
        playlist.name = name;
        playlist.isMaster = name.compare(
            QStringLiteral("iPod de Zacky"), Qt::CaseInsensitive) == 0;

        playlistIndexes.insert(pid, outPlaylists.size());
        outPlaylists.append(playlist);
    }
    sqlite3_finalize(stmt);
    stmt = nullptr;

    const char* membershipSql =
        "SELECT container_pid, item_pid "
        "FROM item_to_container ORDER BY container_pid, physical_order";

    if (sqlite3_prepare_v2(db, membershipSql, -1, &stmt, nullptr) != SQLITE_OK) {
        outPlaylists.clear();
        closeDb();
        return false;
    }

    QSet<qint64> assignedMasterTracks;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const qint64 containerPid = sqlite3_column_int64(stmt, 0);
        const qint64 itemPid = sqlite3_column_int64(stmt, 1);

        if (!tracksByPid.contains(itemPid) ||
            !playlistIndexes.contains(containerPid)) continue;

        const int index = playlistIndexes.value(containerPid);
        outPlaylists[index].tracks.append(tracksByPid.value(itemPid));

        if (outPlaylists[index].isMaster)
            assignedMasterTracks.insert(itemPid);
    }
    sqlite3_finalize(stmt);
    closeDb();

    // Si la biblioteca no tiene una lista maestra reconocible, conservamos
    // todas las canciones en una lista de biblioteca, sin duplicarlas.
    bool hasMaster = false;
    for (const Playlist& p : outPlaylists)
        if (p.isMaster) { hasMaster = true; break; }

    if (!hasMaster) {
        Playlist master;
        master.id = 0;
        master.name = QStringLiteral("Toda la música");
        master.isMaster = true;
        for (qint64 pid : trackOrder)
            master.tracks.append(tracksByPid.value(pid));
        outPlaylists.prepend(master);
    }

    return !outPlaylists.isEmpty();
}

void ITunesDBReader::scanFilesFallback(QList<Playlist>& outPlaylists) {
    Playlist library;
    library.id = 1;
    library.name = "Música (Almacenamiento)";
    library.isMaster = true;

    QDir musicDir(m_mountPath + "/iPod_Control/Music");
    if (!musicDir.exists()) {
        musicDir.setPath(m_mountPath + "/ipod_control/music");
    }

    uint32_t idCounter = 1;
    if (musicDir.exists()) {
        QDirIterator it(musicDir.absolutePath(), {"*.mp3", "*.m4a", "*.aac", "*.wav", "*.aiff"}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            QFileInfo fi = it.fileInfo();

            Track t;
            t.id = idCounter++;
            t.title = fi.baseName();
            t.artist = "Desconocido";
            t.album = fi.dir().dirName();
            t.filePath = fi.absoluteFilePath();
            t.sizeBytes = static_cast<uint64_t>(fi.size());
            t.fileType = fi.suffix().toUpper();
            t.durationMs = 210000;
            t.bitrate = 256;

            library.tracks.append(t);
        }
    }

    outPlaylists.append(library);
}
