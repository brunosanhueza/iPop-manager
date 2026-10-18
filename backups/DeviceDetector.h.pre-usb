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

    // Comprueba si una ruta específica corresponde a un iPod válido
    static bool isIpodMount(const QString& path);

signals:
    void devicesRefreshed(const QStringList& ipodMounts);

public slots:
    void refresh();
};
