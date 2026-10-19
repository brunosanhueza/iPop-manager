#include "IpodTouchBridge.h"
#include "AudioConverter.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QElapsedTimer>

#ifndef IPOP_SOURCE_TOOLS_DIR
#define IPOP_SOURCE_TOOLS_DIR ""
#endif

namespace {
QString clean(QString s) {
    s.replace('\t', ' ');
    s.replace('\n', ' ');
    s.replace('\r', ' ');
    return s.trimmed();
}

QProcessEnvironment gestorEnv(const QString& tools) {
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("OPENSSL_CONF", QDir(tools).filePath("openssl-legacy.cnf"));
    // ifuse no implementa chmod; libgpod lo necesita para renombrar la base
    env.insert("LD_PRELOAD", QDir(tools).filePath("nochmod.so"));
    return env;
}
} // namespace

bool IpodTouchBridge::isTouchLayout(const QString& mountPath) {
    if (mountPath.isEmpty()) return false;
    return QFileInfo(QDir(mountPath).filePath("iTunes_Control/iTunes/iTunes Library.itlp")).isDir();
}

QString IpodTouchBridge::toolsDir() {
    const QStringList candidates = {
        QCoreApplication::applicationDirPath() + "/tools/ipod",
        QCoreApplication::applicationDirPath() + "/../tools/ipod",
        QCoreApplication::applicationDirPath(),
        QString::fromUtf8(IPOP_SOURCE_TOOLS_DIR)
    };
    for (const QString& c : candidates) {
        if (c.isEmpty()) continue;
        QDir d(c);
        if (QFileInfo(d.filePath("gestor")).isExecutable() &&
            QFileInfo(d.filePath("nochmod.so")).exists() &&
            QFileInfo(d.filePath("openssl-legacy.cnf")).exists())
            return d.absolutePath();
    }
    return QString();
}

bool IpodTouchBridge::toolsAvailable(QString* errorMsg) {
    if (!toolsDir().isEmpty()) return true;
    if (errorMsg)
        *errorMsg = "No se encuentran las herramientas del iPod Touch (gestor, nochmod.so, "
                    "openssl-legacy.cnf). Compila el proyecto con CMake (requiere libgpod-dev).";
    return false;
}

QStringList IpodTouchBridge::environmentWithLegacyTls() {
    const QString tools = toolsDir();
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if (!tools.isEmpty())
        env.insert("OPENSSL_CONF", QDir(tools).filePath("openssl-legacy.cnf"));
    return env.toStringList();
}

bool IpodTouchBridge::addTracks(const QString& mountPath, const QList<QString>& sourceFiles,
                                ProgressCb progressCb, QString* errorMsg, int* addedCount,
                                const QString& playlistName)
{
    if (addedCount) *addedCount = 0;
    if (sourceFiles.isEmpty()) return false;
    if (!toolsAvailable(errorMsg)) return false;
    const QString tools = toolsDir();

    // Una linea por archivo: ruta TAB titulo TAB artista TAB dur_ms TAB album TAB genero TAB bitrate TAB anio TAB pista
    QByteArray input;
    uint64_t totalBytes = 0;
    QHash<QString, uint64_t> sizes;
    for (const QString& src : sourceFiles) {
        const Track t = AudioConverter::extractMetadata(src);
        const QString abs = QFileInfo(src).absoluteFilePath();
        const uint64_t sz = static_cast<uint64_t>(QFileInfo(src).size());
        sizes.insert(abs, sz);
        totalBytes += sz;
        const QString line = QStringList{
            abs, clean(t.title), clean(t.artist), QString::number(t.durationMs),
            clean(t.album), clean(t.genre), QString::number(t.bitrate),
            QString::number(t.year), QString::number(t.trackNumber)
        }.join('\t') + '\n';
        input += line.toUtf8();
    }

    QProcess proc;
    proc.setProcessEnvironment(gestorEnv(tools));
    proc.setProcessChannelMode(QProcess::MergedChannels);
    QStringList addArgs{mountPath, "addmany"};
    if (!playlistName.isEmpty()) addArgs << playlistName;
    proc.start(QDir(tools).filePath("gestor"), addArgs);
    if (!proc.waitForStarted(5000)) {
        if (errorMsg) *errorMsg = "No se pudo ejecutar gestor.";
        return false;
    }
    proc.write(input);
    proc.closeWriteChannel();

    int current = 0;
    uint64_t doneBytes = 0;
    const int total = sourceFiles.size();
    QStringList failures;
    bool dbWritten = false;
    int added = 0;
    QByteArray buffer;
    QElapsedTimer limit; limit.start();

    auto consume = [&](const QByteArray& chunk) {
        buffer += chunk;
        int nl;
        while ((nl = buffer.indexOf('\n')) >= 0) {
            const QString line = QString::fromUtf8(buffer.left(nl)).trimmed();
            buffer.remove(0, nl + 1);
            if (line.startsWith("COPIED\t")) {
                ++current; ++added;
                const QString f = line.section('\t', 1);
                doneBytes += sizes.value(f);
                if (progressCb)
                    progressCb(current, total, doneBytes, totalBytes,
                               QString("Copiada (%1/%2): %3").arg(current).arg(total).arg(QFileInfo(f).fileName()));
            } else if (line.startsWith("FAIL\t")) {
                ++current;
                failures << (QFileInfo(line.section('\t', 1, 1)).fileName() + ": " + line.section('\t', 2));
            } else if (line.contains("escrita OK")) {
                dbWritten = true;
            }
        }
    };

    while (!proc.waitForFinished(100)) {
        consume(proc.readAll());
        QCoreApplication::processEvents();
        if (limit.elapsed() > 30 * 60 * 1000) { proc.kill(); break; }
    }
    consume(proc.readAll());

    if (addedCount) *addedCount = added;
    const bool ok = proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0 && dbWritten && added > 0;
    if (!ok && errorMsg) {
        *errorMsg = failures.isEmpty()
            ? "gestor no pudo escribir la base de datos del iPod. Revisa que el iPod este desbloqueado y montado."
            : failures.join("\n");
    } else if (ok && !failures.isEmpty() && errorMsg) {
        *errorMsg = "Algunos archivos no se anadieron:\n" + failures.join("\n");
    }
    return ok;
}

bool IpodTouchBridge::deleteTracks(const QString& mountPath, const QList<uint64_t>& dbids, QString* errorMsg) {
    if (dbids.isEmpty()) return false;
    if (!toolsAvailable(errorMsg)) return false;
    const QString tools = toolsDir();

    QStringList args{mountPath, "del"};
    for (uint64_t id : dbids) {
        if (id == 0) {
            if (errorMsg) *errorMsg = "Una de las canciones no tiene ID valido; no se borro nada.";
            return false;
        }
        args << QString::number(id);
    }

    QProcess proc;
    proc.setProcessEnvironment(gestorEnv(tools));
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start(QDir(tools).filePath("gestor"), args);
    if (!proc.waitForStarted(5000)) {
        if (errorMsg) *errorMsg = "No se pudo ejecutar gestor.";
        return false;
    }
    QElapsedTimer limit; limit.start();
    while (!proc.waitForFinished(100)) {
        QCoreApplication::processEvents();
        if (limit.elapsed() > 10 * 60 * 1000) { proc.kill(); break; }
    }
    const QString out = QString::fromUtf8(proc.readAll());
    const bool ok = proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0 &&
                    out.contains("escrita OK");
    if (!ok && errorMsg) {
        QString detail;
        for (const QString& l : out.split('\n'))
            if (l.contains("No existe") || l.contains("ID ") || l.contains("Error")) detail += l + "\n";
        *errorMsg = detail.isEmpty() ? "gestor no pudo borrar las canciones." : detail.trimmed();
    }
    return ok;
}

// Ejecuta un comando de escritura de gestor y comprueba que la base se haya escrito
static bool runGestorWrite(const QString& mountPath, const QStringList& cmdArgs, QString* errorMsg) {
    QString why;
    if (!IpodTouchBridge::toolsAvailable(&why)) { if (errorMsg) *errorMsg = why; return false; }
    const QString tools = IpodTouchBridge::toolsDir();

    QProcess proc;
    proc.setProcessEnvironment(gestorEnv(tools));
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start(QDir(tools).filePath("gestor"), QStringList{mountPath} + cmdArgs);
    if (!proc.waitForStarted(5000)) {
        if (errorMsg) *errorMsg = "No se pudo ejecutar gestor.";
        return false;
    }
    QElapsedTimer limit; limit.start();
    while (!proc.waitForFinished(100)) {
        QCoreApplication::processEvents();
        if (limit.elapsed() > 10 * 60 * 1000) { proc.kill(); break; }
    }
    const QString out = QString::fromUtf8(proc.readAll());
    const bool ok = proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0 &&
                    out.contains("escrita OK");
    if (!ok && errorMsg) {
        QString detail;
        for (const QString& l : out.split('\n'))
            if (l.contains("No ") || l.contains("ya existe") || l.contains("invalido") ||
                l.contains("Error") || l.contains("repetida")) detail += l + "\n";
        *errorMsg = detail.isEmpty() ? "gestor no pudo escribir la base de datos del iPod." : detail.trimmed();
    }
    return ok;
}

bool IpodTouchBridge::createPlaylist(const QString& mountPath, const QString& name, QString* errorMsg) {
    return runGestorWrite(mountPath, {"plnew", name}, errorMsg);
}

bool IpodTouchBridge::renamePlaylist(const QString& mountPath, const QString& oldName,
                                     const QString& newName, QString* errorMsg) {
    return runGestorWrite(mountPath, {"plrename", oldName, newName}, errorMsg);
}

bool IpodTouchBridge::deletePlaylists(const QString& mountPath, const QStringList& names, QString* errorMsg) {
    if (names.isEmpty()) return false;
    return runGestorWrite(mountPath, QStringList{"pldel"} + names, errorMsg);
}

bool IpodTouchBridge::unmount(const QString& mountPath, QString* errorMsg) {
    QProcess::execute("sync");
    for (const QString& tool : {QString("fusermount"), QString("fusermount3")}) {
        if (QProcess::execute(tool, {"-u", mountPath}) == 0) return true;
    }
    if (QProcess::execute("umount", {mountPath}) == 0) return true;
    if (errorMsg) *errorMsg = "No se pudo desmontar el iPod (cierra los archivos abiertos e intentalo de nuevo).";
    return false;
}
