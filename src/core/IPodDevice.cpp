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
#include <QSettings>

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

    // Curva electroquímica real de celda Li-Po (LiCoO2) para iPod
    double percentageToVoltage(double pct, double maxV = 4.20, double minV = 3.35) {
        pct = qBound(0.0, pct, 100.0);
        if (pct >= 90.0) {
            return 4.10 + ((pct - 90.0) / 10.0) * (maxV - 4.10);
        } else if (pct >= 20.0) {
            return 3.64 + ((pct - 20.0) / 70.0) * (4.10 - 3.64);
        } else {
            return minV + (pct / 20.0) * (3.64 - minV);
        }
    }

    double voltageToPercentage(double v, double maxV = 4.20, double minV = 3.35) {
        if (v >= maxV) return 100.0;
        if (v <= minV) return 0.0;
        if (v >= 4.10) {
            return 90.0 + ((v - 4.10) / (maxV - 4.10)) * 10.0;
        } else if (v >= 3.64) {
            return 20.0 + ((v - 3.64) / (4.10 - 3.64)) * 70.0;
        } else {
            return ((v - minV) / (3.64 - minV)) * 20.0;
        }
    }

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

int IPodDevice::defaultCapacityMahForModel(const QString& modelName, const QString& generation) {
    QString m = modelName.toUpper() + " " + generation.toUpper();
    // iPod Nano 3G ("conejillo de indias") -> 400 mAh exactos
    if (m.contains("NANO 3G") || m.contains("A978") || m.contains("B249") || m.contains("B261")) return 400;
    if (m.contains("NANO 1G")) return 290;
    if (m.contains("NANO 2G")) return 330;
    if (m.contains("NANO 4G") || m.contains("NANO 5G")) return 240;
    if (m.contains("NANO 6G")) return 105;
    if (m.contains("NANO 7G")) return 220;
    if (m.contains("MINI 1G")) return 400;
    if (m.contains("MINI 2G")) return 500;
    if (m.contains("VIDEO") || m.contains("5G")) {
        return (m.contains("60") || m.contains("80")) ? 850 : 450;
    }
    if (m.contains("CLASSIC")) {
        return (m.contains("160") && m.contains("THICK")) ? 850 : 650;
    }
    if (m.contains("SHUFFLE 1G")) return 220;
    if (m.contains("SHUFFLE 2G")) return 120;
    if (m.contains("SHUFFLE 3G")) return 73;
    if (m.contains("SHUFFLE 4G")) return 51;
    if (m.contains("3G")) return 630;
    if (m.contains("4G") || m.contains("PHOTO") || m.contains("COLOR")) return 750;
    if (m.contains("1G") || m.contains("2G")) return 1200;
    return 400; // Por defecto celda Li-Po de 400 mAh
}

void IPodDevice::loadBatteryStateFromDisk() {
    int defCap = defaultCapacityMahForModel(m_modelName, m_generation);
    m_battery.available = true;
    m_battery.maxCapacityMah = defCap;
    m_battery.maxVoltage = 4.20;
    m_battery.minVoltage = 3.35;
    m_battery.isCharging = true;

    QDir dir(m_mountPath);
    QString battPath = dir.filePath("iPod_Control/Device/.ipop_battery");
    if (!QFile::exists(battPath)) {
        battPath = dir.filePath("ipod_control/device/.ipop_battery");
    }

    if (QFile::exists(battPath)) {
        QSettings settings(battPath, QSettings::IniFormat);
        m_battery.maxCapacityMah = settings.value("nominalCapacityMah", defCap).toInt();
        m_battery.maxVoltage = settings.value("maxVoltage", 4.20).toDouble();
        m_battery.minVoltage = settings.value("minVoltage", 3.35).toDouble();
        m_battery.voltage = settings.value("currentVoltage", 4.20).toDouble();
        m_battery.currentMah = settings.value("currentMah", m_battery.maxCapacityMah).toInt();
        m_battery.percentage = qBound(0, static_cast<int>((m_battery.currentMah * 100.0) / m_battery.maxCapacityMah), 100);
        m_battery.isProtectionCutoff = (m_battery.percentage >= 100 || m_battery.voltage >= m_battery.maxVoltage);
    } else {
        // Inicialización predeterminada de fábrica: 100% de carga, 4.20 V, corte de protección activo
        m_battery.voltage = 4.20;
        m_battery.currentMah = defCap;
        m_battery.percentage = 100;
        m_battery.isProtectionCutoff = true;
        saveBatteryStateToDisk();
    }

    if (m_battery.isProtectionCutoff) {
        m_battery.stateString = QString("%1 V (100% • %2 mAh) - Carga completa (Corte activo)")
            .arg(m_battery.voltage, 0, 'f', 2)
            .arg(m_battery.maxCapacityMah);
    } else {
        m_battery.stateString = QString("%1 V (%2% • %3/%4 mAh) - Cargando USB")
            .arg(m_battery.voltage, 0, 'f', 2)
            .arg(m_battery.percentage)
            .arg(m_battery.currentMah)
            .arg(m_battery.maxCapacityMah);
    }
}

void IPodDevice::saveBatteryStateToDisk() {
    if (!m_isValid || m_mountPath.isEmpty()) return;
    QDir dir(m_mountPath);
    QString devDir = dir.filePath("iPod_Control/Device");
    if (!QDir(devDir).exists()) {
        devDir = dir.filePath("ipod_control/device");
    }
    if (!QDir(devDir).exists()) return;

    QString battPath = QDir(devDir).filePath(".ipop_battery");
    QSettings settings(battPath, QSettings::IniFormat);
    settings.setValue("nominalCapacityMah", m_battery.maxCapacityMah);
    settings.setValue("maxVoltage", m_battery.maxVoltage);
    settings.setValue("minVoltage", m_battery.minVoltage);
    settings.setValue("currentVoltage", m_battery.voltage);
    settings.setValue("currentMah", m_battery.currentMah);
    settings.setValue("isProtectionCutoff", m_battery.isProtectionCutoff);
    settings.sync();
}

void IPodDevice::refreshBatteryStatus() {
    loadBatteryStateFromDisk();
}

void IPodDevice::updateBatteryTelemetry(double deltaSeconds) {
    if (!m_isValid || !m_battery.available) return;

    if (m_battery.isCharging && m_battery.currentMah < m_battery.maxCapacityMah) {
        // Tasa de carga típica USB: ~250 mA
        double deltaMah = (250.0 / 3600.0) * deltaSeconds;
        double newMah = m_battery.currentMah + deltaMah;
        if (newMah >= m_battery.maxCapacityMah) {
            newMah = m_battery.maxCapacityMah;
            m_battery.isProtectionCutoff = true;
            m_battery.voltage = m_battery.maxVoltage;
            m_battery.percentage = 100;
        } else {
            m_battery.currentMah = static_cast<int>(newMah);
            m_battery.percentage = qBound(0, static_cast<int>((m_battery.currentMah * 100.0) / m_battery.maxCapacityMah), 100);
            m_battery.voltage = percentageToVoltage(m_battery.percentage, m_battery.maxVoltage, m_battery.minVoltage);
            m_battery.isProtectionCutoff = false;
        }
        m_battery.currentMah = static_cast<int>(newMah);
        saveBatteryStateToDisk();
    } else if (m_battery.percentage >= 100 || m_battery.voltage >= m_battery.maxVoltage) {
        m_battery.isProtectionCutoff = true;
        m_battery.voltage = m_battery.maxVoltage;
        m_battery.percentage = 100;
        m_battery.currentMah = m_battery.maxCapacityMah;
    }

    if (m_battery.isProtectionCutoff) {
        m_battery.stateString = QString("%1 V (100% • %2 mAh) - Carga completa (Corte 4.2V)")
            .arg(m_battery.voltage, 0, 'f', 2)
            .arg(m_battery.maxCapacityMah);
    } else {
        m_battery.stateString = QString("%1 V (%2% • %3/%4 mAh) - Cargando USB")
            .arg(m_battery.voltage, 0, 'f', 2)
            .arg(m_battery.percentage)
            .arg(m_battery.currentMah)
            .arg(m_battery.maxCapacityMah);
    }
}

void IPodDevice::calibrateBattery(double newVoltage, int newMah) {
    if (!m_battery.available) return;

    newVoltage = qBound(m_battery.minVoltage, newVoltage, m_battery.maxVoltage + 0.10);
    m_battery.voltage = newVoltage;

    if (newMah > 0) {
        m_battery.currentMah = qBound(0, newMah, m_battery.maxCapacityMah);
        m_battery.percentage = qBound(0, static_cast<int>((m_battery.currentMah * 100.0) / m_battery.maxCapacityMah), 100);
    } else {
        double pct = voltageToPercentage(newVoltage, m_battery.maxVoltage, m_battery.minVoltage);
        m_battery.percentage = qBound(0, static_cast<int>(pct + 0.5), 100);
        m_battery.currentMah = qBound(0, static_cast<int>((pct / 100.0) * m_battery.maxCapacityMah + 0.5), m_battery.maxCapacityMah);
    }

    m_battery.isProtectionCutoff = (m_battery.percentage >= 100 || m_battery.voltage >= m_battery.maxVoltage);
    saveBatteryStateToDisk();
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
