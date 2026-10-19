#pragma once

#include <QObject>
#include <QStringList>
#include "IPodDevice.h"

class DeviceDetector : public QObject {
    Q_OBJECT

public:
    explicit DeviceDetector(QObject* parent = nullptr);

    // Devuelve las rutas de iPods montados y accesibles.
    QStringList detectDevices();

    // Identifica iPods conectados físicamente por USB.
    QStringList detectUsbDevices();

    static bool isIpodMount(const QString& path);

    // Carpeta donde se monta (con ifuse) el iPod Touch: ~/.local/share/iPop/iPop/mount
    static QString touchMountPoint();

    // Si hay un iPod Touch por USB y no esta montado, lo monta con ifuse (empareja si hace falta).
    // Devuelve true si queda montado y accesible. Mensaje de error opcional.
    bool mountTouchIfNeeded(QString* errorMsg = nullptr);

signals:
    void devicesRefreshed(const QStringList& ipodMounts);
    void usbDevicesRefreshed(const QStringList& usbDevices);

public slots:
    void refresh();
};
