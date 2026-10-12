#pragma once

#include <QAbstractTableModel>
#include <QList>
#include "../core/Track.h"

class TrackTableModel : public QAbstractTableModel {
    Q_OBJECT

public:
    enum Column {
        ColCheck = 0,
        ColTitle,
        ColArtist,
        ColAlbum,
        ColQuality,
        ColDuration,
        ColFormat,
        ColSize,
        ColumnCount
    };

    explicit TrackTableModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;

    void setTracks(const QList<Track>& tracks);
    const QList<Track>& tracks() const { return m_tracks; }

    void selectAll(bool select);
    QList<uint32_t> selectedTrackIds() const;
    int selectedCount() const;

signals:
    void selectionChanged(int selectedCount);

private:
    QList<Track> m_tracks;
};
