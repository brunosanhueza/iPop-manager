#include "Romanizer.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

#ifndef IPOP_SOURCE_TOOLS_DIR
#define IPOP_SOURCE_TOOLS_DIR ""
#endif
#ifndef IPOP_SRC_TOOLS_DIR
#define IPOP_SRC_TOOLS_DIR ""
#endif

bool Romanizer::needsRomanization(const QString& s) {
    for (const QChar& c : s)
        if (c.unicode() > 127) return true;
    return false;
}

QString Romanizer::scriptPath() {
    const QStringList dirs = {
        QCoreApplication::applicationDirPath() + "/tools/ipod",
        QCoreApplication::applicationDirPath() + "/../tools/ipod",
        QCoreApplication::applicationDirPath(),
        QString::fromUtf8(IPOP_SOURCE_TOOLS_DIR),
        QString::fromUtf8(IPOP_SRC_TOOLS_DIR)
    };
    for (const QString& d : dirs) {
        if (d.isEmpty()) continue;
        const QString f = QDir(d).filePath("romanizar.py");
        if (QFileInfo::exists(f)) return QDir(d).absoluteFilePath("romanizar.py");
    }
    return QString();
}

bool Romanizer::available(QString* why) {
    if (QStandardPaths::findExecutable("python3").isEmpty()) {
        if (why) *why = "Falta python3 para convertir el japonés a romaji.";
        return false;
    }
    if (scriptPath().isEmpty()) {
        if (why) *why = "No se encuentra tools/ipod/romanizar.py.";
        return false;
    }
    return true;
}

QStringList Romanizer::romanize(const QStringList& texts, QString* engine, QString* error) {
    QStringList result;
    for (int i = 0; i < texts.size(); ++i) result << QString();

    QStringList toSend;
    QList<int> idx;
    for (int i = 0; i < texts.size(); ++i) {
        if (!needsRomanization(texts[i])) continue;
        QString t = texts[i];
        t.replace('\n', ' ').replace('\r', ' ');
        toSend << t;
        idx << i;
    }
    if (toSend.isEmpty()) return result;

    QString why;
    if (!available(&why)) { if (error) *error = why; return result; }

    QProcess p;
    p.start(QStandardPaths::findExecutable("python3"), {scriptPath()});
    if (!p.waitForStarted(5000)) { if (error) *error = "No se pudo ejecutar python3."; return result; }
    p.write((toSend.join('\n') + '\n').toUtf8());
    p.closeWriteChannel();
    if (!p.waitForFinished(60000)) {
        p.kill(); p.waitForFinished(2000);
        if (error) *error = "El convertidor de romaji tardó demasiado.";
        return result;
    }
    const QStringList lines = QString::fromUtf8(p.readAllStandardOutput()).split('\n');
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0 || lines.isEmpty() ||
        !lines.first().startsWith("#ENGINE ")) {
        if (error) *error = "El convertidor de romaji falló: " + QString::fromUtf8(p.readAllStandardError()).left(300);
        return result;
    }
    if (engine) *engine = lines.first().mid(8).trimmed();
    if (lines.size() - 1 < toSend.size()) {
        if (error) *error = "El convertidor de romaji devolvió menos líneas de las esperadas.";
        return result;
    }
    for (int k = 0; k < idx.size(); ++k) result[idx[k]] = lines[1 + k].trimmed();
    return result;
}
