#pragma once

#include <QString>
#include <QList>
#include "Track.h"

class Playlist {
public:
    uint32_t id{0};
    QString name{"Nueva Playlist"};
    bool isMaster{false};      // Lista maestra (Todas las canciones)
    bool isPodcasts{false};
    QList<Track> tracks;

    int trackCount() const { return tracks.size(); }

    uint64_t totalBytes() const {
        uint64_t sum = 0;
        for (const auto& t : tracks) {
            sum += t.sizeBytes;
        }
        return sum;
    }

    int totalDurationMs() const {
        int sum = 0;
        for (const auto& t : tracks) {
            sum += t.durationMs;
        }
        return sum;
    }

    QString formattedSummary() const {
        int totalSecs = totalDurationMs() / 1000;
        int hours = totalSecs / 3600;
        int mins = (totalSecs % 3600) / 60;

        QString timeStr;
        if (hours > 0) {
            timeStr = QString("%1 h %2 min").arg(hours).arg(mins);
        } else {
            timeStr = QString("%1 min").arg(mins);
        }

        constexpr double MB = 1024.0 * 1024.0;
        double mb = static_cast<double>(totalBytes()) / MB;
        QString sizeStr = (mb >= 1024.0) ? QString::asprintf("%.2f GB", mb / 1024.0) : QString::asprintf("%.1f MB", mb);

        return QString("%1 canciones • %2 • %3").arg(trackCount()).arg(timeStr).arg(sizeStr);
    }
};
