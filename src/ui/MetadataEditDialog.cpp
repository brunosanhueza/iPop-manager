#include "MetadataEditDialog.h"
#include "../core/AudioConverter.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSet>
#include <QSettings>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
enum Col { ColFile = 0, ColTitle, ColArtist, ColAlbum, ColDuration, ColCount };

QString fmtDuration(int ms) {
    if (ms <= 0) return QStringLiteral("?");
    const int total = (ms + 500) / 1000;
    return QString("%1:%2").arg(total / 60).arg(total % 60, 2, 10, QChar('0'));
}
}

MetadataEditDialog::MetadataEditDialog(const QStringList& files, const QList<Track>& tracks, QWidget* parent)
    : QDialog(parent), m_tracks(tracks)
{
    setWindowTitle("Revisar canciones antes de añadir");
    resize(980, 580);

    auto* root = new QVBoxLayout(this);

    auto* info = new QLabel(
        "Revisa el título y el artista de cada canción (doble clic para editar). "
        "Lo que escribas aquí es lo que se guardará en el iPod. "
        "Si una canción no tiene artista en sus etiquetas, puedes sacarlo del nombre «Artista - Título» o escribirlo a mano.");
    info->setWordWrap(true);
    root->addWidget(info);

    auto* bar = new QHBoxLayout();
    bar->addWidget(new QLabel("Artista:"));
    m_artistEdit = new QLineEdit();
    m_artistEdit->setPlaceholderText("Escribe un artista para aplicarlo");
    bar->addWidget(m_artistEdit, 1);
    auto* applyBtn = new QPushButton("Aplicar a seleccionadas (o a todas)");
    bar->addWidget(applyBtn);
    auto* splitBtn = new QPushButton("Artista desde «Artista - Título»");
    splitBtn->setToolTip("En las filas sin artista, separa el título en artista y título usando « - »");
    bar->addWidget(splitBtn);
    root->addLayout(bar);

    m_table = new QTableWidget(files.size(), ColCount);
    m_table->setHorizontalHeaderLabels({"Archivo", "Título", "Artista", "Álbum", "Duración"});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setAlternatingRowColors(true);
    m_table->horizontalHeader()->setSectionResizeMode(ColFile, QHeaderView::Interactive);
    m_table->horizontalHeader()->setSectionResizeMode(ColTitle, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(ColArtist, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(ColAlbum, QHeaderView::Interactive);
    m_table->setColumnWidth(ColFile, 200);
    m_table->setColumnWidth(ColAlbum, 140);
    m_table->setColumnWidth(ColDuration, 70);

    for (int i = 0; i < files.size() && i < m_tracks.size(); ++i) {
        const Track& t = m_tracks.at(i);
        auto* fileItem = new QTableWidgetItem(QFileInfo(files.at(i)).fileName());
        fileItem->setFlags(fileItem->flags() & ~Qt::ItemIsEditable);
        fileItem->setToolTip(files.at(i));
        m_table->setItem(i, ColFile, fileItem);
        m_table->setItem(i, ColTitle, new QTableWidgetItem(t.title));
        m_table->setItem(i, ColArtist, new QTableWidgetItem(t.artist));
        m_table->setItem(i, ColAlbum, new QTableWidgetItem(t.album));
        auto* durItem = new QTableWidgetItem(fmtDuration(t.durationMs));
        durItem->setFlags(durItem->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(i, ColDuration, durItem);
    }
    root->addWidget(m_table, 1);

    m_alwaysChk = new QCheckBox("Mostrar siempre esta ventana al añadir canciones (si lo desmarcas, solo aparece cuando falte el artista)");
    m_alwaysChk->setChecked(alwaysReview());
    root->addWidget(m_alwaysChk);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText("Añadir al iPod");
    buttons->button(QDialogButtonBox::Cancel)->setText("Cancelar");
    root->addWidget(buttons);

    connect(applyBtn, &QPushButton::clicked, this, &MetadataEditDialog::applyArtistToRows);
    connect(splitBtn, &QPushButton::clicked, this, &MetadataEditDialog::artistFromTitle);
    connect(buttons, &QDialogButtonBox::accepted, this, &MetadataEditDialog::onAccepted);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

bool MetadataEditDialog::alwaysReview() {
    return QSettings().value("import/reviewAlways", true).toBool();
}

QList<int> MetadataEditDialog::targetRows() const {
    QSet<int> sel;
    for (const auto& idx : m_table->selectionModel()->selectedRows()) sel.insert(idx.row());
    QList<int> rows;
    if (sel.isEmpty()) {
        for (int r = 0; r < m_table->rowCount(); ++r) rows.append(r);
    } else {
        rows = sel.values();
    }
    return rows;
}

void MetadataEditDialog::applyArtistToRows() {
    const QString artist = m_artistEdit->text().trimmed();
    if (artist.isEmpty()) return;
    for (int r : targetRows()) m_table->item(r, ColArtist)->setText(artist);
}

void MetadataEditDialog::artistFromTitle() {
    for (int r : targetRows()) {
        if (!m_table->item(r, ColArtist)->text().trimmed().isEmpty()) continue;
        QString artist, title;
        // primero el titulo; si no, el nombre del archivo
        if (AudioConverter::splitArtistTitle(m_table->item(r, ColTitle)->text(), artist, title) ||
            AudioConverter::splitArtistTitle(QFileInfo(m_table->item(r, ColFile)->toolTip()).completeBaseName(), artist, title)) {
            m_table->item(r, ColArtist)->setText(artist);
            m_table->item(r, ColTitle)->setText(title);
        }
    }
}

QList<Track> MetadataEditDialog::tracks() const {
    QList<Track> out = m_tracks;
    for (int r = 0; r < out.size() && r < m_table->rowCount(); ++r) {
        const QString title = m_table->item(r, ColTitle)->text().trimmed();
        if (!title.isEmpty()) out[r].title = title;
        out[r].artist = m_table->item(r, ColArtist)->text().trimmed();
        out[r].album = m_table->item(r, ColAlbum)->text().trimmed();
    }
    return out;
}

void MetadataEditDialog::onAccepted() {
    QSettings().setValue("import/reviewAlways", m_alwaysChk->isChecked());
    accept();
}
