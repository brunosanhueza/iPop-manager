#pragma once

#include <QString>
#include <QByteArray>

class ITunesHash58 {
public:
    static bool updateHash(QByteArray& itunesDbData, const QString& firewireGuid);
    static QString readFirewireGuid(const QString& mountPath);
};
