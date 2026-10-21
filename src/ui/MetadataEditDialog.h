#pragma once

#include <QDialog>
#include <QList>
#include <QStringList>
#include "../core/Track.h"

class QTableWidget;
class QLineEdit;
class QCheckBox;

// Ventana para revisar/editar titulo, artista y album antes de copiar las canciones al iPod.
class MetadataEditDialog : public QDialog {
    Q_OBJECT
public:
    MetadataEditDialog(const QStringList& files, const QList<Track>& tracks, QWidget* parent = nullptr);

    // Pistas con los cambios hechos por el usuario (mismo orden que 'files')
    QList<Track> tracks() const;

    // true = mostrar siempre esta ventana al importar (si es false, solo cuando falte el artista)
    static bool alwaysReview();

private slots:
    void applyArtistToRows();
    void artistFromTitle();
    void onAccepted();

private:
    QList<int> targetRows() const;

    QList<Track> m_tracks;
    QTableWidget* m_table{nullptr};
    QLineEdit* m_artistEdit{nullptr};
    QCheckBox* m_alwaysChk{nullptr};
};
