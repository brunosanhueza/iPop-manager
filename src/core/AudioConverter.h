#pragma once

#include <QString>
#include <QFileInfo>
#include <functional>
#include "Track.h"

struct CompatibilityCheck {
    bool isSupported{true};
    QString detectedFormat;
    QString targetFormat;
    QString reason;
};

class AudioConverter {
public:
    static QString findFFmpegBinary();

    static CompatibilityCheck checkCompatibility(const QString& filePath, const QString& ipodModelName);

    static Track extractMetadata(const QString& filePath);

    static bool convertToCompatibleFormat(
        const QString& inputPath,
        const QString& outputPath,
        std::function<void(int percentage, const QString& status)> progressCb = nullptr
    );
};
