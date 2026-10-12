#include "TrackTableModel.h"

TrackTableModel::TrackTableModel(QObject* parent)
    : QAbstractTableModel(parent)
{
}

int TrackTableModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return m_tracks.size();
}

int TrackTableModel::columnCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return ColumnCount;
}

QVariant TrackTableModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_tracks.size()) {
        return QVariant();
    }

    const auto& track = m_tracks.at(index.row());

    if (role == Qt::CheckStateRole && index.column() == ColCheck) {
        return track.selected ? Qt::Checked : Qt::Unchecked;
    }

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
            case ColTitle:    return track.title;
            case ColArtist:   return track.artist;
            case ColAlbum:    return track.album;
            case ColQuality:  return track.formattedBitrate();
            case ColDuration: return track.formattedDuration();
            case ColFormat:   return track.fileType;
            case ColSize:     return track.formattedSize();
            default: break;
        }
    }

    if (role == Qt::TextAlignmentRole) {
        switch (index.column()) {
            case ColCheck:    return Qt::AlignCenter;
            case ColQuality:  return Qt::AlignCenter;
            case ColDuration: return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
            case ColFormat:   return Qt::AlignCenter;
            case ColSize:     return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
            default:          return static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter);
        }
    }

    return QVariant();
}

bool TrackTableModel::setData(const QModelIndex& index, const QVariant& value, int role) {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_tracks.size()) {
        return false;
    }

    if (index.column() == ColCheck && role == Qt::CheckStateRole) {
        m_tracks[index.row()].selected = (value.toInt() == Qt::Checked);
        emit dataChanged(index, index, {Qt::CheckStateRole});
        emit selectionChanged(selectedCount());
        return true;
    }

    return false;
}

QVariant TrackTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
        switch (section) {
            case ColCheck:    return "✓";
            case ColTitle:    return "Título";
            case ColArtist:   return "Artista";
            case ColAlbum:    return "Álbum";
            case ColQuality:  return "Calidad";
            case ColDuration: return "Duración";
            case ColFormat:   return "Formato";
            case ColSize:     return "Tamaño";
            default: break;
        }
    }
    return QVariant();
}

Qt::ItemFlags TrackTableModel::flags(const QModelIndex& index) const {
    if (!index.isValid()) return Qt::NoItemFlags;

    Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (index.column() == ColCheck) {
        f |= Qt::ItemIsUserCheckable;
    }
    return f;
}

void TrackTableModel::setTracks(const QList<Track>& tracks) {
    beginResetModel();
    m_tracks = tracks;
    endResetModel();
    emit selectionChanged(selectedCount());
}

void TrackTableModel::selectAll(bool select) {
    if (m_tracks.isEmpty()) return;

    for (auto& t : m_tracks) {
        t.selected = select;
    }

    QModelIndex topLeft = index(0, ColCheck);
    QModelIndex bottomRight = index(m_tracks.size() - 1, ColCheck);
    emit dataChanged(topLeft, bottomRight, {Qt::CheckStateRole});
    emit selectionChanged(selectedCount());
}

QList<uint32_t> TrackTableModel::selectedTrackIds() const {
    QList<uint32_t> ids;
    for (const auto& t : m_tracks) {
        if (t.selected) ids.append(t.id);
    }
    return ids;
}

int TrackTableModel::selectedCount() const {
    int count = 0;
    for (const auto& t : m_tracks) {
        if (t.selected) ++count;
    }
    return count;
}
