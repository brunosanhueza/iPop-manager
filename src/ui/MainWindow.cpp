#include "MainWindow.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QFileDialog>
#include <QMessageBox>
#include <QProcess>
#include <QStatusBar>
#include <QFrame>
#include <QHeaderView>
#include <QTimer>

#ifdef Q_OS_WIN
#include <windows.h>
#include <tlhelp32.h>
#endif

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle("iPop - Gestor de iPod");
    resize(1180, 780);
    setMinimumSize(980, 640);

    setupUi();

    auto* itunesTimer = new QTimer(this);
    connect(itunesTimer, &QTimer::timeout, this, &MainWindow::checkITunesStatus);
    itunesTimer->start(2000);
    checkITunesStatus();

    connect(&m_detector, &DeviceDetector::devicesRefreshed, this, [this](const QStringList& mounts) {
        m_deviceCombo->blockSignals(true);
        m_deviceCombo->clear();
        if (mounts.isEmpty()) {
            m_deviceCombo->addItem("Ningún iPod detectado automáticamente");
        } else {
            for (const QString& m : mounts) {
                IPodDevice dev(m);
                QString text = QString("%1 (%2)").arg(dev.modelName(), m);
                m_deviceCombo->addItem(text, m);
            }
        }
        m_deviceCombo->blockSignals(false);

        if (!mounts.isEmpty()) {
            onDeviceSelected(0);
        } else {
            m_currentDevice.reset();
            updateDeviceView();
        }
    });

    onRefreshDevices();
}

void MainWindow::setupUi() {
    auto* centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    auto* rootLayout = new QVBoxLayout(centralWidget);
    rootLayout->setContentsMargins(16, 14, 16, 14);
    rootLayout->setSpacing(12);

    // 1. Barra Superior: Selector de Dispositivo y Acciones
    auto* topBar = new QFrame(this);
    topBar->setObjectName("topBar");
    topBar->setStyleSheet(
        "#topBar { background-color: #1E293B; border-radius: 10px; padding: 6px 12px; }"
    );
    auto* topLayout = new QHBoxLayout(topBar);
    topLayout->setContentsMargins(10, 6, 10, 6);
    topLayout->setSpacing(10);

    auto* brandLabel = new QLabel("iPop", this);
    brandLabel->setStyleSheet("font-size: 20px; font-weight: bold; color: #38BDF8; letter-spacing: 1px;");
    topLayout->addWidget(brandLabel);

    topLayout->addSpacing(12);

    auto* selectLabel = new QLabel("iPod:", this);
    selectLabel->setStyleSheet("color: #94A3B8; font-weight: 500;");
    topLayout->addWidget(selectLabel);

    m_deviceCombo = new QComboBox(this);
    m_deviceCombo->setMinimumWidth(340);
    m_deviceCombo->setStyleSheet(
        "QComboBox { background-color: #0F172A; color: #F8FAFC; border: 1px solid #334155; "
        "border-radius: 6px; padding: 6px 12px; font-size: 13px; } "
        "QComboBox::drop-down { border: none; } "
        "QComboBox QAbstractItemView { background-color: #0F172A; color: #F8FAFC; selection-background-color: #2563EB; }"
    );
    connect(m_deviceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onDeviceSelected);
    topLayout->addWidget(m_deviceCombo, 1);

    m_refreshBtn = new QPushButton("Refrescar", this);
    m_refreshBtn->setStyleSheet(
        "QPushButton { background-color: #334155; color: #F8FAFC; border-radius: 6px; padding: 6px 14px; font-weight: 500; } "
        "QPushButton:hover { background-color: #475569; }"
    );
    connect(m_refreshBtn, &QPushButton::clicked, this, &MainWindow::onRefreshDevices);
    topLayout->addWidget(m_refreshBtn);

    m_browseBtn = new QPushButton("Elegir carpeta...", this);
    m_browseBtn->setStyleSheet(
        "QPushButton { background-color: #334155; color: #F8FAFC; border-radius: 6px; padding: 6px 14px; font-weight: 500; } "
        "QPushButton:hover { background-color: #475569; }"
    );
    connect(m_browseBtn, &QPushButton::clicked, this, &MainWindow::onBrowseCustomMount);
    topLayout->addWidget(m_browseBtn);

    m_ejectBtn = new QPushButton("Expulsar", this);
    m_ejectBtn->setStyleSheet(
        "QPushButton { background-color: #DC2626; color: white; border-radius: 6px; padding: 6px 16px; font-weight: bold; } "
        "QPushButton:hover { background-color: #B91C1C; }"
    );
    connect(m_ejectBtn, &QPushButton::clicked, this, &MainWindow::onEjectDevice);
    topLayout->addWidget(m_ejectBtn);

    rootLayout->addWidget(topBar);

    // Banner de advertencia de conflicto con iTunes
    m_itunesWarningFrame = new QFrame(this);
    m_itunesWarningFrame->setStyleSheet("background-color: #7C2D12; border: 1px solid #EA580C; border-radius: 8px; padding: 6px 12px;");
    auto* warnLayout = new QHBoxLayout(m_itunesWarningFrame);
    warnLayout->setContentsMargins(10, 4, 10, 4);
    warnLayout->setSpacing(10);
    auto* warnLabel = new QLabel("⚠️ iTunes está abierto en segundo plano. iTunes mantiene la base de datos en RAM y no detecta cambios externos ni vacía los archivos borrados.", m_itunesWarningFrame);
    warnLabel->setStyleSheet("color: #FFEDD5; font-size: 12px; font-weight: 500;");
    warnLayout->addWidget(warnLabel, 1);
    auto* closeItunesBtn = new QPushButton("Cerrar iTunes", m_itunesWarningFrame);
    closeItunesBtn->setStyleSheet("background-color: #DC2626; color: white; border-radius: 4px; padding: 4px 12px; font-weight: bold; font-size: 11px;");
    connect(closeItunesBtn, &QPushButton::clicked, this, [this]() {
        killITunesProcess();
        checkITunesStatus();
    });
    warnLayout->addWidget(closeItunesBtn);
    m_itunesWarningFrame->setVisible(false);
    rootLayout->addWidget(m_itunesWarningFrame);

    // 2. Zona Central: Organizador de Carpetas / Ficheros (Árbol) a la izquierda y Tabla de Canciones a la derecha
    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setHandleWidth(4);
    splitter->setStyleSheet("QSplitter::handle { background-color: #334155; }");

    // 2.1 Sidebar Izquierda: Árbol Organizador (Estilo iTunes / Gestor de Archivos)
    auto* treeWidget = new QFrame(this);
    treeWidget->setStyleSheet("background-color: #1E293B; border-radius: 10px;");
    auto* treeLayout = new QVBoxLayout(treeWidget);
    treeLayout->setContentsMargins(10, 12, 10, 12);
    treeLayout->setSpacing(8);

    auto* treeHeader = new QLabel("EXPLORADOR DEL DISPOSITIVO", this);
    treeHeader->setStyleSheet("color: #64748B; font-size: 11px; font-weight: bold; letter-spacing: 1px; padding-left: 6px;");
    treeLayout->addWidget(treeHeader);

    m_treeOrganizer = new QTreeWidget(this);
    m_treeOrganizer->setHeaderHidden(true);
    m_treeOrganizer->setAnimated(true);
    m_treeOrganizer->setStyleSheet(
        "QTreeWidget { background-color: transparent; border: none; color: #E2E8F0; font-size: 13px; } "
        "QTreeWidget::item { padding: 6px 8px; border-radius: 6px; margin: 1px 0; } "
        "QTreeWidget::item:hover { background-color: #334155; } "
        "QTreeWidget::item:selected { background-color: #2563EB; color: white; font-weight: 500; }"
    );
    connect(m_treeOrganizer, &QTreeWidget::itemClicked, this, &MainWindow::onTreeItemClicked);
    treeLayout->addWidget(m_treeOrganizer, 1);

    splitter->addWidget(treeWidget);
    splitter->setStretchFactor(0, 0);
    treeWidget->setMinimumWidth(240);
    treeWidget->setMaximumWidth(320);

    // 2.2 Panel de Canciones (Derecha)
    auto* songsWidget = new QFrame(this);
    songsWidget->setStyleSheet("background-color: #1E293B; border-radius: 10px;");
    auto* songsLayout = new QVBoxLayout(songsWidget);
    songsLayout->setContentsMargins(16, 14, 16, 14);
    songsLayout->setSpacing(10);

    // Encabezado de la Vista Seleccionada
    auto* songsHeaderRow = new QHBoxLayout();
    auto* titleLayout = new QVBoxLayout();
    titleLayout->setSpacing(2);

    m_viewTitleLabel = new QLabel("Música", this);
    m_viewTitleLabel->setStyleSheet("font-size: 20px; font-weight: bold; color: #F8FAFC;");
    titleLayout->addWidget(m_viewTitleLabel);

    m_viewSummaryLabel = new QLabel("0 canciones", this);
    m_viewSummaryLabel->setStyleSheet("color: #94A3B8; font-size: 12px;");
    titleLayout->addWidget(m_viewSummaryLabel);
    songsHeaderRow->addLayout(titleLayout);

    songsHeaderRow->addStretch();

    // Barra de búsqueda rápida
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText("Buscar canción, artista o álbum...");
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->setFixedWidth(260);
    m_searchEdit->setStyleSheet(
        "QLineEdit { background-color: #0F172A; color: #F8FAFC; border: 1px solid #334155; "
        "border-radius: 6px; padding: 6px 12px; font-size: 12px; } "
        "QLineEdit:focus { border: 1px solid #38BDF8; }"
    );
    connect(m_searchEdit, &QLineEdit::textChanged, this, &MainWindow::onFilterTracks);
    songsHeaderRow->addWidget(m_searchEdit);

    songsLayout->addLayout(songsHeaderRow);

    // Barra de herramientas de canciones (Seleccionar todo / Eliminar)
    auto* toolsRow = new QHBoxLayout();
    toolsRow->setSpacing(10);

    m_selectAllBtn = new QPushButton("Seleccionar todas", this);
    m_selectAllBtn->setStyleSheet(
        "QPushButton { background-color: #334155; color: #F8FAFC; border-radius: 6px; padding: 6px 12px; font-size: 12px; font-weight: 500; } "
        "QPushButton:hover { background-color: #475569; }"
    );
    connect(m_selectAllBtn, &QPushButton::clicked, this, &MainWindow::onToggleSelectAll);
    toolsRow->addWidget(m_selectAllBtn);

    m_deleteBtn = new QPushButton("Eliminar seleccionadas", this);
    m_deleteBtn->setEnabled(false);
    m_deleteBtn->setStyleSheet(
        "QPushButton { background-color: #DC2626; color: white; border-radius: 6px; padding: 6px 14px; font-size: 12px; font-weight: bold; } "
        "QPushButton:hover { background-color: #B91C1C; } "
        "QPushButton:disabled { background-color: #475569; color: #94A3B8; }"
    );
    connect(m_deleteBtn, &QPushButton::clicked, this, &MainWindow::onDeleteSelectedTracks);
    toolsRow->addWidget(m_deleteBtn);

    toolsRow->addStretch();
    songsLayout->addLayout(toolsRow);

    // Tabla de Canciones
    m_trackTable = new QTableView(this);
    m_trackModel = new TrackTableModel(this);
    m_trackTable->setModel(m_trackModel);
    m_trackTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_trackTable->setAlternatingRowColors(true);
    m_trackTable->verticalHeader()->setVisible(false);
    m_trackTable->setShowGrid(false);
    m_trackTable->setStyleSheet(
        "QTableView { background-color: #0F172A; alternate-background-color: #142036; "
        "color: #F8FAFC; border: 1px solid #334155; border-radius: 6px; font-size: 13px; } "
        "QTableView::item { padding: 6px; border: none; } "
        "QTableView::item:selected { background-color: #2563EB; color: white; } "
        "QHeaderView::section { background-color: #1E293B; color: #94A3B8; font-weight: 600; "
        "font-size: 12px; padding: 6px; border: none; border-bottom: 1px solid #334155; }"
    );

    // Ajustar columnas
    m_trackTable->horizontalHeader()->setSectionResizeMode(TrackTableModel::ColCheck, QHeaderView::Fixed);
    m_trackTable->setColumnWidth(TrackTableModel::ColCheck, 34);
    m_trackTable->horizontalHeader()->setSectionResizeMode(TrackTableModel::ColTitle, QHeaderView::Stretch);
    m_trackTable->horizontalHeader()->setSectionResizeMode(TrackTableModel::ColArtist, QHeaderView::Interactive);
    m_trackTable->setColumnWidth(TrackTableModel::ColArtist, 170);
    m_trackTable->horizontalHeader()->setSectionResizeMode(TrackTableModel::ColAlbum, QHeaderView::Interactive);
    m_trackTable->setColumnWidth(TrackTableModel::ColAlbum, 170);
    m_trackTable->horizontalHeader()->setSectionResizeMode(TrackTableModel::ColQuality, QHeaderView::Fixed);
    m_trackTable->setColumnWidth(TrackTableModel::ColQuality, 90);
    m_trackTable->horizontalHeader()->setSectionResizeMode(TrackTableModel::ColDuration, QHeaderView::Fixed);
    m_trackTable->setColumnWidth(TrackTableModel::ColDuration, 75);
    m_trackTable->horizontalHeader()->setSectionResizeMode(TrackTableModel::ColFormat, QHeaderView::Fixed);
    m_trackTable->setColumnWidth(TrackTableModel::ColFormat, 75);
    m_trackTable->horizontalHeader()->setSectionResizeMode(TrackTableModel::ColSize, QHeaderView::Fixed);
    m_trackTable->setColumnWidth(TrackTableModel::ColSize, 85);

    connect(m_trackModel, &TrackTableModel::selectionChanged, this, &MainWindow::onSelectionChanged);

    songsLayout->addWidget(m_trackTable, 1);
    splitter->addWidget(songsWidget);
    splitter->setStretchFactor(1, 1);

    rootLayout->addWidget(splitter, 1);

    // 3. Panel Inferior: Resumen de Hardware, Batería y Barra de Almacenamiento estilo iTunes
    auto* bottomCard = new QFrame(this);
    bottomCard->setStyleSheet("background-color: #1E293B; border-radius: 10px; padding: 8px 14px;");
    auto* bottomLayout = new QVBoxLayout(bottomCard);
    bottomLayout->setContentsMargins(12, 6, 12, 6);
    bottomLayout->setSpacing(6);

    auto* bottomInfoRow = new QHBoxLayout();
    bottomInfoRow->setSpacing(14);

    m_deviceIconLabel = new QLabel("📱", this);
    m_deviceIconLabel->setStyleSheet("font-size: 24px;");
    bottomInfoRow->addWidget(m_deviceIconLabel);

    m_modelLabel = new QLabel("Sin dispositivo", this);
    m_modelLabel->setStyleSheet("font-size: 14px; font-weight: bold; color: #F8FAFC;");
    bottomInfoRow->addWidget(m_modelLabel);

    bottomInfoRow->addSpacing(10);

    m_serialLabel = new QLabel("", this);
    m_serialLabel->setStyleSheet("color: #64748B; font-size: 12px;");
    bottomInfoRow->addWidget(m_serialLabel);

    bottomInfoRow->addStretch();

    m_batteryIconLabel = new QLabel("🔋", this);
    bottomInfoRow->addWidget(m_batteryIconLabel);
    m_batteryLabel = new QLabel("100% (Cargando)", this);
    m_batteryLabel->setStyleSheet("color: #10B981; font-weight: bold; font-size: 12px;");
    bottomInfoRow->addWidget(m_batteryLabel);

    bottomInfoRow->addSpacing(16);

    m_freeCapacityLabel = new QLabel("Libre: —", this);
    m_freeCapacityLabel->setStyleSheet("color: #94A3B8; font-size: 12px;");
    bottomInfoRow->addWidget(m_freeCapacityLabel);

    m_totalCapacityLabel = new QLabel("Total: —", this);
    m_totalCapacityLabel->setStyleSheet("color: #E2E8F0; font-size: 12px; font-weight: bold;");
    bottomInfoRow->addWidget(m_totalCapacityLabel);

    bottomLayout->addLayout(bottomInfoRow);

    m_capacityBar = new CapacityBar(this);
    m_capacityBar->setFixedHeight(22);
    bottomLayout->addWidget(m_capacityBar);

    rootLayout->addWidget(bottomCard);

    statusBar()->showMessage("Listo");
    statusBar()->setStyleSheet("background-color: #0F172A; color: #64748B; font-size: 12px;");
}

void MainWindow::onRefreshDevices() {
    statusBar()->showMessage("Buscando dispositivos iPod...");
    m_detector.refresh();
}

void MainWindow::onBrowseCustomMount() {
    QString dir = QFileDialog::getExistingDirectory(
        this,
        "Seleccionar punto de montaje del iPod",
        QDir::homePath(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks
    );

    if (!dir.isEmpty()) {
        m_deviceCombo->addItem(QString("Manual: %1").arg(dir), dir);
        m_deviceCombo->setCurrentIndex(m_deviceCombo->count() - 1);
    }
}

void MainWindow::onDeviceSelected(int index) {
    if (index < 0 || m_deviceCombo->count() == 0) return;

    QString mount = m_deviceCombo->itemData(index).toString();
    if (mount.isEmpty()) return;

    statusBar()->showMessage(QString("Cargando iPod en %1...").arg(mount));
    m_currentDevice = std::make_unique<IPodDevice>(mount);
    updateDeviceView();
    populateTreeOrganizer();
    statusBar()->showMessage("iPod cargado con éxito.");
}

void MainWindow::populateTreeOrganizer() {
    m_treeOrganizer->clear();
    m_currentPlaylistIndex = -1;

    if (!m_currentDevice || !m_currentDevice->isValid()) return;

    // Raíz: Dispositivo
    auto* rootItem = new QTreeWidgetItem(m_treeOrganizer);
    rootItem->setText(0, QString("📱 %1").arg(m_currentDevice->modelName()));
    QFont rf = rootItem->font(0);
    rf.setBold(true);
    rootItem->setFont(0, rf);

    // Rama 1: En mi dispositivo
    auto* devBranch = new QTreeWidgetItem(rootItem);
    devBranch->setText(0, "📂 En mi dispositivo");

    const auto pls = m_currentDevice->playlists();
    int masterIdx = -1;
    for (int i = 0; i < pls.size(); ++i) {
        if (pls[i].isMaster) {
            masterIdx = i;
            break;
        }
    }

    int songCount = (masterIdx >= 0) ? pls[masterIdx].trackCount() : 0;
    auto* musicItem = new QTreeWidgetItem(devBranch);
    musicItem->setText(0, QString("🎵 Música (%1)").arg(songCount));
    musicItem->setData(0, Qt::UserRole, masterIdx >= 0 ? masterIdx : 0);

    auto* moviesItem = new QTreeWidgetItem(devBranch);
    moviesItem->setText(0, "🎬 Películas (0)");
    moviesItem->setData(0, Qt::UserRole, -2);

    auto* tvItem = new QTreeWidgetItem(devBranch);
    tvItem->setText(0, "📺 Programas de TV (0)");
    tvItem->setData(0, Qt::UserRole, -2);

    auto* booksItem = new QTreeWidgetItem(devBranch);
    booksItem->setText(0, "📖 Audiolibros (0)");
    booksItem->setData(0, Qt::UserRole, -2);

    auto* podItem = new QTreeWidgetItem(devBranch);
    podItem->setText(0, "🎙️ Podcasts (0)");
    podItem->setData(0, Qt::UserRole, -2);

    // Rama 2: Listas de Reproducción
    QList<int> userPlIndices;
    for (int i = 0; i < pls.size(); ++i) {
        if (!pls[i].isMaster) userPlIndices.append(i);
    }

    auto* plBranch = new QTreeWidgetItem(rootItem);
    plBranch->setText(0, QString("📑 Listas de reproducción (%1)").arg(userPlIndices.size()));

    QTreeWidgetItem* firstUserItem = nullptr;
    for (int idx : userPlIndices) {
        const auto& pl = pls[idx];
        auto* plItem = new QTreeWidgetItem(plBranch);
        plItem->setText(0, QString("🎶 %1 (%2)").arg(pl.name).arg(pl.trackCount()));
        plItem->setData(0, Qt::UserRole, idx);
        if (!firstUserItem) firstUserItem = plItem;
    }

    rootItem->setExpanded(true);
    devBranch->setExpanded(true);
    plBranch->setExpanded(true);

    m_treeOrganizer->setCurrentItem(musicItem);
    onTreeItemClicked(musicItem, 0);
}

void MainWindow::onTreeItemClicked(QTreeWidgetItem* item, int /*column*/) {
    if (!item || !m_currentDevice) return;

    QVariant data = item->data(0, Qt::UserRole);
    if (!data.isValid()) return;

    int idx = data.toInt();
    if (idx == -2) {
        m_viewTitleLabel->setText(item->text(0).section('(', 0, 0).trimmed());
        m_viewSummaryLabel->setText("0 elementos");
        m_activeTracks.clear();
        m_trackModel->setTracks({});
        m_currentPlaylistIndex = -1;
        return;
    }

    const auto pls = m_currentDevice->playlists();
    if (idx < 0 || idx >= pls.size()) return;

    m_currentPlaylistIndex = idx;
    const auto& pl = pls[idx];
    m_viewTitleLabel->setText(pl.name);
    m_viewSummaryLabel->setText(pl.formattedSummary());

    m_allSelected = false;
    m_selectAllBtn->setText("Seleccionar todas");

    m_activeTracks = pl.tracks;
    QString filter = m_searchEdit->text().trimmed();
    if (filter.isEmpty()) {
        m_trackModel->setTracks(m_activeTracks);
    } else {
        onFilterTracks(filter);
    }
}

void MainWindow::onToggleSelectAll() {
    m_allSelected = !m_allSelected;
    m_trackModel->selectAll(m_allSelected);
    m_selectAllBtn->setText(m_allSelected ? "Deseleccionar todas" : "Seleccionar todas");
}

void MainWindow::onFilterTracks(const QString& query) {
    QString q = query.trimmed().toLower();
    if (q.isEmpty()) {
        m_trackModel->setTracks(m_activeTracks);
    } else {
        QList<Track> filtered;
        for (const auto& t : m_activeTracks) {
            if (t.title.toLower().contains(q) ||
                t.artist.toLower().contains(q) ||
                t.album.toLower().contains(q)) {
                filtered.append(t);
            }
        }
        m_trackModel->setTracks(filtered);
    }
}

void MainWindow::onSelectionChanged(int selectedCount) {
    if (selectedCount > 0) {
        m_deleteBtn->setEnabled(true);
        m_deleteBtn->setText(QString("Eliminar seleccionadas (%1)").arg(selectedCount));
    } else {
        m_deleteBtn->setEnabled(false);
        m_deleteBtn->setText("Eliminar seleccionadas");
        m_allSelected = false;
        m_selectAllBtn->setText("Seleccionar todas");
    }
}

void MainWindow::onDeleteSelectedTracks() {
    if (!m_currentDevice || m_currentPlaylistIndex < 0) return;

    QList<uint32_t> ids = m_trackModel->selectedTrackIds();
    if (ids.isEmpty()) return;

    // Detectar si iTunes está abierto para evitar conflictos en RAM o sobrescrituras
    if (isITunesRunning()) {
        auto itunesReply = QMessageBox::warning(
            this,
            "Conflicto con iTunes detectado",
            "⚠️ iTunes está abierto en segundo plano.\n\n"
            "iTunes mantiene la base de datos del iPod en su memoria RAM y NO detecta cambios externos en tiempo real.\n"
            "Si no se cierra, iTunes continuará mostrando las canciones y podría sobrescribir la base de datos del iPod al cerrarse.\n\n"
            "¿Deseas que iPop cierre iTunes automáticamente ahora para que los cambios se sincronicen y refresquen de inmediato?",
            QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel,
            QMessageBox::Yes
        );
        if (itunesReply == QMessageBox::Cancel) {
            return;
        }
        if (itunesReply == QMessageBox::Yes) {
            killITunesProcess();
            checkITunesStatus();
        }
    }

    const auto pls = m_currentDevice->playlists();
    const auto& pl = pls.at(m_currentPlaylistIndex);
    QString confirmText = pl.isMaster
        ? QString("¿Estás seguro de que deseas eliminar permanentemente %1 canciones del iPod? Los archivos se borrarán del disco y la base de datos se actualizará.").arg(ids.size())
        : QString("¿Deseas quitar %1 canciones de la lista \"%2\"?").arg(ids.size()).arg(pl.name);

    auto reply = QMessageBox::question(
        this,
        "Confirmar eliminación",
        confirmText,
        QMessageBox::Yes | QMessageBox::No
    );

    if (reply == QMessageBox::Yes) {
        int savedPlaylistIndex = m_currentPlaylistIndex;
        m_currentDevice->deleteTracks(ids, m_currentPlaylistIndex);
        updateDeviceView();
        populateTreeOrganizer();
        selectPlaylistIndex(savedPlaylistIndex);
        statusBar()->showMessage(QString("%1 canciones eliminadas permanentemente del iPod y base de datos actualizada.").arg(ids.size()));
    }
}

void MainWindow::updateDeviceView() {
    if (!m_currentDevice || !m_currentDevice->isValid()) {
        m_modelLabel->setText("Ningún iPod seleccionado");
        m_serialLabel->setText("");
        m_batteryLabel->setText("Desconectado");
        m_batteryLabel->setStyleSheet("color: #94A3B8; font-size: 12px;");
        m_batteryIconLabel->setText("🔌");

        m_capacityBar->setBreakdown(StorageBreakdown{});
        m_totalCapacityLabel->setText("Total: —");
        m_freeCapacityLabel->setText("Libre: —");
        m_ejectBtn->setEnabled(false);
        m_treeOrganizer->clear();
        m_trackModel->setTracks({});
        return;
    }

    m_ejectBtn->setEnabled(true);
    m_modelLabel->setText(m_currentDevice->modelName());
    m_serialLabel->setText(m_currentDevice->serialNumber().isEmpty() ? "" : QString("SN: %1").arg(m_currentDevice->serialNumber()));

    // Batería
    BatteryStatus batt = m_currentDevice->batteryStatus();
    if (batt.percentage >= 0) {
        m_batteryLabel->setText(QString("%1% %2").arg(batt.percentage).arg(batt.isCharging ? "(Cargando)" : ""));
        m_batteryIconLabel->setText(batt.isCharging ? "⚡" : "🔋");
        m_batteryLabel->setStyleSheet("color: #10B981; font-weight: bold; font-size: 12px;");
    } else {
        m_batteryLabel->setText(batt.stateString);
        m_batteryIconLabel->setText("🔋");
    }

    // Almacenamiento
    StorageBreakdown sb = m_currentDevice->storageBreakdown();
    m_capacityBar->setBreakdown(sb);

    m_totalCapacityLabel->setText(QString("Total: %1").arg(IPodDevice::formatBytes(sb.totalBytes)));
    m_freeCapacityLabel->setText(QString("Libre: %1").arg(IPodDevice::formatBytes(sb.freeBytes)));
}

void MainWindow::onEjectDevice() {
    if (!m_currentDevice || !m_currentDevice->isValid()) return;

    QString mount = m_currentDevice->mountPath();
    auto reply = QMessageBox::question(
        this,
        "Expulsar iPod",
        QString("¿Deseas expulsar con seguridad el iPod en \"%1\"?").arg(mount),
        QMessageBox::Yes | QMessageBox::No
    );

    if (reply == QMessageBox::Yes) {
        statusBar()->showMessage("Sincronizando y expulsando...");
#if defined(Q_OS_WIN)
        QMessageBox::information(this, "Listo", "Dispositivo listo para desconectar de forma segura.");
        onRefreshDevices();
#else
        QProcess::execute("sync");
        int exitCode = QProcess::execute("eject", {mount});
        if (exitCode != 0) {
            exitCode = QProcess::execute("umount", {mount});
        }

        if (exitCode == 0) {
            QMessageBox::information(this, "Listo", "El iPod ha sido expulsado con seguridad. Ya puedes desconectarlo.");
            onRefreshDevices();
        } else {
            QMessageBox::warning(this, "Atención", "No se pudo expulsar automáticamente el dispositivo. Intenta cerrarlo desde el explorador de archivos.");
        }
#endif
    }
}

void MainWindow::selectPlaylistIndex(int playlistIndex) {
    if (!m_currentDevice) return;
    const auto pls = m_currentDevice->playlists();
    if (playlistIndex < 0 || playlistIndex >= pls.size()) return;

    QTreeWidgetItemIterator it(m_treeOrganizer);
    while (*it) {
        if ((*it)->data(0, Qt::UserRole).toInt() == playlistIndex) {
            m_treeOrganizer->setCurrentItem(*it);
            onTreeItemClicked(*it, 0);
            return;
        }
        ++it;
    }
}

void MainWindow::checkITunesStatus() {
    if (m_itunesWarningFrame) {
        m_itunesWarningFrame->setVisible(isITunesRunning());
    }
}

bool MainWindow::isITunesRunning() {
#ifdef Q_OS_WIN
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    bool found = false;
    if (Process32FirstW(hSnap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, L"iTunes.exe") == 0) {
                found = true;
                break;
            }
        } while (Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap);
    return found;
#else
    return false;
#endif
}

bool MainWindow::killITunesProcess() {
#ifdef Q_OS_WIN
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    bool killed = false;
    if (Process32FirstW(hSnap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, L"iTunes.exe") == 0) {
                HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                if (hProc) {
                    TerminateProcess(hProc, 0);
                    CloseHandle(hProc);
                    killed = true;
                }
            }
        } while (Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap);
    return killed;
#else
    return false;
#endif
}

