#pragma once

#include <QString>
#include <QList>
#include <QHash>
#include <functional>
#include "Track.h"

class TrackImporter {
public:
    static bool importTracksToDevice(
        const QString& mountPath,
        const QString& firewireGuid,
        const QList<QString>& sourceFilePaths,
        std::function<void(int current, int total, uint64_t currentBytes, uint64_t totalBytes, const QString& status)> progressCb = nullptr,
        const QString& playlistName = QString(),
        const QHash<QString, Track>& metadata = QHash<QString, Track>()
    );

private:
    static QString allocateIpodPath(const QString& mountPath, const QString& extension, QString& outApplePath);
    static bool insertTracksIntoDatabase(const QString& dbPath, const QString& firewireGuid, const QList<QPair<Track, QString>>& importedTracks);
};
