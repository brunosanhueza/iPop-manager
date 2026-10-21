#include "AudioConverter.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>
#include <QDateTime>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

QString AudioConverter::findFFmpegBinary() {
#ifdef Q_OS_WIN

    QStringList candidates = {
        QDir(QCoreApplication::applicationDirPath()).filePath("tools/ffmpeg.exe"),
        QDir::current().filePath("tools/ffmpeg.exe"),
        QDir("C:/Users/Zacky/Desktop/iPop").filePath("tools/ffmpeg.exe"),
        "ffmpeg"
    };

    for (const auto& path : candidates) {
        if (QFile::exists(path)) {
            return QDir::toNativeSeparators(path);
        }
    }

    QString found = QProcessEnvironment::systemEnvironment().value("PATH");
    for (const auto& dirStr : found.split(';')) {
        QString testPath = QDir(dirStr).filePath("ffmpeg.exe");
        if (QFile::exists(testPath)) {
            return QDir::toNativeSeparators(testPath);
        }
    }

    return "ffmpeg";
#else
    // En Linux/macOS NUNCA usar tools/ffmpeg.exe (binario de Windows): usar el ffmpeg del sistema
    const QString sys = QStandardPaths::findExecutable("ffmpeg");
    return sys.isEmpty() ? QString("ffmpeg") : sys;
#endif
}

QString AudioConverter::findFFprobeBinary() {
#ifdef Q_OS_WIN
    const QFileInfo ff(findFFmpegBinary());
    const QString probe = ff.dir().filePath("ffprobe.exe");
    return QFile::exists(probe) ? QDir::toNativeSeparators(probe) : QString();
#else
    return QStandardPaths::findExecutable("ffprobe");
#endif
}

bool AudioConverter::splitArtistTitle(const QString& text, QString& artist, QString& title) {
    static const QStringList seps{QStringLiteral(" - "), QStringLiteral(" – "), QStringLiteral(" — ")};
    int best = -1, sepLen = 0;
    for (const QString& sep : seps) {
        const int i = text.indexOf(sep);
        if (i > 0 && (best < 0 || i < best)) { best = i; sepLen = sep.size(); }
    }
    if (best <= 0) return false;
    const QString a = text.left(best).trimmed();
    const QString t = text.mid(best + sepLen).trimmed();
    if (a.isEmpty() || t.isEmpty() || a.size() > 80) return false;
    artist = a;
    title = t;
    return true;
}

CompatibilityCheck AudioConverter::checkCompatibility(const QString& filePath, const QString& ipodModelName) {
    QFileInfo fi(filePath);
    QString ext = fi.suffix().toLower();

    CompatibilityCheck check;
    check.detectedFormat = ext.toUpper();
    check.targetFormat = "M4A (AAC)";

    QString model = ipodModelName.toUpper();
    bool isOldGen = model.contains("1G") || model.contains("2G");

    if (isOldGen) {
        check.targetFormat = "MP3 (320 kbps)";
    }

    if (ext == "mp3" || ext == "wav" || ext == "aif" || ext == "aiff") {
        check.isSupported = true;
        return check;
    }

    if (ext == "m4a" || ext == "aac" || ext == "m4b") {
        if (isOldGen) {
            check.isSupported = false;
            check.reason = QString("Tu %1 no soporta códec AAC de forma nativa. Requiere conversión a MP3.").arg(ipodModelName);
            return check;
        }
        check.isSupported = true;
        return check;
    }

    // Formatos no soportados por ningún iPod
    check.isSupported = false;
    if (ext == "flac") {
        check.reason = QString("El formato FLAC (Lossless) no es reproducible en el firmware nativo del %1.").arg(ipodModelName);
    } else if (ext == "ogg" || ext == "oga") {
        check.reason = QString("El formato Ogg Vorbis no está soportado por Apple iPod.").arg(ipodModelName);
    } else if (ext == "opus") {
        check.reason = QString("El formato Opus no es compatible con el reproductor de hardware del iPod.");
    } else if (ext == "wma") {
        check.reason = QString("El formato Windows Media Audio (WMA) no es compatible con el iPod.");
    } else if (ext == "ape" || ext == "wv") {
        check.reason = QString("Formato de audio comprimido no estándar para iPod.");
    } else {
        check.reason = QString("El formato .%1 no es compatible directamente con el iPod.").arg(ext);
    }

    return check;
}

Track AudioConverter::extractMetadata(const QString& filePath) {
    QFileInfo fi(filePath);
    Track t;
    t.title = fi.completeBaseName();
    t.fileType = fi.suffix().toUpper();
    t.sizeBytes = fi.size();
    // Track trae valores por defecto ("Artista Desconocido", año actual...): partir de cero.
    t.artist.clear();
    t.album.clear();
    t.genre.clear();
    t.durationMs = 0;      // 0 = desconocida (antes se inventaban 3 min)
    t.bitrate = 0;
    t.sampleRate = 44100;
    t.trackNumber = 0;
    t.year = 0;

    bool hasTagTitle = false;
    auto applyTag = [&](const QString& rawKey, const QString& rawVal) {
        const QString key = rawKey.trimmed().toLower();
        const QString val = rawVal.trimmed();
        if (val.isEmpty()) return;
        if (key == "title") { t.title = val; hasTagTitle = true; }
        else if (key == "artist") t.artist = val;
        else if (key == "album_artist" || key == "albumartist") { if (t.artist.isEmpty()) t.artist = val; }
        else if (key == "album") t.album = val;
        else if (key == "genre") t.genre = val;
        else if (key == "track") {
            const int slash = val.indexOf('/');
            t.trackNumber = (slash > 0 ? val.left(slash) : val).toInt();
        } else if (key == "date" || key == "year") {
            const int y = val.left(4).toInt();
            if (y > 1000) t.year = y;
        }
    };

    bool probed = false;
    const QString probe = findFFprobeBinary();
    if (!probe.isEmpty()) {
        QProcess p;
        p.start(probe, {"-v", "error", "-print_format", "json", "-show_format", "-show_streams", filePath});
        if (p.waitForStarted(3000) && p.waitForFinished(15000) && p.exitStatus() == QProcess::NormalExit) {
            const QJsonDocument doc = QJsonDocument::fromJson(p.readAllStandardOutput());
            if (doc.isObject()) {
                const QJsonObject root = doc.object();
                const QJsonObject fmt = root["format"].toObject();
                bool ok = false;
                const double dur = fmt["duration"].toString().toDouble(&ok);
                if (ok && dur > 0) t.durationMs = qRound(dur * 1000.0);
                const double br = fmt["bit_rate"].toString().toDouble(&ok);
                if (ok && br > 0) t.bitrate = qRound(br / 1000.0);

                const QJsonObject ftags = fmt["tags"].toObject();
                for (auto it = ftags.begin(); it != ftags.end(); ++it) applyTag(it.key(), it.value().toString());

                for (const QJsonValue& sv : root["streams"].toArray()) {
                    const QJsonObject so = sv.toObject();
                    if (so["codec_type"].toString() != "audio") continue;
                    const int sr = so["sample_rate"].toString().toInt();
                    if (sr > 0) t.sampleRate = sr;
                    if (t.durationMs == 0) {
                        const double sd = so["duration"].toString().toDouble(&ok);
                        if (ok && sd > 0) t.durationMs = qRound(sd * 1000.0);
                    }
                    const QJsonObject stags = so["tags"].toObject();
                    for (auto it = stags.begin(); it != stags.end(); ++it) {
                        const QString k = it.key().toLower();
                        if ((k == "title" && !hasTagTitle) || (k == "artist" && t.artist.isEmpty()) ||
                            (k == "album" && t.album.isEmpty()))
                            applyTag(it.key(), it.value().toString());
                    }
                    break;
                }
                probed = true;
            }
        } else {
            p.kill();
        }
    }

    if (!probed) {
        // Respaldo sin ffprobe: leer con ffmpeg
        QProcess proc;
        proc.start(findFFmpegBinary(), {"-hide_banner", "-i", filePath, "-f", "ffmetadata", "-"});
        if (proc.waitForStarted(3000) && proc.waitForFinished(10000)) {
            const QString metaOutput = QString::fromUtf8(proc.readAllStandardOutput());
            const QString errOutput = QString::fromUtf8(proc.readAllStandardError());
            for (const QString& line : metaOutput.split('\n')) {
                const int eq = line.indexOf('=');
                if (eq > 0) applyTag(line.left(eq), line.mid(eq + 1));
            }
            static QRegularExpression durRegex(R"(Duration:\s*(\d{2}):(\d{2}):(\d{2})\.(\d{2}))");
            const auto dm = durRegex.match(errOutput);
            if (dm.hasMatch()) {
                t.durationMs = ((dm.captured(1).toInt() * 3600) + (dm.captured(2).toInt() * 60) + dm.captured(3).toInt()) * 1000
                               + dm.captured(4).toInt() * 10;
            }
            static QRegularExpression brRegex(R"(bitrate:\s*(\d+)\s*kb/s)");
            const auto bm = brRegex.match(errOutput);
            if (bm.hasMatch()) t.bitrate = bm.captured(1).toInt();
        } else {
            proc.kill();
            qWarning() << "extractMetadata: no se pudo leer" << filePath << "(¿ffmpeg/ffprobe instalado?)";
        }
    }

    // Artista: si las etiquetas no lo traen, sacarlo de "Artista - Titulo" (titulo o nombre del archivo)
    QString a, ti;
    if (t.artist.isEmpty()) {
        if (hasTagTitle && splitArtistTitle(t.title, a, ti)) {
            t.artist = a; t.title = ti;
        } else if (splitArtistTitle(fi.completeBaseName(), a, ti)) {
            t.artist = a;
            if (!hasTagTitle) t.title = ti;
        }
    } else if (splitArtistTitle(t.title, a, ti) && a.compare(t.artist, Qt::CaseInsensitive) == 0) {
        t.title = ti;   // "Kikuo - Cancion" con artista "Kikuo" -> "Cancion"
    }

    return t;
}

bool AudioConverter::convertToCompatibleFormat(
    const QString& inputPath,
    const QString& outputPath,
    std::function<void(int percentage, const QString& status)> progressCb
) {
    QString ffmpeg = findFFmpegBinary();
    QFileInfo inFi(inputPath);
    QFileInfo outFi(outputPath);

    QDir().mkpath(outFi.absolutePath());

    QStringList args;
    args << "-y" << "-i" << inputPath << "-vn";

    if (outFi.suffix().toLower() == "mp3") {
        args << "-c:a" << "libmp3lame" << "-b:a" << "320k" << "-ar" << "44100";
    } else {
        // Formato AAC / M4A nativo de Apple
        args << "-c:a" << "aac" << "-b:a" << "256k" << "-ar" << "44100";
    }

    args << outputPath;

    QProcess proc;
    proc.start(ffmpeg, args);

    if (progressCb) {
        progressCb(10, QString("Iniciando conversión de %1...").arg(inFi.fileName()));
    }

    if (!proc.waitForStarted(4000)) {
        return false;
    }

    int step = 20;
    while (!proc.waitForFinished(400)) {
        if (progressCb) {
            step = std::min(step + 10, 90);
            progressCb(step, QString("Codificando audio a formato compatible para iPod... (%1%)").arg(step));
        }
    }

    bool success = (proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0 && QFile::exists(outputPath));
    if (progressCb) {
        progressCb(100, success ? "Conversión completada." : "Error en la conversión.");
    }

    return success;
}
