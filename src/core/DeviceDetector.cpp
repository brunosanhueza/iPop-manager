#include "DeviceDetector.h"

#include <QStorageInfo>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QTextStream>
#include <QProcessEnvironment>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QDebug>

DeviceDetector::DeviceDetector(QObject* parent)
    : QObject(parent)
{
}

QString DeviceDetector::findCaseInsensitiveEntry(const QString& parentDir, const QString& targetName) {
    if (parentDir.isEmpty() || targetName.isEmpty()) return QString();
    QDir dir(parentDir);
    if (!dir.exists()) return QString();

    // 1. Comprobación directa (rápida si el nombre ya coincide exactamente)
    QString direct = dir.filePath(targetName);
    if (QFileInfo::exists(direct)) {
        return direct;
    }

    // 2. Búsqueda insensible a mayúsculas/minúsculas incluyendo carpetas y ficheros ocultos
    const QStringList entries = dir.entryList(QDir::Dirs | QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot);
    for (const QString& entry : entries) {
        if (entry.compare(targetName, Qt::CaseInsensitive) == 0) {
            return dir.filePath(entry);
        }
    }
    return QString();
}

QString DeviceDetector::resolveCaseInsensitivePath(const QString& basePath, const QStringList& subparts) {
    if (basePath.isEmpty() || subparts.isEmpty()) return QString();

    // Comprobación directa de la ruta completa primero
    QString fullCandidate = basePath;
    for (const QString& part : subparts) {
        fullCandidate = QDir(fullCandidate).filePath(part);
    }
    if (QFileInfo::exists(fullCandidate)) {
        return fullCandidate;
    }

    // Resolución paso a paso insensible a mayúsculas
    QString current = basePath;
    for (const QString& part : subparts) {
        QString matched = findCaseInsensitiveEntry(current, part);
        if (matched.isEmpty()) {
            return QString();
        }
        current = matched;
    }
    return current;
}

bool DeviceDetector::isIpodMount(const QString& path) {
    if (path.isEmpty()) return false;
    QDir dir(path);
    if (!dir.exists()) return false;

    // Buscar "iPod_Control" de forma insensible a mayúsculas/minúsculas
    QString ctrl = findCaseInsensitiveEntry(path, "iPod_Control");
    if (!ctrl.isEmpty() && QFileInfo(ctrl).isDir()) {
        return true;
    }
    return false;
}

QString DeviceDetector::resolveControlPath(const QString& mountPath) {
    QString p = resolveCaseInsensitivePath(mountPath, {"iPod_Control"});
    if (!p.isEmpty()) return p;
    return QDir(mountPath).filePath("iPod_Control");
}

QString DeviceDetector::resolveMusicPath(const QString& mountPath) {
    QString p = resolveCaseInsensitivePath(mountPath, {"iPod_Control", "Music"});
    if (!p.isEmpty()) return p;
    QString ctrl = resolveControlPath(mountPath);
    return QDir(ctrl).filePath("Music");
}

QString DeviceDetector::resolveDatabasePath(const QString& mountPath) {
    return resolveCaseInsensitivePath(mountPath, {"iPod_Control", "iTunes", "iTunesDB"});
}

QString DeviceDetector::resolveSysInfoPath(const QString& mountPath) {
    return resolveCaseInsensitivePath(mountPath, {"iPod_Control", "Device", "SysInfo"});
}

QStringList DeviceDetector::detectDevices() {
    QSet<QString> detected;

#if defined(Q_OS_WIN)
    // 1. En Windows: Escanear todas las unidades lógicas disponibles de A: a Z:
    for (char c = 'A'; c <= 'Z'; ++c) {
        QString drive = QString("%1:/").arg(c);
        if (isIpodMount(drive)) {
            detected.insert(drive);
        }
    }
#endif

    // 2. Volúmenes montados según QStorageInfo (funciona en Windows y Linux)
    const auto volumes = QStorageInfo::mountedVolumes();
    for (const auto& vol : volumes) {
        if (vol.isValid() && vol.isReady()) {
            QString root = vol.rootPath();
            if (isIpodMount(root)) {
                detected.insert(root);
            }
        }
    }

#if defined(Q_OS_LINUX)
    // 3. En Linux: Leer directamente /proc/mounts (la fuente de verdad del kernel)
    QFile mountsFile("/proc/mounts");
    if (mountsFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&mountsFile);
        while (!in.atEnd()) {
            QString line = in.readLine().trimmed();
            if (line.isEmpty() || line.startsWith('#')) continue;
            QStringList parts = line.split(' ', Qt::SkipEmptyParts);
            if (parts.size() >= 2) {
                QString mountPoint = parts[1];
                // Desescapar secuencias octales comunes (ej. \040 para espacios en etiquetas)
                mountPoint.replace("\\040", " ");
                mountPoint.replace("\\011", "\t");
                if (isIpodMount(mountPoint)) {
                    detected.insert(mountPoint);
                }
            }
        }
        mountsFile.close();
    }

    // 4. En Linux: Escaneo exhaustivo con profundidad 1 y 2 en /run/media, /media y /mnt
    QStringList searchBases = {"/run/media", "/media", "/mnt"};
    for (const QString& base : searchBases) {
        QDir baseDir(base);
        if (!baseDir.exists()) continue;

        // Nivel 1 (ej. /media/IPOD o /run/media/usuario)
        const QStringList l1Dirs = baseDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString& d1 : l1Dirs) {
            QString p1 = baseDir.filePath(d1);
            if (isIpodMount(p1)) {
                detected.insert(p1);
                continue;
            }

            // Nivel 2 (ej. /run/media/usuario/IPOD)
            QDir subDir(p1);
            const QStringList l2Dirs = subDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
            for (const QString& d2 : l2Dirs) {
                QString p2 = subDir.filePath(d2);
                if (isIpodMount(p2)) {
                    detected.insert(p2);
                }
            }
        }
    }
#endif

    return detected.values();
}

#if defined(Q_OS_LINUX)
QList<DeviceDetector::UnmountedIpod> DeviceDetector::detectUnmountedIpods() {
    QList<UnmountedIpod> result;
    QProcess process;
    process.start("lsblk", {"-P", "-o", "NAME,TYPE,FSTYPE,LABEL,MOUNTPOINT,VENDOR,MODEL"});
    if (!process.waitForFinished(1500)) return result;

    QString output = QString::fromUtf8(process.readAllStandardOutput());
    const QStringList lines = output.split('\n', Qt::SkipEmptyParts);

    for (const QString& line : lines) {
        auto getVal = [&](const QString& key) -> QString {
            QRegularExpression re(key + "=\"([^\"]*)\"");
            auto match = re.match(line);
            return match.hasMatch() ? match.captured(1) : QString();
        };

        QString name = getVal("NAME");
        QString type = getVal("TYPE");
        QString fstype = getVal("FSTYPE");
        QString label = getVal("LABEL");
        QString mount = getVal("MOUNTPOINT");
        QString vendor = getVal("VENDOR");
        QString model = getVal("MODEL");

        if (type == "part" && mount.isEmpty()) {
            bool isApple = vendor.contains("Apple", Qt::CaseInsensitive) || model.contains("iPod", Qt::CaseInsensitive);
            bool isIpodLabel = label.contains("iPod", Qt::CaseInsensitive) || label.contains("IPOD", Qt::CaseInsensitive);
            if ((isApple || isIpodLabel) && (fstype == "vfat" || fstype == "hfsplus" || fstype.isEmpty())) {
                UnmountedIpod u;
                u.deviceNode = name.startsWith('/') ? name : ("/dev/" + name);
                u.label = label.isEmpty() ? "iPod" : label;
                u.fstype = fstype;
                u.model = model.isEmpty() ? "iPod" : model;
                result.append(u);
            }
        }
    }
    return result;
}

bool DeviceDetector::mountDeviceNode(const QString& deviceNode, QString* outMountPoint) {
    if (deviceNode.isEmpty()) return false;
    QProcess process;
    process.start("udisksctl", {"mount", "-b", deviceNode});
    if (!process.waitForFinished(3500)) return false;

    QString output = QString::fromUtf8(process.readAllStandardOutput());
    if (output.contains("Mounted") && output.contains(" at ")) {
        QString mnt = output.section(" at ", 1).trimmed();
        if (mnt.endsWith('.')) mnt.chop(1);
        if (outMountPoint) *outMountPoint = mnt.trimmed();
        return true;
    }
    return false;
}
#else
QList<DeviceDetector::UnmountedIpod> DeviceDetector::detectUnmountedIpods() {
    return {};
}

bool DeviceDetector::mountDeviceNode(const QString&, QString*) {
    return false;
}
#endif

void DeviceDetector::refresh() {
    QStringList devs = detectDevices();
    emit devicesRefreshed(devs);
}
