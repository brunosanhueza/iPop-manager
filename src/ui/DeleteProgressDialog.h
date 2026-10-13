#pragma once

#include <QDialog>
#include <QLabel>
#include <QProgressBar>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <cstdint>

class DeleteProgressDialog : public QDialog {
    Q_OBJECT

public:
    explicit DeleteProgressDialog(QWidget* parent = nullptr);

    void setTitleText(const QString& title, const QString& icon = "📥");
    void setTotalBytes(uint64_t totalBytes);
    void updateProgress(uint64_t currentBytes, const QString& statusText);
    void setComplete(const QString& finalMessage = QString());

private:
    uint64_t m_totalBytes{0};
    QLabel* m_iconLabel{nullptr};
    QLabel* m_titleLabel{nullptr};
    QLabel* m_statusLabel{nullptr};
    QProgressBar* m_progressBar{nullptr};
    QLabel* m_mbLabel{nullptr};
    QLabel* m_rangeBadgeLabel{nullptr};
};
