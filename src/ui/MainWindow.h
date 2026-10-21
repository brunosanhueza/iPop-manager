#pragma once

#include <QMainWindow>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QTableView>
#include <QLineEdit>
#include <QSplitter>
#include <memory>

#include "../core/IPodDevice.h"
#include "../core/DeviceDetector.h"
#include "CapacityBar.h"
#include "TrackTableModel.h"
#include "ITunesPlayerWidget.h"

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override = default;

private slots:
    void onRefreshDevices();
    void onBrowseCustomMount();
    void onDeviceSelected(int index);
    void onEjectDevice();
    void onAddTracks();
    void onEditSelectedTracks();
    void onEditArtwork();
    void onTreeItemClicked(QTreeWidgetItem* item, int column);
    void onTreeItemChanged(QTreeWidgetItem* item, int column);
    void onTreeContextMenu(const QPoint& pos);
    void onToggleSelectAll();
    void onDeleteSelectedTracks();
    void onFilterTracks(const QString& query);
    void onSelectionChanged(int selectedCount);
    void onTrackDoubleClicked(const QModelIndex& index);
    void onPlayerNextTrack();
    void onPlayerPrevTrack();
    void onToggleSelectAllPlaylists();
    void onEditSelectedPlaylist();
    void onDeleteSelectedPlaylists();
    void onNewPlaylist();
    void onUpdateBatteryTelemetry();
    void onBatteryDiagnosticsClicked();

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void setupUi();
    void updateDeviceView();
    void populateTreeOrganizer();
    void selectPlaylistIndex(int playlistIndex);
    void checkITunesStatus();
    static bool isITunesRunning();
    static bool killITunesProcess();
    void handleDroppedFiles(const QStringList& filePaths);
    QList<int> getCheckedPlaylistIndices() const;
    void updatePlaylistActionButtons();
    void renamePlaylistByIndex(int idx);
    void deletePlaylistsByIndices(const QList<int>& indices);

    // Core
    DeviceDetector m_detector;
    std::unique_ptr<IPodDevice> m_currentDevice;
    int m_currentPlaylistIndex{-1};
    bool m_allSelected{false};
    bool m_allPlaylistsChecked{false};
    QList<Track> m_activeTracks;
    QTimer* m_batteryTimer{nullptr};

    // UI Widgets - Top Bar
    QComboBox* m_deviceCombo{nullptr};
    QPushButton* m_refreshBtn{nullptr};
    QPushButton* m_browseBtn{nullptr};
    QPushButton* m_ejectBtn{nullptr};
    QPushButton* m_addTracksBtn{nullptr};
    QFrame* m_itunesWarningFrame{nullptr};
    ITunesPlayerWidget* m_playerWidget{nullptr};

    // UI Widgets - Árbol Organizador (Sidebar Izquierda)
    QTreeWidget* m_treeOrganizer{nullptr};
    QPushButton* m_newPlBtn{nullptr};
    QPushButton* m_selectAllPlBtn{nullptr};
    QPushButton* m_editPlBtn{nullptr};
    QPushButton* m_deletePlBtn{nullptr};

    // UI Widgets - Canciones (Panel Derecho)
    QLabel* m_viewTitleLabel{nullptr};
    QLabel* m_viewSummaryLabel{nullptr};
    QLineEdit* m_searchEdit{nullptr};
    QPushButton* m_selectAllBtn{nullptr};
    QPushButton* m_deleteBtn{nullptr};
    QPushButton* m_editTagsBtn{nullptr};
    QPushButton* m_artBtn{nullptr};
    QTableView* m_trackTable{nullptr};
    TrackTableModel* m_trackModel{nullptr};

    // UI Widgets - Device & Storage (Panel Inferior)
    QLabel* m_deviceIconLabel{nullptr};
    QLabel* m_modelLabel{nullptr};
    QLabel* m_serialLabel{nullptr};
    QLabel* m_batteryLabel{nullptr};
    QLabel* m_batteryIconLabel{nullptr};
    CapacityBar* m_capacityBar{nullptr};
    QLabel* m_totalCapacityLabel{nullptr};
    QLabel* m_freeCapacityLabel{nullptr};
};

