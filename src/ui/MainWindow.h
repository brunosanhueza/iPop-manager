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
    void onTreeItemClicked(QTreeWidgetItem* item, int column);
    void onToggleSelectAll();
    void onDeleteSelectedTracks();
    void onFilterTracks(const QString& query);
    void onSelectionChanged(int selectedCount);

private:
    void setupUi();
    void updateDeviceView();
    void populateTreeOrganizer();
    void selectPlaylistIndex(int playlistIndex);
    void checkITunesStatus();
    static bool isITunesRunning();
    static bool killITunesProcess();

    // Core
    DeviceDetector m_detector;
    std::unique_ptr<IPodDevice> m_currentDevice;
    int m_currentPlaylistIndex{-1};
    bool m_allSelected{false};
    QList<Track> m_activeTracks;

    // UI Widgets - Top Bar
    QComboBox* m_deviceCombo{nullptr};
    QPushButton* m_refreshBtn{nullptr};
    QPushButton* m_browseBtn{nullptr};
    QPushButton* m_ejectBtn{nullptr};
    QFrame* m_itunesWarningFrame{nullptr};

    // UI Widgets - Árbol Organizador (Sidebar Izquierda)
    QTreeWidget* m_treeOrganizer{nullptr};

    // UI Widgets - Canciones (Panel Derecho)
    QLabel* m_viewTitleLabel{nullptr};
    QLabel* m_viewSummaryLabel{nullptr};
    QLineEdit* m_searchEdit{nullptr};
    QPushButton* m_selectAllBtn{nullptr};
    QPushButton* m_deleteBtn{nullptr};
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
