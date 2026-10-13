#pragma once

#include <QString>
#include <QDateTime>
#include <QMap>
#include <QList>
#include <cstdint>
#include <functional>
#include "Track.h"
#include "Playlist.h"

using DeleteProgressCallback = std::function<void(uint64_t currentBytes, uint64_t totalBytes, const QString& statusText)>;

enum class FileCategory {
    Audio,
    Video,
    Podcasts,
    Photos,
    Other,
    Free
};

struct StorageBreakdown {
    uint64_t totalBytes{0};
    uint64_t freeBytes{0};
    uint64_t audioBytes{0};
    uint64_t videoBytes{0};
    uint64_t podcastBytes{0};
    uint64_t photoBytes{0};
    uint64_t otherBytes{0};

    double audioPercent() const { return totalBytes > 0 ? (audioBytes * 100.0 / totalBytes) : 0.0; }
    double videoPercent() const { return totalBytes > 0 ? (videoBytes * 100.0 / totalBytes) : 0.0; }
    double podcastPercent() const { return totalBytes > 0 ? (podcastBytes * 100.0 / totalBytes) : 0.0; }
    double photoPercent() const { return totalBytes > 0 ? (photoBytes * 100.0 / totalBytes) : 0.0; }
    double otherPercent() const { return totalBytes > 0 ? (otherBytes * 100.0 / totalBytes) : 0.0; }
    double freePercent() const { return totalBytes > 0 ? (freeBytes * 100.0 / totalBytes) : 0.0; }
};

struct BatteryStatus {
    bool available{false};
    int percentage{100};            // 0 - 100%
    double voltage{4.20};           // Voltaje actual de la celda (ej. 4.20 V)
    double maxVoltage{4.20};        // Voltaje máximo de corte (4.20 V)
    double minVoltage{3.35};        // Voltaje mínimo de corte seguro (3.35 V)
    int currentMah{400};            // Capacidad actual en mAh
    int maxCapacityMah{400};        // Capacidad nominal original en mAh
    bool isCharging{true};          // Alimentado por USB
    bool isProtectionCutoff{true};  // Corte de protección activo al 100%
    QString stateString{"4.20 V (100% • 400 mAh)"};

    QString shortLabel() const {
        if (!available) return "Desconectado";
        return QString("%1%2 V (%3% • %4 mAh)")
            .arg(isCharging ? "⚡ " : "")
            .arg(voltage, 0, 'f', 2)
            .arg(percentage)
            .arg(currentMah);
    }
};

class IPodDevice {
public:
    explicit IPodDevice(const QString& mountPath);

    bool isValid() const { return m_isValid; }
    QString mountPath() const { return m_mountPath; }
    QString modelName() const { return m_modelName; }
    QString generation() const { return m_generation; }
    QString serialNumber() const { return m_serialNumber; }
    QString firmwareVersion() const { return m_firmwareVersion; }
    
    // Almacenamiento y desglose
    StorageBreakdown storageBreakdown() const { return m_storage; }
    void refreshStorageBreakdown();

    // Batería
    BatteryStatus batteryStatus() const { return m_battery; }
    void refreshBatteryStatus();
    void updateBatteryTelemetry(double deltaSeconds);
    void calibrateBattery(double newVoltage, int newMah = -1);
    static int defaultCapacityMahForModel(const QString& modelName, const QString& generation);

    // Playlists y canciones
    QList<Playlist> playlists() const { return m_playlists; }
    void loadPlaylists();
    uint64_t calculateTracksSizeBytes(const QList<uint32_t>& trackIds) const;
    bool deleteTracks(const QList<uint32_t>& trackIds, int playlistIndex, DeleteProgressCallback progressCb = nullptr);

    bool deletePlaylists(const QList<int>& playlistIndices, QString* errorMsg = nullptr);
    bool renamePlaylist(int playlistIndex, const QString& newName, QString* errorMsg = nullptr);
    bool createPlaylist(const QString& name, QString* errorMsg = nullptr);

    // Expulsión segura
    bool eject(QString* errorMsg = nullptr);

    // Utilidades
    static QString formatBytes(uint64_t bytes);

private:
    void parseSysInfo();
    void scanStorageCategories();
    void loadBatteryStateFromDisk();
    void saveBatteryStateToDisk();

    QString m_mountPath;
    bool m_isValid{false};
    QString m_modelName{"iPod"};
    QString m_generation{"Desconocida"};
    QString m_serialNumber{""};
    QString m_firmwareVersion{""};

    StorageBreakdown m_storage;
    BatteryStatus m_battery;
    QList<Playlist> m_playlists;
};
