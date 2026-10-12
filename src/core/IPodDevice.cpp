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

bool IPodDevice::deleteTracks(const QList<uint32_t>& trackIds, int playlistIndex) {
    if (playlistIndex < 0 || playlistIndex >= m_playlists.size() || trackIds.isEmpty()) {
        return false;
    }

    QSet<uint32_t> toDelete(trackIds.begin(), trackIds.end());

    // 1. Borrar archivos físicos del almacenamiento masivo del iPod
    for (const auto& pl : m_playlists) {
        for (const auto& t : pl.tracks) {
            if (toDelete.contains(t.id) && !t.filePath.isEmpty()) {
                QString realPath = ITunesDBReader::resolvePhysicalPath(m_mountPath, t.filePath);
                if (QFile::exists(realPath)) {
                    QFile::remove(realPath);
                }
            }
        }
    }

    // 2. Modificar y guardar la base de datos binaria iTunesDB en el iPod
    QDir dir(m_mountPath);
    QString dbPath = dir.filePath("iPod_Control/iTunes/iTunesDB");
    if (!QFile::exists(dbPath)) {
        dbPath = dir.filePath("ipod_control/itunes/itunesdb");
    }

    if (QFile::exists(dbPath)) {
        ITunesDBReader::removeTracksFromDatabase(dbPath, toDelete, m_serialNumber);
    }

    ITunesDBReader::flushVolumeBuffers(m_mountPath);

    // 3. Recargar playlists limpias y recalcular almacenamiento
    loadPlaylists();
    refreshStorageBreakdown();
    return true;
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
