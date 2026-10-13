#pragma once

#include <QObject>
#include <QStringList>
#include <memory>
#include "IPodDevice.h"

class DeviceDetector : public QObject {
    Q_OBJECT

public:
    explicit DeviceDetector(QObject* parent = nullptr);

    // Escanea puntos de montaje del sistema y detecta iPods conectados
    QStringList detectDevices();

    // Comprueba si una ruta específica corresponde a un iPod válido (insensible a mayúsculas/minúsculas)
    static bool isIpodMount(const QString& path);

    // Busca un fichero o subdirectorio de forma insensible a mayúsculas/minúsculas
    static QString findCaseInsensitiveEntry(const QString& parentDir, const QString& targetName);

    // Resuelve una ruta multinivel de forma insensible a mayúsculas/minúsculas
    static QString resolveCaseInsensitivePath(const QString& basePath, const QStringList& subparts);

    // Rutas canónicas resueltas en disco respetando mayúsculas/minúsculas del sistema
    static QString resolveControlPath(const QString& mountPath);
    static QString resolveMusicPath(const QString& mountPath);
    static QString resolveDatabasePath(const QString& mountPath);
    static QString resolveSysInfoPath(const QString& mountPath);

    // Detección de dispositivos iPod conectados pero no montados en Linux
    struct UnmountedIpod {
        QString deviceNode; // ej. /dev/sdb2
        QString label;      // ej. IPOD
        QString fstype;     // ej. vfat
        QString model;      // ej. iPod
    };
    static QList<UnmountedIpod> detectUnmountedIpods();
    static bool mountDeviceNode(const QString& deviceNode, QString* outMountPoint = nullptr);

signals:
    void devicesRefreshed(const QStringList& ipodMounts);

public slots:
    void refresh();
};
