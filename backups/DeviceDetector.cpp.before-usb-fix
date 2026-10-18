#include "DeviceDetector.h"

#include <QStorageInfo>
#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QSet>
#include <QProcess>

DeviceDetector::DeviceDetector(QObject* parent)
    : QObject(parent)
{
}

bool DeviceDetector::isIpodMount(const QString& path) {
    if (path.isEmpty()) return false;

    QDir dir(path);
    if (!dir.exists()) return false;

    const QString ctrl = dir.filePath("iPod_Control");
    if (QFileInfo(ctrl).isDir()) return true;

    const QString ctrlLower = dir.filePath("ipod_control");
    return QFileInfo(ctrlLower).isDir();
}

QStringList DeviceDetector::detectDevices() {
    QSet<QString> detected;

    for (const auto& vol : QStorageInfo::mountedVolumes()) {
        if (vol.isValid() && vol.isReady()) {
            const QString root = vol.rootPath();
            if (isIpodMount(root)) detected.insert(root);
        }
    }

    const QString user =
        QProcessEnvironment::systemEnvironment().value("USER");

    const QStringList searchBases = {
        "/media",
        "/run/media",
        "/run/media/" + user,
        "/media/" + user,
        "/mnt"
    };

    for (const QString& base : searchBases) {
        QDir baseDir(base);
        if (!baseDir.exists()) continue;

        const QStringList subdirs =
            baseDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

        for (const QString& sub : subdirs) {
            const QString fullPath = baseDir.filePath(sub);
            if (isIpodMount(fullPath)) detected.insert(fullPath);
        }
    }

    return detected.values();
}

QStringList DeviceDetector::detectUsbDevices() {
    QStringList devices;

    QProcess process;
    process.start("lsusb", {});
    if (!process.waitForStarted(1000) || !process.waitForFinished(2000)) {
        process.kill();
        process.waitForFinished(500);
        return devices;
    }

    if (process.exitStatus() != QProcess::NormalExit ||
        process.exitCode() != 0) {
        return devices;
    }

    const QString output =
        QString::fromLocal8Bit(process.readAllStandardOutput());

    for (const QString& line : output.split('\n')) {
        if (line.contains("ID 05ac:1293", Qt::CaseInsensitive)) {
            devices.append("iPod touch 2.ª generación — conectado por USB");
        }
    }

    return devices;
}

void DeviceDetector::refresh() {
    const QStringList mounts = detectDevices();
    emit devicesRefreshed(mounts);
    emit usbDevicesRefreshed(detectUsbDevices());
}
