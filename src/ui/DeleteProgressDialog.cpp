#include "DeleteProgressDialog.h"
#include <QApplication>
#include <QThread>

DeleteProgressDialog::DeleteProgressDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Eliminando canciones del iPod");
    setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint);
    setFixedSize(500, 200);
    setStyleSheet(
        "QDialog { background-color: #1E293B; border: 1px solid #334155; border-radius: 12px; }"
    );

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(22, 18, 22, 18);
    layout->setSpacing(12);

    // Encabezado
    auto* headerLayout = new QHBoxLayout();
    headerLayout->setSpacing(10);
    m_iconLabel = new QLabel("🗑️", this);
    m_iconLabel->setStyleSheet("font-size: 22px;");
    headerLayout->addWidget(m_iconLabel);

    m_titleLabel = new QLabel("Eliminando del iPod...", this);
    m_titleLabel->setStyleSheet("color: #F8FAFC; font-size: 16px; font-weight: bold;");
    headerLayout->addWidget(m_titleLabel, 1);

    m_rangeBadgeLabel = new QLabel("0.0 MB a 0.0 MB", this);
    m_rangeBadgeLabel->setStyleSheet(
        "background-color: #0F172A; color: #38BDF8; font-size: 12px; font-weight: bold; "
        "padding: 4px 10px; border-radius: 6px; border: 1px solid #334155;"
    );
    headerLayout->addWidget(m_rangeBadgeLabel);
    layout->addLayout(headerLayout);

    // Estado / Título de la pista
    m_statusLabel = new QLabel("Preparando eliminación...", this);
    m_statusLabel->setStyleSheet("color: #94A3B8; font-size: 13px;");
    m_statusLabel->setWordWrap(true);
    layout->addWidget(m_statusLabel);

    // Barra de progreso
    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 1000);
    m_progressBar->setValue(0);
    m_progressBar->setFixedHeight(22);
    m_progressBar->setTextVisible(false);
    m_progressBar->setStyleSheet(
        "QProgressBar { background-color: #0F172A; border: 1px solid #334155; border-radius: 6px; } "
        "QProgressBar::chunk { background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #2563EB, stop:1 #38BDF8); border-radius: 5px; }"
    );
    layout->addWidget(m_progressBar);

    // Texto de progreso en MB
    auto* infoLayout = new QHBoxLayout();
    m_mbLabel = new QLabel("Progreso: 0.0 MB / 0.0 MB (0%)", this);
    m_mbLabel->setStyleSheet("color: #F8FAFC; font-size: 12px; font-weight: 600;");
    infoLayout->addWidget(m_mbLabel);
    infoLayout->addStretch();

    layout->addLayout(infoLayout);
}

void DeleteProgressDialog::setTitleText(const QString& title, const QString& icon) {
    if (m_titleLabel) m_titleLabel->setText(title);
    if (m_iconLabel) m_iconLabel->setText(icon);
    setWindowTitle(title);
    QApplication::processEvents();
}

void DeleteProgressDialog::setTotalBytes(uint64_t totalBytes) {
    m_totalBytes = totalBytes;
    double totMb = static_cast<double>(totalBytes) / (1024.0 * 1024.0);
    m_rangeBadgeLabel->setText(QString("0.0 MB a %1 MB").arg(totMb, 0, 'f', 1));
    m_mbLabel->setText(QString("Progreso: 0.0 MB / %1 MB (0%)").arg(totMb, 0, 'f', 1));
    m_progressBar->setValue(0);
    QApplication::processEvents();
}

void DeleteProgressDialog::updateProgress(uint64_t currentBytes, const QString& statusText) {
    if (!statusText.isEmpty()) {
        m_statusLabel->setText(statusText);
    }

    double currMb = static_cast<double>(currentBytes) / (1024.0 * 1024.0);
    double totMb = static_cast<double>(m_totalBytes) / (1024.0 * 1024.0);

    double fraction = (m_totalBytes > 0) ? (static_cast<double>(currentBytes) / static_cast<double>(m_totalBytes)) : 1.0;
    if (fraction > 1.0) fraction = 1.0;
    if (fraction < 0.0) fraction = 0.0;

    int progressVal = static_cast<int>(fraction * 1000.0);
    m_progressBar->setValue(progressVal);

    int pct = static_cast<int>(fraction * 100.0);
    m_mbLabel->setText(QString("Progreso: %1 MB / %2 MB (%3%)")
                       .arg(currMb, 0, 'f', 1)
                       .arg(totMb, 0, 'f', 1)
                       .arg(pct));

    m_rangeBadgeLabel->setText(QString("0.0 MB a %1 MB").arg(totMb, 0, 'f', 1));

    QApplication::processEvents();
}

void DeleteProgressDialog::setComplete(const QString& finalMessage) {
    m_progressBar->setValue(1000);
    double totMb = static_cast<double>(m_totalBytes) / (1024.0 * 1024.0);
    m_mbLabel->setText(QString("Progreso: %1 MB / %1 MB (100%)").arg(totMb, 0, 'f', 1));
    m_statusLabel->setText(finalMessage.isEmpty() ? "¡Eliminación completada con éxito!" : finalMessage);
    m_titleLabel->setText("¡Completado!");
    QApplication::processEvents();
    QThread::msleep(450);
}
