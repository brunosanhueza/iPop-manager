#include "DeviceDetector.h"

#include <QStorageInfo>
#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QSet>

DeviceDetector::DeviceDetector(QObject* parent)
    : QObject(parent)
{
}

bool DeviceDetector::isIpodMount(const QString& path) {
    if (path.isEmpty()) return false;
    QDir dir(path);
    if (!dir.exists()) return false;

    // Verificar si existe iPod_Control
    QString ctrl = dir.filePath("iPod_Control");
    if (QFileInfo::exists(ctrl) && QFileInfo(ctrl).isDir()) {
        return true;
    }
    // Caso de sistemas de archivos case-sensitive
    QString ctrlLower = dir.filePath("ipod_control");
    return (QFileInfo::exists(ctrlLower) && QFileInfo(ctrlLower).isDir());
}

QStringList DeviceDetector::detectDevices() {
    QSet<QString> detected;

    // 1. Escanear volúmenes montados reportados por Qt
    const auto volumes = QStorageInfo::mountedVolumes();
    for (const auto& vol : volumes) {
        if (vol.isValid() && vol.isReady()) {
            QString root = vol.rootPath();
            if (isIpodMount(root)) {
                detected.insert(root);
            }
        }
    }

    // 2. Escanear carpetas de medios típicas en Linux (/media y /run/media)
    QString user = QProcessEnvironment::systemEnvironment().value("USER");
    QStringList searchBases = {
        "/media",
        "/run/media",
        "/run/media/" + user,
        "/media/" + user,
        "/mnt"
    };

    for (const QString& base : searchBases) {
        QDir baseDir(base);
        if (baseDir.exists()) {
            const QStringList subdirs = baseDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
            for (const QString& sub : subdirs) {
                QString fullPath = baseDir.filePath(sub);
                if (isIpodMount(fullPath)) {
                    detected.insert(fullPath);
                }
            }
        }
    }

    return detected.values();
}

void DeviceDetector::refresh() {
    QStringList devs = detectDevices();
    emit devicesRefreshed(devs);
}
