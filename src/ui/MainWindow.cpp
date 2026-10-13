#include "MainWindow.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QFileDialog>
#include <QMessageBox>
#include <QInputDialog>
#include <QMenu>
#include <QProcess>
#include <QStatusBar>
#include <QFrame>
#include <QHeaderView>
#include <QTimer>
#include <QThread>
#include <QApplication>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QDirIterator>
#include "DeleteProgressDialog.h"
#include "../core/AudioConverter.h"
#include "../core/TrackImporter.h"
#include "../core/ITunesDBReader.h"

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
    setAcceptDrops(true);

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

    // 1.5 Reproductor de Medios Integrado (Estilo iTunes)
    m_playerWidget = new ITunesPlayerWidget(this);
    connect(m_playerWidget, &ITunesPlayerWidget::requestNextTrack, this, &MainWindow::onPlayerNextTrack);
    connect(m_playerWidget, &ITunesPlayerWidget::requestPreviousTrack, this, &MainWindow::onPlayerPrevTrack);
    rootLayout->addWidget(m_playerWidget);

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

    // Barra de acciones para listas de reproducción
    auto* plToolsGrid = new QGridLayout();
    plToolsGrid->setSpacing(6);
    plToolsGrid->setContentsMargins(2, 0, 2, 4);

    m_newPlBtn = new QPushButton("➕ Nueva", this);
    m_newPlBtn->setToolTip("Crear una nueva lista de reproducción en el iPod");
    m_newPlBtn->setStyleSheet(
        "QPushButton { background-color: #334155; color: #F8FAFC; border-radius: 6px; padding: 5px 8px; font-size: 11px; font-weight: 500; } "
        "QPushButton:hover { background-color: #475569; }"
    );
    connect(m_newPlBtn, &QPushButton::clicked, this, &MainWindow::onNewPlaylist);
    plToolsGrid->addWidget(m_newPlBtn, 0, 0);

    m_selectAllPlBtn = new QPushButton("☑️ Todas", this);
    m_selectAllPlBtn->setToolTip("Seleccionar o deseleccionar todas las listas de reproducción");
    m_selectAllPlBtn->setStyleSheet(
        "QPushButton { background-color: #334155; color: #F8FAFC; border-radius: 6px; padding: 5px 8px; font-size: 11px; font-weight: 500; } "
        "QPushButton:hover { background-color: #475569; }"
    );
    connect(m_selectAllPlBtn, &QPushButton::clicked, this, &MainWindow::onToggleSelectAllPlaylists);
    plToolsGrid->addWidget(m_selectAllPlBtn, 0, 1);

    m_editPlBtn = new QPushButton("✏️ Editar", this);
    m_editPlBtn->setToolTip("Editar o renombrar la lista (habilitado si seleccionas exactamente una lista)");
    m_editPlBtn->setEnabled(false);
    m_editPlBtn->setStyleSheet(
        "QPushButton { background-color: #1E293B; color: #475569; border: 1px solid #334155; border-radius: 6px; padding: 5px 8px; font-size: 11px; font-weight: 500; } "
        "QPushButton:hover { background-color: #475569; color: white; } "
        "QPushButton:disabled { background-color: #1E293B; color: #475569; border: 1px solid #334155; }"
    );
    connect(m_editPlBtn, &QPushButton::clicked, this, &MainWindow::onEditSelectedPlaylist);
    plToolsGrid->addWidget(m_editPlBtn, 1, 0);

    m_deletePlBtn = new QPushButton("🗑️ Eliminar", this);
    m_deletePlBtn->setToolTip("Eliminar las listas de reproducción seleccionadas");
    m_deletePlBtn->setEnabled(false);
    m_deletePlBtn->setStyleSheet(
        "QPushButton { background-color: #1E293B; color: #475569; border: 1px solid #334155; border-radius: 6px; padding: 5px 8px; font-size: 11px; font-weight: 500; } "
        "QPushButton:hover { background-color: #B91C1C; color: white; } "
        "QPushButton:disabled { background-color: #1E293B; color: #475569; border: 1px solid #334155; }"
    );
    connect(m_deletePlBtn, &QPushButton::clicked, this, &MainWindow::onDeleteSelectedPlaylists);
    plToolsGrid->addWidget(m_deletePlBtn, 1, 1);

    treeLayout->addLayout(plToolsGrid);

    m_treeOrganizer = new QTreeWidget(this);
    m_treeOrganizer->setHeaderHidden(true);
    m_treeOrganizer->setAnimated(true);
    m_treeOrganizer->setContextMenuPolicy(Qt::CustomContextMenu);
    m_treeOrganizer->setStyleSheet(
        "QTreeWidget { background-color: transparent; border: none; color: #E2E8F0; font-size: 13px; } "
        "QTreeWidget::item { padding: 6px 8px; border-radius: 6px; margin: 1px 0; } "
        "QTreeWidget::item:hover { background-color: #334155; } "
        "QTreeWidget::item:selected { background-color: #2563EB; color: white; font-weight: 500; } "
        "QTreeWidget::indicator { width: 14px; height: 14px; border-radius: 3px; border: 1px solid #475569; background-color: #0F172A; } "
        "QTreeWidget::indicator:hover { border: 1px solid #38BDF8; } "
        "QTreeWidget::indicator:checked { background-color: #2563EB; border: 1px solid #38BDF8; }"
    );
    connect(m_treeOrganizer, &QTreeWidget::itemClicked, this, &MainWindow::onTreeItemClicked);
    connect(m_treeOrganizer, &QTreeWidget::itemChanged, this, &MainWindow::onTreeItemChanged);
    connect(m_treeOrganizer, &QTreeWidget::customContextMenuRequested, this, &MainWindow::onTreeContextMenu);
    connect(m_treeOrganizer, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem*, QTreeWidgetItem*) {
        updatePlaylistActionButtons();
    });
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
    connect(m_trackTable, &QTableView::doubleClicked, this, &MainWindow::onTrackDoubleClicked);

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
    m_treeOrganizer->blockSignals(true);
    m_treeOrganizer->clear();
    m_currentPlaylistIndex = -1;

    if (!m_currentDevice || !m_currentDevice->isValid()) {
        m_treeOrganizer->blockSignals(false);
        updatePlaylistActionButtons();
        return;
    }

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
        plItem->setFlags(plItem->flags() | Qt::ItemIsUserCheckable);
        plItem->setCheckState(0, Qt::Unchecked);
        if (!firstUserItem) firstUserItem = plItem;
    }

    rootItem->setExpanded(true);
    devBranch->setExpanded(true);
    plBranch->setExpanded(true);
    m_treeOrganizer->blockSignals(false);

    m_treeOrganizer->setCurrentItem(musicItem);
    onTreeItemClicked(musicItem, 0);
    updatePlaylistActionButtons();
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
        updatePlaylistActionButtons();
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
    updatePlaylistActionButtons();
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

        // Mostrar barra de progreso visual de 0.0 MB a X.X MB
        uint64_t totalBytesToDelete = m_currentDevice->calculateTracksSizeBytes(ids);
        DeleteProgressDialog progressDlg(this);
        progressDlg.setTotalBytes(totalBytesToDelete);
        progressDlg.show();

        auto progressCallback = [&progressDlg](uint64_t currentBytes, uint64_t /*totalBytes*/, const QString& statusText) {
            progressDlg.updateProgress(currentBytes, statusText);
            QThread::msleep(35); // Breve pausa visual para que el usuario aprecie el progreso suave
        };

        m_currentDevice->deleteTracks(ids, m_currentPlaylistIndex, progressCallback);

        progressDlg.setComplete("Canciones eliminadas físicamente y base de datos actualizada.");
        progressDlg.close();

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
        QString("¿Deseas expulsar con seguridad el iPod en \"%1\"?\n\n"
                "La base de datos se sincronizará y el iPod cambiará su pantalla a 'Aceptable desconectar' (OK to disconnect) o al menú principal.").arg(mount),
        QMessageBox::Yes | QMessageBox::No
    );

    if (reply == QMessageBox::Yes) {
        statusBar()->showMessage("Sincronizando y expulsando dispositivo...");
        QApplication::processEvents();

        QString errorMsg;
        bool ok = m_currentDevice->eject(&errorMsg);

        if (ok) {
            QMessageBox::information(
                this,
                "iPod Expulsado",
                "El iPod ha sido expulsado con seguridad.\n\n"
                "En la pantalla del iPod aparecerá 'Aceptable desconectar' (OK to disconnect) o el menú principal de música. Ya puedes retirar el cable con total seguridad."
            );
            onRefreshDevices();
        } else {
            QMessageBox::warning(
                this,
                "Error al expulsar",
                QString("No se pudo expulsar automáticamente el iPod:\n%1\n\n"
                        "Asegúrate de que iTunes no esté en ejecución ni haya exploradores de archivos abiertos en la unidad.").arg(errorMsg)
            );
        }
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

void MainWindow::onTrackDoubleClicked(const QModelIndex& index) {
    if (!index.isValid() || !m_currentDevice || !m_currentDevice->isValid()) return;
    int row = index.row();
    if (row < 0 || row >= m_activeTracks.size()) return;

    const Track& track = m_activeTracks.at(row);
    QString physPath = ITunesDBReader::resolvePhysicalPath(m_currentDevice->mountPath(), track.filePath);
    if (m_playerWidget) {
        m_playerWidget->loadAndPlayTrack(track, physPath);
    }
}

void MainWindow::onPlayerNextTrack() {
    if (m_activeTracks.isEmpty() || !m_currentDevice || !m_currentDevice->isValid() || !m_playerWidget) return;

    uint32_t curId = m_playerWidget->currentTrackId();
    int curIdx = -1;
    for (int i = 0; i < m_activeTracks.size(); ++i) {
        if (m_activeTracks[i].id == curId) {
            curIdx = i;
            break;
        }
    }

    int nextIdx = (curIdx + 1) % m_activeTracks.size();
    const Track& track = m_activeTracks.at(nextIdx);
    QString physPath = ITunesDBReader::resolvePhysicalPath(m_currentDevice->mountPath(), track.filePath);
    m_playerWidget->loadAndPlayTrack(track, physPath);
    m_trackTable->selectRow(nextIdx);
}

void MainWindow::onPlayerPrevTrack() {
    if (m_activeTracks.isEmpty() || !m_currentDevice || !m_currentDevice->isValid() || !m_playerWidget) return;

    uint32_t curId = m_playerWidget->currentTrackId();
    int curIdx = -1;
    for (int i = 0; i < m_activeTracks.size(); ++i) {
        if (m_activeTracks[i].id == curId) {
            curIdx = i;
            break;
        }
    }

    int prevIdx = (curIdx - 1 + m_activeTracks.size()) % m_activeTracks.size();
    const Track& track = m_activeTracks.at(prevIdx);
    QString physPath = ITunesDBReader::resolvePhysicalPath(m_currentDevice->mountPath(), track.filePath);
    m_playerWidget->loadAndPlayTrack(track, physPath);
    m_trackTable->selectRow(prevIdx);
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void MainWindow::dragMoveEvent(QDragMoveEvent* event) {
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent* event) {
    const QList<QUrl> urls = event->mimeData()->urls();
    QStringList paths;
    for (const auto& url : urls) {
        if (url.isLocalFile()) {
            paths.append(url.toLocalFile());
        }
    }
    if (!paths.isEmpty()) {
        handleDroppedFiles(paths);
    }
}

void MainWindow::handleDroppedFiles(const QStringList& filePaths) {
    if (!m_currentDevice || !m_currentDevice->isValid()) {
        QMessageBox::warning(this, "Sin iPod", "Conecta o selecciona un iPod antes de agregar canciones.");
        return;
    }

    checkITunesStatus();
    if (isITunesRunning()) {
        auto ans = QMessageBox::warning(
            this,
            "iTunes en Ejecución",
            "iTunes está abierto en segundo plano. Para evitar que sobrescriba o bloquee la base de datos del iPod, se recomienda cerrarlo.\n\n"
            "¿Deseas cerrar iTunes automáticamente ahora para continuar?",
            QMessageBox::Yes | QMessageBox::No
        );
        if (ans == QMessageBox::Yes) {
            killITunesProcess();
            checkITunesStatus();
        } else {
            return;
        }
    }

    const QSet<QString> supportedExts = {
        "mp3", "m4a", "aac", "m4b", "wav", "aif", "aiff",
        "flac", "ogg", "oga", "opus", "wma", "ape", "wv", "webm", "mka"
    };

    QStringList audioFiles;
    for (const auto& p : filePaths) {
        QFileInfo fi(p);
        if (fi.isDir()) {
            QDirIterator it(p, QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                QString subP = it.next();
                QString ext = QFileInfo(subP).suffix().toLower();
                if (supportedExts.contains(ext)) {
                    audioFiles.append(subP);
                }
            }
        } else if (fi.isFile()) {
            QString ext = fi.suffix().toLower();
            if (supportedExts.contains(ext)) {
                audioFiles.append(p);
            }
        }
    }

    if (audioFiles.isEmpty()) {
        QMessageBox::information(this, "Sin archivos de audio", "No se encontraron archivos de audio reconocibles en los elementos arrastrados.");
        return;
    }

    QString modelName = m_currentDevice->modelName();
    QStringList compatibleFiles;
    QStringList incompatibleFiles;
    QStringList incompatibleReasons;

    for (const auto& f : audioFiles) {
        auto check = AudioConverter::checkCompatibility(f, modelName);
        if (check.isSupported) {
            compatibleFiles.append(f);
        } else {
            incompatibleFiles.append(f);
            incompatibleReasons.append(QString("• %1: %2").arg(QFileInfo(f).fileName()).arg(check.reason));
        }
    }

    QStringList filesToImport = compatibleFiles;

    if (!incompatibleFiles.isEmpty()) {
        QString details = incompatibleReasons.mid(0, 6).join("\n");
        if (incompatibleReasons.size() > 6) {
            details += QString("\n... y %1 archivos adicionales.").arg(incompatibleReasons.size() - 6);
        }

        QString targetFmt = (modelName.contains("1G") || modelName.contains("2G")) ? "MP3 (320 kbps)" : "M4A (AAC 256 kbps de alta fidelidad)";

        auto reply = QMessageBox::question(
            this,
            "Compatibilidad de Códec con " + modelName,
            QString("Se detectaron %1 archivo(s) cuyo formato o códec no es reproducible por el hardware de tu %2:\n\n%3\n\n"
                    "¿Deseas convertirlos automáticamente a %4 manteniendo todas sus etiquetas y carátulas?")
                .arg(incompatibleFiles.size()).arg(modelName).arg(details).arg(targetFmt),
            QMessageBox::Yes | QMessageBox::No
        );

        if (reply == QMessageBox::Yes) {
            auto* progressDlg = new DeleteProgressDialog(this);
            progressDlg->setTitleText("Convirtiendo canciones incompatibles", "⚙️");
            uint64_t incompTotalBytes = 0;
            for (const auto& f : incompatibleFiles) incompTotalBytes += QFileInfo(f).size();
            progressDlg->setTotalBytes(incompTotalBytes);
            progressDlg->show();
            QApplication::processEvents();

            QString tempDir = QDir::temp().filePath("iPop_Converted");
            QDir().mkpath(tempDir);

            int convertedCount = 0;
            uint64_t incompAccumBytes = 0;
            for (const auto& src : incompatibleFiles) {
                QFileInfo fi(src);
                uint64_t fSize = fi.size();
                QString extOut = (modelName.contains("1G") || modelName.contains("2G")) ? "mp3" : "m4a";
                QString dest = QDir(tempDir).filePath(fi.completeBaseName() + "." + extOut);

                progressDlg->updateProgress(incompAccumBytes, QString("Convirtiendo (%1/%2): %3...")
                    .arg(convertedCount + 1).arg(incompatibleFiles.size()).arg(fi.fileName()));

                bool ok = AudioConverter::convertToCompatibleFormat(src, dest, [&](int pct, const QString& status) {
                    Q_UNUSED(status);
                    uint64_t curEstimate = incompAccumBytes + (fSize * pct) / 100;
                    progressDlg->updateProgress(curEstimate, QString("Convirtiendo (%1/%2): %3...")
                        .arg(convertedCount + 1).arg(incompatibleFiles.size()).arg(fi.fileName()));
                });

                incompAccumBytes += fSize;

                if (ok && QFile::exists(dest)) {
                    filesToImport.append(dest);
                    convertedCount++;
                }
            }
            progressDlg->close();
            progressDlg->deleteLater();
        }
    }

    if (filesToImport.isEmpty()) {
        return;
    }

    uint64_t totalBytes = 0;
    for (const auto& f : filesToImport) {
        totalBytes += QFileInfo(f).size();
    }

    auto* importDlg = new DeleteProgressDialog(this);
    importDlg->setTitleText("Agregando canciones al iPod", "📥");
    importDlg->setTotalBytes(totalBytes);
    importDlg->show();
    QApplication::processEvents();

    QString fwGuid = m_currentDevice->serialNumber();
    bool importOk = TrackImporter::importTracksToDevice(
        m_currentDevice->mountPath(),
        fwGuid,
        filesToImport,
        [&](int cur, int tot, uint64_t curBytes, uint64_t totBytes, const QString& status) {
            Q_UNUSED(cur);
            Q_UNUSED(tot);
            Q_UNUSED(totBytes);
            importDlg->updateProgress(curBytes, status);
        }
    );

    importDlg->setComplete("¡Canciones transferidas con éxito!");
    importDlg->close();
    importDlg->deleteLater();

    if (importOk) {
        m_currentDevice->loadPlaylists();
        m_currentDevice->refreshStorageBreakdown();
        updateDeviceView();
        populateTreeOrganizer();
        selectPlaylistIndex(0);

        QMessageBox::information(
            this,
            "Transferencia completada",
            QString("¡Se agregaron %1 canciones con éxito a tu %2!\n\n"
                    "La base de datos iTunesDB ha sido sincronizada y las canciones están listas para reproducirse.")
                .arg(filesToImport.size()).arg(m_currentDevice->modelName())
        );
    } else {
        QMessageBox::warning(
            this,
            "Error al transferir",
            "No se pudieron transferir los archivos o actualizar la base de datos iTunesDB en el iPod."
        );
    }
}

QList<int> MainWindow::getCheckedPlaylistIndices() const {
    QList<int> result;
    if (!m_treeOrganizer) return result;
    QTreeWidgetItemIterator it(m_treeOrganizer);
    while (*it) {
        QTreeWidgetItem* item = *it;
        if (item->checkState(0) == Qt::Checked) {
            int idx = item->data(0, Qt::UserRole).toInt();
            if (idx > 0) {
                result.append(idx);
            }
        }
        ++it;
    }
    return result;
}

void MainWindow::updatePlaylistActionButtons() {
    if (!m_treeOrganizer) return;

    QList<int> checked = getCheckedPlaylistIndices();
    int count = checked.size();

    QTreeWidgetItem* cur = m_treeOrganizer->currentItem();
    int currentIdx = (cur && cur->data(0, Qt::UserRole).isValid()) ? cur->data(0, Qt::UserRole).toInt() : -1;
    bool hasSingleFocusedUserPl = (count == 0 && currentIdx > 0);

    // Contar total de listas de usuario
    int totalUserPls = 0;
    QTreeWidgetItemIterator itCount(m_treeOrganizer);
    while (*itCount) {
        if ((*itCount)->data(0, Qt::UserRole).toInt() > 0) totalUserPls++;
        ++itCount;
    }

    // Botón "Todas" / "Deselec."
    if (m_selectAllPlBtn) {
        bool allChecked = (totalUserPls > 0 && count == totalUserPls);
        m_allPlaylistsChecked = allChecked;
        m_selectAllPlBtn->setText(allChecked ? "☑️ Deselec." : "☑️ Todas");
        m_selectAllPlBtn->setEnabled(totalUserPls > 0);
    }

    // Botón "Editar": habilitado ÚNICAMENTE si se selecciona una sola lista
    bool canEdit = (count == 1) || hasSingleFocusedUserPl;
    if (m_editPlBtn) {
        m_editPlBtn->setEnabled(canEdit);
        if (canEdit) {
            m_editPlBtn->setStyleSheet(
                "QPushButton { background-color: #2563EB; color: white; border-radius: 6px; padding: 5px 8px; font-size: 11px; font-weight: bold; } "
                "QPushButton:hover { background-color: #1D4ED8; }"
            );
        } else {
            m_editPlBtn->setStyleSheet(
                "QPushButton { background-color: #1E293B; color: #475569; border: 1px solid #334155; border-radius: 6px; padding: 5px 8px; font-size: 11px; font-weight: 500; } "
                "QPushButton:disabled { background-color: #1E293B; color: #475569; border: 1px solid #334155; }"
            );
        }
    }

    // Botón "Eliminar": habilitado si hay >= 1 lista seleccionada
    bool canDelete = (count >= 1) || hasSingleFocusedUserPl;
    if (m_deletePlBtn) {
        m_deletePlBtn->setEnabled(canDelete);
        if (canDelete) {
            if (count > 1) {
                m_deletePlBtn->setText(QString("🗑️ Eliminar (%1)").arg(count));
            } else {
                m_deletePlBtn->setText("🗑️ Eliminar");
            }
            m_deletePlBtn->setStyleSheet(
                "QPushButton { background-color: #DC2626; color: white; border-radius: 6px; padding: 5px 8px; font-size: 11px; font-weight: bold; } "
                "QPushButton:hover { background-color: #B91C1C; }"
            );
        } else {
            m_deletePlBtn->setText("🗑️ Eliminar");
            m_deletePlBtn->setStyleSheet(
                "QPushButton { background-color: #1E293B; color: #475569; border: 1px solid #334155; border-radius: 6px; padding: 5px 8px; font-size: 11px; font-weight: 500; } "
                "QPushButton:disabled { background-color: #1E293B; color: #475569; border: 1px solid #334155; }"
            );
        }
    }
}

void MainWindow::onTreeItemChanged(QTreeWidgetItem* item, int column) {
    Q_UNUSED(item);
    Q_UNUSED(column);
    updatePlaylistActionButtons();
}

void MainWindow::onTreeContextMenu(const QPoint& pos) {
    if (!m_currentDevice || !m_currentDevice->isValid()) return;

    QTreeWidgetItem* item = m_treeOrganizer->itemAt(pos);
    QMenu menu(this);
    menu.setStyleSheet(
        "QMenu { background-color: #1E293B; color: #F8FAFC; border: 1px solid #334155; padding: 4px; border-radius: 6px; font-size: 12px; } "
        "QMenu::item { padding: 6px 20px 6px 12px; border-radius: 4px; } "
        "QMenu::item:selected { background-color: #2563EB; color: white; } "
        "QMenu::item:disabled { color: #64748B; }"
    );

    menu.addAction("➕ Nueva lista de reproducción...", this, &MainWindow::onNewPlaylist);

    if (item && item->data(0, Qt::UserRole).toInt() > 0) {
        int idx = item->data(0, Qt::UserRole).toInt();
        menu.addSeparator();

        menu.addAction("✏️ Renombrar lista...", [this, idx]() {
            renamePlaylistByIndex(idx);
        });

        QList<int> checked = getCheckedPlaylistIndices();
        if (checked.size() > 1 && checked.contains(idx)) {
            menu.addAction(QString("🗑️ Eliminar %1 listas seleccionadas...").arg(checked.size()), [this, checked]() {
                deletePlaylistsByIndices(checked);
            });
        } else {
            menu.addAction("🗑️ Eliminar lista...", [this, idx]() {
                deletePlaylistsByIndices({idx});
            });
        }
    }

    menu.exec(m_treeOrganizer->viewport()->mapToGlobal(pos));
}

void MainWindow::onToggleSelectAllPlaylists() {
    if (!m_treeOrganizer) return;

    QList<int> checked = getCheckedPlaylistIndices();
    int totalUserPls = 0;
    QTreeWidgetItemIterator itCount(m_treeOrganizer);
    while (*itCount) {
        if ((*itCount)->data(0, Qt::UserRole).toInt() > 0) totalUserPls++;
        ++itCount;
    }

    bool selectAll = (checked.size() < totalUserPls);

    m_treeOrganizer->blockSignals(true);
    QTreeWidgetItemIterator it(m_treeOrganizer);
    while (*it) {
        QTreeWidgetItem* item = *it;
        if (item->data(0, Qt::UserRole).toInt() > 0) {
            item->setCheckState(0, selectAll ? Qt::Checked : Qt::Unchecked);
        }
        ++it;
    }
    m_treeOrganizer->blockSignals(false);

    updatePlaylistActionButtons();
}

void MainWindow::onEditSelectedPlaylist() {
    QList<int> checked = getCheckedPlaylistIndices();
    int targetIdx = -1;
    if (checked.size() == 1) {
        targetIdx = checked.first();
    } else if (checked.isEmpty()) {
        QTreeWidgetItem* cur = m_treeOrganizer->currentItem();
        if (cur && cur->data(0, Qt::UserRole).toInt() > 0) {
            targetIdx = cur->data(0, Qt::UserRole).toInt();
        }
    }

    if (targetIdx > 0) {
        renamePlaylistByIndex(targetIdx);
    }
}

void MainWindow::renamePlaylistByIndex(int idx) {
    if (!m_currentDevice || idx <= 0) return;
    const auto pls = m_currentDevice->playlists();
    if (idx >= pls.size()) return;

    QString currentName = pls[idx].name;
    bool ok = false;
    QString newName = QInputDialog::getText(
        this,
        "Renombrar lista de reproducción",
        QString("Nuevo nombre para '%1':").arg(currentName),
        QLineEdit::Normal,
        currentName,
        &ok
    );

    if (!ok) return;
    newName = newName.trimmed();
    if (newName.isEmpty() || newName == currentName) return;

    QString errorMsg;
    if (m_currentDevice->renamePlaylist(idx, newName, &errorMsg)) {
        populateTreeOrganizer();
        selectPlaylistIndex(idx);
        statusBar()->showMessage(QString("Lista renombrada a '%1'").arg(newName), 3000);
    } else {
        QMessageBox::warning(this, "Error", errorMsg.isEmpty() ? "No se pudo renombrar la lista." : errorMsg);
    }
}

void MainWindow::onDeleteSelectedPlaylists() {
    QList<int> checked = getCheckedPlaylistIndices();
    if (checked.isEmpty()) {
        QTreeWidgetItem* cur = m_treeOrganizer->currentItem();
        if (cur && cur->data(0, Qt::UserRole).toInt() > 0) {
            checked.append(cur->data(0, Qt::UserRole).toInt());
        }
    }

    if (!checked.isEmpty()) {
        deletePlaylistsByIndices(checked);
    }
}

void MainWindow::deletePlaylistsByIndices(const QList<int>& indices) {
    if (!m_currentDevice || indices.isEmpty()) return;

    QList<int> validIndices;
    for (int idx : indices) {
        if (idx > 0) validIndices.append(idx);
    }
    if (validIndices.isEmpty()) return;

    const auto pls = m_currentDevice->playlists();
    QString confirmText;
    if (validIndices.size() == 1) {
        int idx = validIndices.first();
        QString name = (idx < pls.size()) ? pls[idx].name : "la lista seleccionada";
        confirmText = QString("¿Estás seguro de que deseas eliminar la lista de reproducción \"%1\" del iPod?\n\n(Las canciones permanecerán intactas en el iPod).").arg(name);
    } else {
        confirmText = QString("¿Estás seguro de que deseas eliminar las %1 listas de reproducción seleccionadas del iPod?\n\n(Las canciones permanecerán intactas en el iPod).").arg(validIndices.size());
    }

    auto reply = QMessageBox::question(
        this,
        "Eliminar listas de reproducción",
        confirmText,
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No
    );

    if (reply != QMessageBox::Yes) return;

    QString errorMsg;
    if (m_currentDevice->deletePlaylists(validIndices, &errorMsg)) {
        populateTreeOrganizer();
        selectPlaylistIndex(0);
        statusBar()->showMessage(QString("%1 lista(s) de reproducción eliminada(s).").arg(validIndices.size()), 3000);
    } else {
        QMessageBox::critical(this, "Error", errorMsg.isEmpty() ? "No se pudieron eliminar las listas de reproducción." : errorMsg);
    }
}

void MainWindow::onNewPlaylist() {
    if (!m_currentDevice || !m_currentDevice->isValid()) {
        QMessageBox::information(this, "Aviso", "Conecta o selecciona un iPod primero.");
        return;
    }

    bool ok = false;
    QString name = QInputDialog::getText(
        this,
        "Nueva lista de reproducción",
        "Nombre de la nueva lista:",
        QLineEdit::Normal,
        "Nueva lista",
        &ok
    );

    if (!ok) return;
    name = name.trimmed();
    if (name.isEmpty()) return;

    QString errorMsg;
    if (m_currentDevice->createPlaylist(name, &errorMsg)) {
        populateTreeOrganizer();
        const auto pls = m_currentDevice->playlists();
        int newIdx = -1;
        for (int i = pls.size() - 1; i >= 1; --i) {
            if (pls[i].name == name) {
                newIdx = i;
                break;
            }
        }
        if (newIdx > 0) {
            selectPlaylistIndex(newIdx);
        }
        statusBar()->showMessage(QString("Lista \"%1\" creada con éxito.").arg(name), 3000);
    } else {
        QMessageBox::warning(this, "Error", errorMsg.isEmpty() ? "No se pudo crear la nueva lista." : errorMsg);
    }
}


