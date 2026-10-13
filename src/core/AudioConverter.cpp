#include "AudioConverter.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>
#include <QDateTime>
#include <QDebug>

#include <QStandardPaths>

QString AudioConverter::findFFmpegBinary() {
#if defined(Q_OS_WIN)
    QString exeName = "ffmpeg.exe";
#else
    QString exeName = "ffmpeg";
#endif

    // 1. Binario empaquetado junto a la aplicación o en carpeta tools
    QStringList candidates = {
        QDir(QCoreApplication::applicationDirPath()).filePath("tools/" + exeName),
        QDir(QCoreApplication::applicationDirPath()).filePath(exeName),
        QDir::current().filePath("tools/" + exeName),
        QDir::current().filePath(exeName)
    };

    for (const auto& path : candidates) {
        if (QFile::exists(path)) {
            return QDir::toNativeSeparators(path);
        }
    }

    // 2. Buscar en el PATH del sistema de forma multiplataforma (Windows y Linux)
    QString systemFfmpeg = QStandardPaths::findExecutable("ffmpeg");
    if (!systemFfmpeg.isEmpty()) {
        return systemFfmpeg;
    }

#if defined(Q_OS_LINUX)
    // 3. Rutas comunes en distribuciones de Linux (Arch, Pop!_OS, Ubuntu, Fedora)
    QStringList linuxPaths = {"/usr/bin/ffmpeg", "/usr/local/bin/ffmpeg", "/bin/ffmpeg"};
    for (const auto& lp : linuxPaths) {
        if (QFile::exists(lp)) return lp;
    }
#endif

    return "ffmpeg";
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
    t.artist = "Artista Desconocido";
    t.album = "Álbum Desconocido";
    t.fileType = fi.suffix().toUpper();
    t.sizeBytes = fi.size();
    t.durationMs = 180000; // 3 min por defecto
    t.bitrate = 256;
    t.sampleRate = 44100;
    t.trackNumber = 1;
    t.year = QDate::currentDate().year();

    QString ffmpeg = findFFmpegBinary();
    QProcess proc;
    QStringList args;
    args << "-i" << filePath << "-f" << "ffmetadata" << "-";
    proc.start(ffmpeg, args);
    if (!proc.waitForFinished(5000)) {
        proc.kill();
        return t;
    }

    QString metaOutput = QString::fromUtf8(proc.readAllStandardOutput());
    QString errOutput = QString::fromUtf8(proc.readAllStandardError());

    // Parsear líneas de ffmetadata
    const QStringList lines = metaOutput.split('\n');
    for (const auto& line : lines) {
        int eq = line.indexOf('=');
        if (eq > 0) {
            QString key = line.left(eq).trimmed().toLower();
            QString val = line.mid(eq + 1).trimmed();
            if (!val.isEmpty()) {
                if (key == "title") t.title = val;
                else if (key == "artist") t.artist = val;
                else if (key == "album") t.album = val;
                else if (key == "genre") t.genre = val;
                else if (key == "track") {
                    int slash = val.indexOf('/');
                    t.trackNumber = (slash > 0 ? val.left(slash) : val).toInt();
                }
                else if (key == "date" || key == "year") {
                    t.year = val.left(4).toInt();
                }
            }
        }
    }

    // Extraer duración y bitrate desde stderr de ffmpeg: Duration: 00:03:45.67, bitrate: 256 kb/s
    static QRegularExpression durRegex(R"(Duration:\s*(\d{2}):(\d{2}):(\d{2})\.(\d{2}))");
    auto durMatch = durRegex.match(errOutput);
    if (durMatch.hasMatch()) {
        int hours = durMatch.captured(1).toInt();
        int mins = durMatch.captured(2).toInt();
        int secs = durMatch.captured(3).toInt();
        int ms = durMatch.captured(4).toInt() * 10;
        t.durationMs = ((hours * 3600) + (mins * 60) + secs) * 1000 + ms;
    }

    static QRegularExpression brRegex(R"(bitrate:\s*(\d+)\s*kb/s)");
    auto brMatch = brRegex.match(errOutput);
    if (brMatch.hasMatch()) {
        t.bitrate = brMatch.captured(1).toInt();
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
