#include "DeviceDetector.h"

#include <QStorageInfo>
#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QSet>
#include <QProcess>
#include <QStandardPaths>
#include <QElapsedTimer>
#include "IpodTouchBridge.h"

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
    return QFileInfo(ctrlLower).isDir()
        || QFileInfo(dir.filePath("iTunes_Control")).isDir()
        || QFileInfo(dir.filePath("itunes_control")).isDir();
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

    // Montaje propio del iPod Touch (ifuse), por si QStorageInfo no lo lista
    const QString touchMp = touchMountPoint();
    if (isIpodMount(touchMp)) detected.insert(touchMp);

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

QString DeviceDetector::touchMountPoint() {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/mount";
}

bool DeviceDetector::mountTouchIfNeeded(QString* errorMsg) {
    const QString mp = touchMountPoint();
    if (isIpodMount(mp)) return true;                 // ya montado y legible

    // No reintentar en cada refresco si falla (cada intento puede tardar unos segundos)
    static QElapsedTimer lastTry;
    static bool tried = false;
    if (tried && lastTry.elapsed() < 5000) return false;
    tried = true;
    lastTry.start();

    if (detectUsbDevices().isEmpty()) return false;

    // Limpia un montaje roto de intentos anteriores (Input/output error)
    QProcess::execute("fusermount", {"-uz", mp});
    QDir().mkpath(mp);

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString tools = IpodTouchBridge::toolsDir();
    if (!tools.isEmpty())
        env.insert("OPENSSL_CONF", QDir(tools).filePath("openssl-legacy.cnf"));

    auto run = [&](const QString& prog, const QStringList& args, int timeoutMs) {
        QProcess p;
        p.setProcessEnvironment(env);
        p.start(prog, args);
        if (!p.waitForStarted(2000) || !p.waitForFinished(timeoutMs)) {
            p.kill();
            p.waitForFinished(500);
            return -1;
        }
        return p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -1;
    };

    if (run("ifuse", {mp}, 15000) != 0) {
        // Puede faltar el emparejado ("Confiar" en el iPod)
        run("idevicepair", {"pair"}, 15000);
        if (run("ifuse", {mp}, 15000) != 0) {
            if (errorMsg)
                *errorMsg = "No se pudo montar el iPod Touch. Desbloquealo, toca \"Confiar\" y revisa "
                            "que usbmuxd este corriendo (sudo sv status usbmuxd).";
            return false;
        }
    }
    return isIpodMount(mp);
}

void DeviceDetector::refresh() {
    if (detectDevices().isEmpty()) mountTouchIfNeeded();
    const QStringList mounts = detectDevices();
    emit devicesRefreshed(mounts);
    emit usbDevicesRefreshed(detectUsbDevices());
}
