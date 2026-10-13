#pragma once

#include <QString>
#include <cstdint>

class Track {
public:
    uint32_t id{0};
    QString title{"Desconocido"};
    QString artist{"Artista Desconocido"};
    QString album{"Álbum Desconocido"};
    QString genre{""};
    int durationMs{0};
    int bitrate{0};            // En kbps
    int sampleRate{44100};     // En Hz
    uint64_t sizeBytes{0};
    QString fileType{"MP3"};
    QString filePath{""};      // Ruta dentro del iPod
    int trackNumber{1};
    int year{2026};
    bool selected{false};

    QString formattedDuration() const {
        int totalSecs = durationMs / 1000;
        int mins = totalSecs / 60;
        int secs = totalSecs % 60;
        return QString::asprintf("%d:%02d", mins, secs);
    }

    QString formattedBitrate() const {
        if (bitrate <= 0) return "—";
        return QString("%1 kbps").arg(bitrate);
    }

    QString formattedSize() const {
        constexpr double MB = 1024.0 * 1024.0;
        double mb = static_cast<double>(sizeBytes) / MB;
        return QString::asprintf("%.1f MB", mb);
    }
};
