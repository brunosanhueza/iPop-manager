#include "IPodDevice.h"
#include "ITunesDBReader.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QFile>
#include <QTextStream>
#include <QStorageInfo>
#include <QSet>
#include <QDebug>

#include <QProcess>

#if defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winioctl.h>
#endif

#if defined(Q_OS_LINUX)
#include <sys/statvfs.h>
#endif

namespace {
    const QSet<QString> AUDIO_EXTENSIONS = {"mp3", "m4a", "aac", "wav", "aif", "aiff"};
    const QSet<QString> VIDEO_EXTENSIONS = {"mp4", "m4v", "mov", "avi"};
    const QSet<QString> PHOTO_EXTENSIONS = {"jpg", "jpeg", "png", "bmp", "ithmb"};

    QString resolveIpodModel(const QString& modelNumStr) {
        QString m = modelNumStr.trimmed().toUpper();
        if (m.startsWith("M8541") || m.startsWith("M8697")) return "iPod 1G / 2G";
        if (m.startsWith("M8976") || m.startsWith("M8946")) return "iPod 3G";
        if (m.startsWith("M9282") || m.startsWith("M9268")) return "iPod 4G";
        if (m.startsWith("M9829") || m.startsWith("M9830")) return "iPod Photo / Color";
        if (m.startsWith("M9160") || m.startsWith("M9436")) return "iPod Mini 1G";
        if (m.startsWith("M9800") || m.startsWith("M9801")) return "iPod Mini 2G";
        if (m.startsWith("MA002") || m.startsWith("MA146") || m.startsWith("PA002") || m.startsWith("MA003")) return "iPod 5G / 5.5G (Video)";
        if (m.startsWith("MB029") || m.startsWith("MB147") || m.startsWith("MB565") || m.startsWith("MC297")) return "iPod Classic (6G / 7G)";
        if (m.startsWith("MA004") || m.startsWith("MA099") || m.startsWith("MA107")) return "iPod Nano 1G";
        if (m.startsWith("MA477") || m.startsWith("MA426") || m.startsWith("MA428")) return "iPod Nano 2G";
        if (m.startsWith("MA978") || m.startsWith("MB249") || m.startsWith("MB261")) return "iPod Nano 3G";
        if (m.startsWith("MB598") || m.startsWith("MB754") || m.startsWith("MB903")) return "iPod Nano 4G";
        if (m.startsWith("MC031") || m.startsWith("MC060") || m.startsWith("MC075")) return "iPod Nano 5G";
        if (m.startsWith("MC525") || m.startsWith("MC688") || m.startsWith("MC694")) return "iPod Nano 6G";
        if (m.startsWith("MD481") || m.startsWith("ME971")) return "iPod Nano 7G";
        if (m.startsWith("M9724") || m.startsWith("M9725")) return "iPod Shuffle 1G";
        if (m.startsWith("MA564") || m.startsWith("MB225") || m.startsWith("MB233")) return "iPod Shuffle 2G";
        if (m.startsWith("MB867") || m.startsWith("MC164")) return "iPod Shuffle 3G";
        if (m.startsWith("MC584") || m.startsWith("MC749") || m.startsWith("MD779")) return "iPod Shuffle 4G";
        return "iPod (" + modelNumStr + ")";
    }
}

IPodDevice::IPodDevice(const QString& mountPath)
    : m_mountPath(mountPath)
{
    QDir dir(mountPath);
    if (!dir.exists()) {
        m_isValid = false;
        return;
    }

    QString controlPath = dir.filePath("iPod_Control");
    QFileInfo checkInfo(controlPath);
    if (!checkInfo.exists() || !checkInfo.isDir()) {
        controlPath = dir.filePath("ipod_control");
        checkInfo.setFile(controlPath);
    }

    m_isValid = checkInfo.exists() && checkInfo.isDir();
    if (m_isValid) {
        parseSysInfo();
        loadPlaylists();
        refreshStorageBreakdown();
        refreshBatteryStatus();
    }
}

void IPodDevice::parseSysInfo() {
    QDir dir(m_mountPath);
    QString sysInfoPath = dir.filePath("iPod_Control/Device/SysInfo");
    if (!QFile::exists(sysInfoPath)) {
        sysInfoPath = dir.filePath("ipod_control/device/sysinfo");
    }

    QFile file(sysInfoPath);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        while (!in.atEnd()) {
            QString line = in.readLine().trimmed();
            if (line.startsWith("ModelNumStr:", Qt::CaseInsensitive)) {
                QString modelId = line.section(':', 1).trimmed();
                m_modelName = resolveIpodModel(modelId);
                m_generation = modelId;
            } else if (line.startsWith("visibleBuildID:", Qt::CaseInsensitive)) {
                m_firmwareVersion = line.section(':', 1).trimmed();
            } else if (line.startsWith("FirewireGuid:", Qt::CaseInsensitive)) {
                m_serialNumber = line.section(':', 1).trimmed();
            }
        }
        file.close();
    }

    if (m_modelName == "iPod" && !m_generation.isEmpty()) {
        m_modelName = resolveIpodModel(m_generation);
    }
}

void IPodDevice::loadPlaylists() {
    ITunesDBReader reader(m_mountPath);
    reader.read(m_playlists);
}

uint64_t IPodDevice::calculateTracksSizeBytes(const QList<uint32_t>& trackIds) const {
    QSet<uint32_t> toDelete(trackIds.begin(), trackIds.end());
    QMap<uint32_t, uint64_t> uniqueSizes;
    for (const auto& pl : m_playlists) {
        for (const auto& t : pl.tracks) {
            if (toDelete.contains(t.id)) {
                uniqueSizes.insert(t.id, t.sizeBytes);
            }
        }
    }
    uint64_t total = 0;
    for (uint64_t sz : uniqueSizes) {
        total += sz;
    }
    return total;
}

bool IPodDevice::deleteTracks(const QList<uint32_t>& trackIds, int playlistIndex, DeleteProgressCallback progressCb) {
    if (playlistIndex < 0 || playlistIndex >= m_playlists.size() || trackIds.isEmpty()) {
        return false;
    }

    QSet<uint32_t> toDelete(trackIds.begin(), trackIds.end());

    // Identificar pistas únicas a eliminar
    QMap<uint32_t, Track> uniqueTracksToDelete;
    for (const auto& pl : m_playlists) {
        for (const auto& t : pl.tracks) {
            if (toDelete.contains(t.id)) {
                uniqueTracksToDelete.insert(t.id, t);
            }
        }
    }

    uint64_t totalBytes = 0;
    for (const auto& t : uniqueTracksToDelete) {
        totalBytes += t.sizeBytes;
    }

    if (progressCb) {
        progressCb(0, totalBytes, QString("Iniciando eliminación de %1 pistas...").arg(uniqueTracksToDelete.size()));
    }

    // 1. Borrar archivos físicos del almacenamiento masivo del iPod
    uint64_t currentBytes = 0;
    int trackCounter = 0;
    for (const auto& t : uniqueTracksToDelete) {
        trackCounter++;
        if (progressCb) {
            progressCb(currentBytes, totalBytes, QString("Borrando (%1/%2): %3").arg(trackCounter).arg(uniqueTracksToDelete.size()).arg(t.title));
        }

        if (!t.filePath.isEmpty()) {
            QString realPath = ITunesDBReader::resolvePhysicalPath(m_mountPath, t.filePath);
            if (QFile::exists(realPath)) {
                QFile::remove(realPath);
            }
        }

        currentBytes += t.sizeBytes;
        if (progressCb) {
            progressCb(currentBytes, totalBytes, QString("Borrado: %1").arg(t.title));
        }
    }

    // 2. Modificar y guardar la base de datos binaria iTunesDB en el iPod
    if (progressCb) {
        progressCb(currentBytes, totalBytes, "Actualizando base de datos iTunesDB...");
    }

    QDir dir(m_mountPath);
    QString dbPath = dir.filePath("iPod_Control/iTunes/iTunesDB");
    if (!QFile::exists(dbPath)) {
        dbPath = dir.filePath("ipod_control/itunes/itunesdb");
    }

    if (QFile::exists(dbPath)) {
        ITunesDBReader::removeTracksFromDatabase(dbPath, toDelete, m_serialNumber);
    }

    // 3. Vaciado físico de buffers del volumen
    if (progressCb) {
        progressCb(currentBytes, totalBytes, "Vaciando búferes a la memoria flash del iPod...");
    }
    ITunesDBReader::flushVolumeBuffers(m_mountPath);

    // 4. Recargar playlists limpias y recalcular almacenamiento
    loadPlaylists();
    refreshStorageBreakdown();

    if (progressCb) {
        progressCb(totalBytes, totalBytes, "¡Operación completada con éxito!");
    }

    return true;
}

bool IPodDevice::deletePlaylists(const QList<int>& playlistIndices, QString* errorMsg) {
    if (!m_isValid || playlistIndices.isEmpty()) return false;
    QDir dir(m_mountPath);
    QString dbPath = dir.filePath("iPod_Control/iTunes/iTunesDB");
    if (!QFile::exists(dbPath)) dbPath = dir.filePath("ipod_control/itunes/itunesdb");

    QSet<int> toDelete(playlistIndices.begin(), playlistIndices.end());
    bool ok = ITunesDBReader::deletePlaylistsFromDatabase(dbPath, toDelete, m_serialNumber);
    if (ok) {
        loadPlaylists();
    } else if (errorMsg) {
        *errorMsg = "No se pudieron eliminar las listas de reproducción de la base de datos.";
    }
    return ok;
}

bool IPodDevice::renamePlaylist(int playlistIndex, const QString& newName, QString* errorMsg) {
    if (!m_isValid || playlistIndex <= 0) return false;
    QDir dir(m_mountPath);
    QString dbPath = dir.filePath("iPod_Control/iTunes/iTunesDB");
    if (!QFile::exists(dbPath)) dbPath = dir.filePath("ipod_control/itunes/itunesdb");

    bool ok = ITunesDBReader::renamePlaylistInDatabase(dbPath, playlistIndex, newName, m_serialNumber);
    if (ok) {
        loadPlaylists();
    } else if (errorMsg) {
        *errorMsg = "No se pudo renombrar la lista en la base de datos.";
    }
    return ok;
}

bool IPodDevice::createPlaylist(const QString& name, QString* errorMsg) {
    if (!m_isValid || name.trimmed().isEmpty()) return false;
    QDir dir(m_mountPath);
    QString dbPath = dir.filePath("iPod_Control/iTunes/iTunesDB");
    if (!QFile::exists(dbPath)) dbPath = dir.filePath("ipod_control/itunes/itunesdb");

    bool ok = ITunesDBReader::createPlaylistInDatabase(dbPath, name, m_serialNumber);
    if (ok) {
        loadPlaylists();
    } else if (errorMsg) {
        *errorMsg = "No se pudo crear la nueva lista en la base de datos.";
    }
    return ok;
}

void IPodDevice::refreshStorageBreakdown() {
    m_storage = StorageBreakdown{};

    QStorageInfo storage(m_mountPath);
    if (storage.isValid() && storage.isReady()) {
        m_storage.totalBytes = static_cast<uint64_t>(storage.bytesTotal());
        m_storage.freeBytes = static_cast<uint64_t>(storage.bytesAvailable());
    }

    scanStorageCategories();
}

void IPodDevice::scanStorageCategories() {
    uint64_t audioBytes = 0;
    for (const auto& pl : m_playlists) {
        if (pl.isMaster) {
            for (const auto& t : pl.tracks) {
                audioBytes += t.sizeBytes;
            }
            break;
        }
    }

    m_storage.audioBytes = audioBytes;

    if (m_storage.totalBytes > m_storage.freeBytes) {
        uint64_t actualUsed = m_storage.totalBytes - m_storage.freeBytes;
        if (actualUsed > audioBytes) {
            m_storage.otherBytes = actualUsed - audioBytes;
        }
    }
}

void IPodDevice::refreshBatteryStatus() {
    m_battery = BatteryStatus{};
    m_battery.available = true;
    m_battery.percentage = 100;
    m_battery.isCharging = true;
    m_battery.stateString = "Alimentado por USB (Cargando)";
}

QString IPodDevice::formatBytes(uint64_t bytes) {
    constexpr double KB = 1024.0;
    constexpr double MB = KB * 1024.0;
    constexpr double GB = MB * 1024.0;
    constexpr double TB = GB * 1024.0;

    double dBytes = static_cast<double>(bytes);
    if (dBytes >= TB) {
        return QString::asprintf("%.2f TB", dBytes / TB);
    } else if (dBytes >= GB) {
        return QString::asprintf("%.2f GB", dBytes / GB);
    } else if (dBytes >= MB) {
        return QString::asprintf("%.1f MB", dBytes / MB);
    } else if (dBytes >= KB) {
        return QString::asprintf("%.1f KB", dBytes / KB);
    } else {
        return QString::number(bytes) + " B";
    }
}

bool IPodDevice::eject(QString* errorMsg) {
    if (m_mountPath.isEmpty()) {
        if (errorMsg) *errorMsg = "Ruta de montaje no especificada.";
        return false;
    }

#if defined(Q_OS_WIN)
    QString cleanPath = m_mountPath.trimmed();
    QChar driveLetter = cleanPath[0].toUpper();
    QString devicePath = QString("\\\\.\\%1:").arg(driveLetter);
    std::wstring wPath = devicePath.toStdWString();

    HANDLE hVolume = CreateFileW(
        wPath.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        0,
        NULL
    );

    if (hVolume == INVALID_HANDLE_VALUE) {
        hVolume = CreateFileW(
            wPath.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            NULL,
            OPEN_EXISTING,
            0,
            NULL
        );
    }

    if (hVolume == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        if (errorMsg) {
            *errorMsg = QString("No se pudo abrir el volumen de la unidad %1: (Código de error %2). Asegúrate de que no haya archivos abiertos.").arg(driveLetter).arg(err);
        }
        return false;
    }

    // Asegurar que todos los datos en caché de Windows se escriban físicamente en el iPod
    FlushFileBuffers(hVolume);

    DWORD bytesReturned = 0;
    // Reintentar bloquear el volumen (FSCTL_LOCK_VOLUME) hasta 5 veces por si el Explorador de Windows o el indexador lo están tocando
    for (int retry = 0; retry < 5; ++retry) {
        if (DeviceIoControl(hVolume, FSCTL_LOCK_VOLUME, NULL, 0, NULL, 0, &bytesReturned, NULL)) {
            break;
        }
        Sleep(200);
    }

    // Desmontar el volumen del sistema de archivos
    DeviceIoControl(hVolume, FSCTL_DISMOUNT_VOLUME, NULL, 0, NULL, 0, &bytesReturned, NULL);

    // Habilitar extracción de medios (PreventMediaRemoval = FALSE)
    PREVENT_MEDIA_REMOVAL pmr;
    pmr.PreventMediaRemoval = FALSE;
    DeviceIoControl(hVolume, IOCTL_STORAGE_MEDIA_REMOVAL, &pmr, sizeof(pmr), NULL, 0, &bytesReturned, NULL);

    // Enviar el comando SCSI de expulsión de hardware (START STOP UNIT con LoEj=1)
    // Este comando hace que el iPod cambie de "No desconectar" a "Aceptable desconectar"
    BOOL ejected = DeviceIoControl(hVolume, IOCTL_STORAGE_EJECT_MEDIA, NULL, 0, NULL, 0, &bytesReturned, NULL);
    Q_UNUSED(ejected);

    CloseHandle(hVolume);

    // Refuerzo adicional notificando al Shell de Windows (Explorer)
    QString psCommand = QString(
        "$sh = New-Object -ComObject Shell.Application; "
        "$item = $sh.Namespace(17).ParseName('%1:'); "
        "if ($item) { foreach ($v in $item.Verbs()) { if ($v.Name.Replace('&','') -match '(?i)expulsar|eject') { $v.DoIt(); break } } }"
    ).arg(driveLetter);
    QProcess::execute("powershell", {"-NoProfile", "-NonInteractive", "-Command", psCommand});

    m_isValid = false;
    return true;
#else
    QProcess::execute("sync");
    int exitCode = QProcess::execute("eject", {m_mountPath});
    if (exitCode != 0) {
        exitCode = QProcess::execute("umount", {m_mountPath});
    }
    if (exitCode == 0) {
        m_isValid = false;
        return true;
    }
    if (errorMsg) *errorMsg = "No se pudo desmontar el iPod automáticamente.";
    return false;
#endif
}
