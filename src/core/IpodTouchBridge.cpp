#include "IpodTouchBridge.h"
#include "AudioConverter.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QElapsedTimer>
#include <QStorageInfo>
#include <QStandardPaths>
#include <QMap>
#include <QRegularExpression>

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

static QString s_lastMessage;
QString IpodTouchBridge::lastMessage() { return s_lastMessage; }

// Hash del AUDIO (sin etiquetas ID3) con ffmpeg -c copy: detecta el mismo audio con distinto nombre
static QString audioHash(const QString& ffmpeg, const QString& file) {
    QProcess p;
    p.start(ffmpeg, {"-v", "error", "-i", file, "-map", "0:a:0", "-c", "copy", "-f", "md5", "-"});
    if (!p.waitForStarted(3000) || !p.waitForFinished(30000)) { p.kill(); p.waitForFinished(1000); return QString(); }
    const QString out = QString::fromUtf8(p.readAllStandardOutput());
    const int i = out.indexOf("MD5=");
    return i >= 0 ? out.mid(i + 4, 32) : QString();
}

bool IpodTouchBridge::addTracks(const QString& mountPath, const QList<QString>& sourceFiles,
                                ProgressCb progressCb, QString* errorMsg, int* addedCount,
                                const QString& playlistName,
                                const QHash<QString, Track>& metadata)
{
    s_lastMessage.clear();
    if (addedCount) *addedCount = 0;
    if (sourceFiles.isEmpty()) return false;
    if (!toolsAvailable(errorMsg)) { s_lastMessage = errorMsg ? *errorMsg : QString(); return false; }
    const QString tools = toolsDir();
    const int total = sourceFiles.size();

    auto fail = [&](const QString& msg) {
        s_lastMessage = msg;
        if (errorMsg) *errorMsg = msg;
        return false;
    };

    // ---- 1. Espacio libre ----
    uint64_t totalBytes = 0;
    QHash<QString, uint64_t> sizes;
    for (const QString& src : sourceFiles) {
        const uint64_t sz = static_cast<uint64_t>(QFileInfo(src).size());
        sizes.insert(QFileInfo(src).absoluteFilePath(), sz);
        totalBytes += sz;
    }
    {
        QStorageInfo st(mountPath);
        const qint64 avail = st.isValid() ? st.bytesAvailable() : -1;
        if (avail > 0 && totalBytes + 32ull * 1024 * 1024 > static_cast<uint64_t>(avail)) {
            return fail(QString("No hay espacio suficiente en el iPod: las canciones ocupan %1 MB y quedan %2 MB libres.")
                        .arg(totalBytes / (1024 * 1024)).arg(avail / (1024 * 1024)));
        }
    }

    // ---- 2. Aviso de audio repetido (mismo audio aunque el nombre/etiquetas cambien) ----
    QString warning;
    const QString ffmpeg = QStandardPaths::findExecutable("ffmpeg");
    if (!ffmpeg.isEmpty() && total > 1 && total <= 1500) {
        QMap<QString, QStringList> groups;
        int n = 0;
        for (const QString& src : sourceFiles) {
            if (progressCb)
                progressCb(0, total, 0, totalBytes, QString("Comprobando audio repetido (%1/%2)...").arg(++n).arg(total));
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
            const QString h = audioHash(ffmpeg, src);
            if (!h.isEmpty()) groups[h].append(QFileInfo(src).fileName());
        }
        int shown = 0, dupGroups = 0;
        QString detail;
        for (auto it = groups.cbegin(); it != groups.cend(); ++it) {
            if (it.value().size() < 2) continue;
            ++dupGroups;
            if (shown++ < 8) detail += "\n • " + it.value().join("  =  ");
        }
        if (dupGroups > 0)
            warning = QString("AVISO: %1 grupos de archivos contienen exactamente el mismo audio aunque tengan nombres distintos "
                              "(suele pasar con conversores de enlaces):%2%3")
                          .arg(dupGroups).arg(detail).arg(dupGroups > shown ? "\n • ..." : "");
    }

    // ---- 3. Copia por lotes: cada lote escribe la base, asi un fallo no pierde todo ----
    const int kChunk = 40;
    int current = 0, added = 0;
    uint64_t doneBytes = 0;
    QStringList failures;
    QString tail;
    bool aborted = false;

    for (int off = 0; off < total && !aborted; off += kChunk) {
        const QList<QString> chunk = sourceFiles.mid(off, kChunk);

        QByteArray input;
        for (const QString& src : chunk) {
            const QString absSrc = QFileInfo(src).absoluteFilePath();
            const Track t = metadata.contains(absSrc) ? metadata.value(absSrc)
                                                       : AudioConverter::extractMetadata(src);
            input += (QStringList{
                QFileInfo(src).absoluteFilePath(), clean(t.title), clean(t.artist), QString::number(t.durationMs),
                clean(t.album), clean(t.genre), QString::number(t.bitrate),
                QString::number(t.year), QString::number(t.trackNumber)
            }.join('\t') + '\n').toUtf8();
        }

        QProcess proc;
        proc.setProcessEnvironment(gestorEnv(tools));
        proc.setProcessChannelMode(QProcess::MergedChannels);
        QStringList addArgs{mountPath, "addmany"};
        if (!playlistName.isEmpty()) addArgs << playlistName;
        proc.start(QDir(tools).filePath("gestor"), addArgs);
        if (!proc.waitForStarted(5000)) { failures << "No se pudo ejecutar gestor."; break; }
        proc.write(input);
        proc.closeWriteChannel();

        bool dbWritten = false;
        int addedInChunk = 0;
        QByteArray buffer;
        QElapsedTimer idle; idle.start();       // solo se mata a gestor si pasa mucho tiempo SIN actividad

        auto consume = [&](const QByteArray& data) {
            if (!data.isEmpty()) idle.restart();
            buffer += data;
            int nl;
            while ((nl = buffer.indexOf('\n')) >= 0) {
                const QString line = QString::fromUtf8(buffer.left(nl)).trimmed();
                buffer.remove(0, nl + 1);
                if (line.startsWith("COPIED\t")) {
                    ++current; ++addedInChunk;
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
                } else if (!line.isEmpty() && !line.startsWith("RESUMEN") &&
                           !line.contains(QRegularExpression("^(\\[mk_|\\[run_post|itdb|libitdbprep|itlp|\\*\\.itdb|library_persistent_id|device name|Locking for sync)")) &&
                           !line.contains("Could not find corresponding track") && !line.contains("CRITICAL")) {
                    tail += line + "\n";
                    if (tail.size() > 1500) tail = tail.right(1500);
                }
            }
        };

        while (!proc.waitForFinished(100)) {
            consume(proc.readAll());
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
            if (idle.elapsed() > 10 * 60 * 1000) {      // 10 min sin respuesta: algo se colgo
                proc.kill();
                proc.waitForFinished(5000);
                failures << "gestor dejo de responder durante 10 minutos y se cancelo.";
                break;
            }
        }
        consume(proc.readAll());

        const bool chunkOk = proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0 &&
                             dbWritten && addedInChunk > 0;
        if (chunkOk) {
            added += addedInChunk;
        } else if (addedInChunk > 0 || failures.isEmpty()) {
            // Se copiaron archivos pero la base no se escribio (o gestor termino mal): detener
            aborted = true;
            failures << QString("El lote %1 no se pudo registrar en la base de datos del iPod.").arg(off / kChunk + 1);
        } else {
            aborted = true;   // todos los archivos del lote fallaron (p. ej. disco lleno): no seguir
        }
    }

    if (addedCount) *addedCount = added;
    QString summary = QString("Se añadieron %1 de %2 canciones.").arg(added).arg(total);
    if (!failures.isEmpty()) summary += "\n\nProblemas:\n" + failures.mid(0, 10).join("\n");
    if (!tail.isEmpty() && added < total) summary += "\n\nDetalle de gestor:\n" + tail.trimmed();
    if (!warning.isEmpty()) summary += "\n\n" + warning;
    s_lastMessage = summary;
    if (errorMsg) *errorMsg = failures.isEmpty() ? QString() : summary;
    qWarning().noquote() << "IpodTouchBridge::addTracks:" << summary;

    return added == total;
}

bool IpodTouchBridge::updateTracks(const QString& mountPath, const QList<TrackEdit>& edits,
                                   QString* errorMsg, int* updatedCount)
{
    if (updatedCount) *updatedCount = 0;
    if (edits.isEmpty()) return false;
    if (!toolsAvailable(errorMsg)) return false;
    const QString tools = toolsDir();

    QByteArray input;
    for (const TrackEdit& e : edits) {
        input += (QStringList{ QString::number(e.dbid), clean(e.title), clean(e.artist), clean(e.album),
                               QString::number(e.durationMs) }.join('\t') + '\n').toUtf8();
    }

    QProcess proc;
    proc.setProcessEnvironment(gestorEnv(tools));
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start(QDir(tools).filePath("gestor"), {mountPath, "settags"});
    if (!proc.waitForStarted(5000)) {
        if (errorMsg) *errorMsg = "No se pudo ejecutar gestor.";
        return false;
    }
    proc.write(input);
    proc.closeWriteChannel();

    QByteArray all;
    QElapsedTimer idle; idle.start();
    while (!proc.waitForFinished(100)) {
        const QByteArray d = proc.readAll();
        if (!d.isEmpty()) { all += d; idle.restart(); }
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        if (idle.elapsed() > 10 * 60 * 1000) { proc.kill(); proc.waitForFinished(5000); break; }
    }
    all += proc.readAll();
    const QString out = QString::fromUtf8(all);

    int updated = 0;
    QStringList failures;
    for (const QString& line : out.split('\n')) {
        if (line.startsWith("UPDATED\t")) ++updated;
        else if (line.startsWith("FAIL\t")) failures << line.section('\t', 1, 1) + ": " + line.section('\t', 2);
    }
    if (updatedCount) *updatedCount = updated;
    const bool ok = proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0 &&
                    out.contains("escrita OK") && updated > 0;
    if (!ok && errorMsg)
        *errorMsg = failures.isEmpty() ? "gestor no pudo guardar los cambios en la base de datos del iPod."
                                       : failures.mid(0, 8).join("\n");
    return ok;
}

bool IpodTouchBridge::setArtwork(const QString& mountPath, const QList<ArtworkEdit>& edits,
                                 QString* errorMsg, int* updatedCount)
{
    if (updatedCount) *updatedCount = 0;
    if (edits.isEmpty()) return false;
    if (!toolsAvailable(errorMsg)) return false;
    const QString tools = toolsDir();

    QByteArray input;
    for (const ArtworkEdit& e : edits)
        input += (QString::number(e.dbid) + '\t' + clean(e.imagePath) + '\n').toUtf8();

    QProcess proc;
    proc.setProcessEnvironment(gestorEnv(tools));
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start(QDir(tools).filePath("gestor"), {mountPath, "setart"});
    if (!proc.waitForStarted(5000)) {
        if (errorMsg) *errorMsg = "No se pudo ejecutar gestor.";
        return false;
    }
    proc.write(input);
    proc.closeWriteChannel();

    QByteArray all;
    QElapsedTimer idle; idle.start();
    while (!proc.waitForFinished(100)) {
        const QByteArray d = proc.readAll();
        if (!d.isEmpty()) { all += d; idle.restart(); }
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        if (idle.elapsed() > 10 * 60 * 1000) { proc.kill(); proc.waitForFinished(5000); break; }
    }
    all += proc.readAll();
    const QString out = QString::fromUtf8(all);

    int updated = 0;
    QStringList failures;
    for (const QString& line : out.split('\n')) {
        if (line.startsWith("UPDATED\t")) ++updated;
        else if (line.startsWith("FAIL\t")) failures << line.section('\t', 1, 1) + ": " + line.section('\t', 2);
        else if (line.startsWith("Error:")) failures << line;
    }
    if (updatedCount) *updatedCount = updated;
    const bool ok = proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0 &&
                    out.contains("escrita OK") && updated > 0;
    if (errorMsg) {
        if (!ok)
            *errorMsg = failures.isEmpty() ? "gestor no pudo guardar las caratulas en el iPod." : failures.mid(0, 8).join("\n");
        else if (!failures.isEmpty())
            *errorMsg = "Algunas caratulas no se pudieron aplicar:\n" + failures.mid(0, 8).join("\n");
    }
    return ok;
}

static bool runFfmpegCover(const QStringList& inputArgs, const QString& outJpg) {
    const QString ffmpeg = QStandardPaths::findExecutable("ffmpeg");
    if (ffmpeg.isEmpty()) return false;
    QFile::remove(outJpg);
    QStringList args{"-y", "-v", "error"};
    args << inputArgs << "-frames:v" << "1"
         << "-vf" << "scale=600:600:force_original_aspect_ratio=increase,crop=600:600"
         << "-q:v" << "2" << outJpg;
    QProcess p;
    p.start(ffmpeg, args);
    if (!p.waitForStarted(3000) || !p.waitForFinished(30000)) { p.kill(); p.waitForFinished(1000); return false; }
    return p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0 && QFileInfo(outJpg).size() > 0;
}

bool IpodTouchBridge::prepareCoverImage(const QString& sourceImage, const QString& outJpg) {
    return runFfmpegCover({"-i", sourceImage}, outJpg);
}

bool IpodTouchBridge::extractEmbeddedCover(const QString& audioFile, const QString& outJpg) {
    return runFfmpegCover({"-i", audioFile, "-map", "0:v:0"}, outJpg);
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
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
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
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
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
