#include "ITunesPlayerWidget.h"

#include <QUrl>
#include <QFileInfo>
#include <QFrame>
#include <QDebug>
#include <QMouseEvent>
#include <QTimer>

SeekSlider::SeekSlider(Qt::Orientation orientation, QWidget* parent)
    : QSlider(orientation, parent)
{
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
}

int SeekSlider::valueFromPosition(qreal x) const {
    const int w = width();
    if (w <= 0) return minimum();

    const qreal handleHalf = 5.0;
    qreal usableWidth = w - (2.0 * handleHalf);
    if (usableWidth <= 0.0) usableWidth = static_cast<qreal>(w);

    qreal relativeX = x - handleHalf;
    qreal ratio = relativeX / usableWidth;
    ratio = qBound(0.0, ratio, 1.0);

    return minimum() + static_cast<int>(ratio * (maximum() - minimum()) + 0.5);
}

void SeekSlider::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        m_isDragging = true;
        int val = valueFromPosition(event->position().x());
        setValue(val);
        emit sliderPressed();
        emit sliderMoved(val);
        event->accept();
        return;
    }
    QSlider::mousePressEvent(event);
}

void SeekSlider::mouseMoveEvent(QMouseEvent* event) {
    if (m_isDragging && (event->buttons() & Qt::LeftButton)) {
        int val = valueFromPosition(event->position().x());
        setValue(val);
        emit sliderMoved(val);
        event->accept();
        return;
    }
    QSlider::mouseMoveEvent(event);
}

void SeekSlider::mouseReleaseEvent(QMouseEvent* event) {
    if (m_isDragging && event->button() == Qt::LeftButton) {
        m_isDragging = false;
        int val = valueFromPosition(event->position().x());
        setValue(val);
        emit sliderReleased();
        event->accept();
        return;
    }
    QSlider::mouseReleaseEvent(event);
}

ITunesPlayerWidget::ITunesPlayerWidget(QWidget* parent)
    : QWidget(parent)
{
    m_player = new QMediaPlayer(this);
    m_audioOutput = new QAudioOutput(this);
    m_player->setAudioOutput(m_audioOutput);
    m_audioOutput->setVolume(0.8f);

    auto* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(10, 4, 10, 4);
    mainLayout->setSpacing(8);

    // 1. Controles de Transporte (Estilo iTunes 10 - Botones Circulares Metálicos)
    auto* transportBox = new QHBoxLayout();
    transportBox->setSpacing(4);

    m_prevBtn = new QPushButton("⏮", this);
    m_prevBtn->setFixedSize(30, 30);
    m_prevBtn->setToolTip("Pista anterior");
    m_prevBtn->setStyleSheet(
        "QPushButton { "
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #FDFDFD, stop:0.49 #EAEAEA, stop:0.5 #DADADA, stop:1 #C5C5C5); "
        "  border: 1px solid #7E848C; border-radius: 15px; color: #2E3338; font-size: 11px; font-weight: bold; "
        "} "
        "QPushButton:hover { "
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #FFFFFF, stop:0.49 #F2F2F2, stop:0.5 #E2E2E2, stop:1 #D2D2D2); "
        "  border-color: #6A7078; "
        "} "
        "QPushButton:pressed { "
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #B5BAC0, stop:0.5 #C5CACF, stop:1 #D5DADE); "
        "  border-color: #555A60; "
        "}"
    );
    connect(m_prevBtn, &QPushButton::clicked, this, &ITunesPlayerWidget::onPrevClicked);
    transportBox->addWidget(m_prevBtn);

    m_playPauseBtn = new QPushButton("▶", this);
    m_playPauseBtn->setFixedSize(36, 36);
    m_playPauseBtn->setToolTip("Reproducir / Pausar");
    m_playPauseBtn->setStyleSheet(
        "QPushButton { "
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #FFFFFF, stop:0.49 #EDEDED, stop:0.5 #DFDFDF, stop:1 #CACACA); "
        "  border: 1px solid #727880; border-radius: 18px; color: #24282C; font-size: 14px; font-weight: bold; "
        "} "
        "QPushButton:hover { "
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #FFFFFF, stop:0.49 #F5F5F5, stop:0.5 #E8E8E8, stop:1 #D6D6D6); "
        "  border-color: #5A6068; "
        "} "
        "QPushButton:pressed { "
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #A8ADB3, stop:0.5 #BFC4CA, stop:1 #CED3D9); "
        "  border-color: #484D54; "
        "}"
    );
    connect(m_playPauseBtn, &QPushButton::clicked, this, &ITunesPlayerWidget::onPlayPauseClicked);
    transportBox->addWidget(m_playPauseBtn);

    m_nextBtn = new QPushButton("⏭", this);
    m_nextBtn->setFixedSize(30, 30);
    m_nextBtn->setToolTip("Siguiente pista");
    m_nextBtn->setStyleSheet(m_prevBtn->styleSheet());
    connect(m_nextBtn, &QPushButton::clicked, this, &ITunesPlayerWidget::onNextClicked);
    transportBox->addWidget(m_nextBtn);

    mainLayout->addLayout(transportBox);

    // 2. Control de Volumen (Inmediatamente a la derecha de los botones, clásico iTunes 10)
    auto* volBox = new QHBoxLayout();
    volBox->setSpacing(3);

    auto* volDownLbl = new QLabel("🔈", this);
    volDownLbl->setStyleSheet("color: #50565E; font-size: 11px;");
    volBox->addWidget(volDownLbl);

    m_volumeSlider = new SeekSlider(Qt::Horizontal, this);
    m_volumeSlider->setRange(0, 100);
    m_volumeSlider->setValue(80);
    m_volumeSlider->setFixedWidth(64);
    m_volumeSlider->setFixedHeight(16);
    m_volumeSlider->setStyleSheet(
        "QSlider { background: transparent; height: 16px; } "
        "QSlider::groove:horizontal { "
        "  height: 4px; background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #8E949C, stop:1 #B2B8C2); "
        "  border: 1px solid #727882; border-radius: 2px; "
        "} "
        "QSlider::sub-page:horizontal { "
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #4E86E4, stop:1 #2762C6); border-radius: 2px; "
        "} "
        "QSlider::handle:horizontal { "
        "  background: qradialgradient(cx:0.5, cy:0.5, radius:0.5, fx:0.35, fy:0.35, stop:0 #FFFFFF, stop:0.7 #DEE2E6, stop:1 #989EA6); "
        "  border: 1px solid #606670; width: 12px; height: 12px; margin: -4px 0; border-radius: 6px; "
        "} "
        "QSlider::handle:horizontal:hover { border-color: #3875D7; background: #FFFFFF; }"
    );
    connect(m_volumeSlider, &QSlider::valueChanged, this, &ITunesPlayerWidget::onVolumeChanged);
    connect(m_volumeSlider, &QSlider::sliderMoved, this, &ITunesPlayerWidget::onVolumeChanged);
    volBox->addWidget(m_volumeSlider);

    auto* volUpLbl = new QLabel("🔊", this);
    volUpLbl->setStyleSheet("color: #50565E; font-size: 11px;");
    volBox->addWidget(volUpLbl);

    mainLayout->addLayout(volBox);

    // Espaciador flexible izquierdo para centrar la pantalla LCD
    mainLayout->addStretch(1);

    // 3. Pantalla LCD Central (Estilo iTunes 10 - Inset Oscuro Metálico)
    auto* lcdFrame = new QFrame(this);
    lcdFrame->setObjectName("lcdFrame");
    lcdFrame->setMinimumWidth(380);
    lcdFrame->setMaximumWidth(480);
    lcdFrame->setFixedHeight(54);
    lcdFrame->setStyleSheet(
        "QFrame#lcdFrame { "
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #1C1F24, stop:0.12 #14161A, stop:0.88 #0D0E11, stop:1 #22262C); "
        "  border: 1px solid #5C626A; border-top-color: #383B40; border-bottom-color: #8C929C; border-radius: 5px; "
        "}"
    );

    auto* lcdLayout = new QVBoxLayout(lcdFrame);
    lcdLayout->setContentsMargins(8, 4, 8, 4);
    lcdLayout->setSpacing(1);

    // Línea 1: Título de la pista (centrado)
    m_titleLabel = new QLabel(" iPop", lcdFrame);
    m_titleLabel->setAlignment(Qt::AlignCenter);
    m_titleLabel->setStyleSheet("color: #FFFFFF; font-weight: bold; font-size: 11px; font-family: -apple-system, 'Lucida Grande', 'Segoe UI', sans-serif;");
    lcdLayout->addWidget(m_titleLabel);

    // Línea 2: Artista — Álbum (centrado)
    m_artistAlbumLabel = new QLabel("Listo para reproducir", lcdFrame);
    m_artistAlbumLabel->setAlignment(Qt::AlignCenter);
    m_artistAlbumLabel->setStyleSheet("color: #A0A5AD; font-size: 10px; font-family: -apple-system, 'Lucida Grande', 'Segoe UI', sans-serif;");
    lcdLayout->addWidget(m_artistAlbumLabel);

    // Línea 3: Scrubber / Barra de progreso
    auto* scrubberLayout = new QHBoxLayout();
    scrubberLayout->setSpacing(6);
    scrubberLayout->setContentsMargins(0, 0, 0, 0);

    m_currentTimeLabel = new QLabel("0:00", lcdFrame);
    m_currentTimeLabel->setFixedWidth(34);
    m_currentTimeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_currentTimeLabel->setStyleSheet("color: #7E8691; font-size: 10px; font-family: monospace;");
    scrubberLayout->addWidget(m_currentTimeLabel);

    m_timeSlider = new SeekSlider(Qt::Horizontal, lcdFrame);
    m_timeSlider->setRange(0, 1000);
    m_timeSlider->setValue(0);
    m_timeSlider->setFixedHeight(16);
    m_timeSlider->setStyleSheet(
        "QSlider { background: transparent; height: 16px; } "
        "QSlider::groove:horizontal { height: 4px; background: #282C32; border-radius: 2px; } "
        "QSlider::sub-page:horizontal { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #5C95F2, stop:1 #2867CE); border-radius: 2px; } "
        "QSlider::handle:horizontal { "
        "  background: qradialgradient(cx:0.5, cy:0.5, radius:0.5, fx:0.35, fy:0.35, stop:0 #FFFFFF, stop:0.7 #E2E6EA, stop:1 #969CA6); "
        "  border: 1px solid #484D54; width: 10px; height: 10px; margin: -3px 0; border-radius: 5px; "
        "} "
        "QSlider::handle:horizontal:hover { border-color: #5C95F2; background: #FFFFFF; }"
    );
    connect(m_timeSlider, &QSlider::sliderPressed, this, &ITunesPlayerWidget::onSliderPressed);
    connect(m_timeSlider, &QSlider::sliderReleased, this, &ITunesPlayerWidget::onSliderReleased);
    connect(m_timeSlider, &QSlider::sliderMoved, this, &ITunesPlayerWidget::onSliderMoved);
    scrubberLayout->addWidget(m_timeSlider, 1);

    m_totalTimeLabel = new QLabel("0:00", lcdFrame);
    m_totalTimeLabel->setFixedWidth(38);
    m_totalTimeLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_totalTimeLabel->setStyleSheet("color: #7E8691; font-size: 10px; font-family: monospace;");
    scrubberLayout->addWidget(m_totalTimeLabel);

    lcdLayout->addLayout(scrubberLayout);
    mainLayout->addWidget(lcdFrame);

    // Espaciador flexible derecho para mantener el LCD en el centro
    mainLayout->addStretch(1);

    // Conectar eventos del QMediaPlayer
    connect(m_player, &QMediaPlayer::positionChanged, this, &ITunesPlayerWidget::onPositionChanged);
    connect(m_player, &QMediaPlayer::durationChanged, this, &ITunesPlayerWidget::onDurationChanged);
    connect(m_player, &QMediaPlayer::mediaStatusChanged, this, &ITunesPlayerWidget::onMediaStatusChanged);
    connect(m_player, &QMediaPlayer::playbackStateChanged, this, &ITunesPlayerWidget::onPlayerStateChanged);
}

ITunesPlayerWidget::~ITunesPlayerWidget() {
    if (m_player) {
        m_player->stop();
    }
}

void ITunesPlayerWidget::loadAndPlayTrack(const Track& track, const QString& localFilePath) {
    if (localFilePath.isEmpty() || !QFile::exists(localFilePath)) {
        qWarning() << "Archivo local de audio no encontrado:" << localFilePath;
        return;
    }

    m_currentTrack = track;
    m_durationMs = track.durationMs > 0 ? track.durationMs : 0;
    m_targetSeekMs = -1;
    m_isSliderSeeking = false;

    QString title = m_currentTrack.title.trimmed().isEmpty() ? "Pista sin título" : m_currentTrack.title.trimmed();
    QString artistAlbum;
    if (!m_currentTrack.artist.trimmed().isEmpty() && !m_currentTrack.album.trimmed().isEmpty()) {
        artistAlbum = QString("%1 — %2").arg(m_currentTrack.artist.trimmed()).arg(m_currentTrack.album.trimmed());
    } else if (!m_currentTrack.artist.trimmed().isEmpty()) {
        artistAlbum = m_currentTrack.artist.trimmed();
    } else if (!m_currentTrack.album.trimmed().isEmpty()) {
        artistAlbum = m_currentTrack.album.trimmed();
    } else {
        artistAlbum = "Artista desconocido";
    }

    QFontMetrics fmTitle(m_titleLabel->font());
    m_titleLabel->setText(fmTitle.elidedText(title, Qt::ElideRight, 380));

    QFontMetrics fmArtist(m_artistAlbumLabel->font());
    m_artistAlbumLabel->setText(fmArtist.elidedText(artistAlbum, Qt::ElideRight, 380));

    m_currentTimeLabel->setText("0:00");
    m_totalTimeLabel->setText(formatRemainingTime(0, m_durationMs));

    m_player->setSource(QUrl::fromLocalFile(localFilePath));
    m_player->play();
}

void ITunesPlayerWidget::pause() {
    m_player->pause();
}

void ITunesPlayerWidget::resume() {
    m_player->play();
}

void ITunesPlayerWidget::stop() {
    m_player->stop();
    m_currentTimeLabel->setText("0:00");
    m_totalTimeLabel->setText("0:00");
    m_timeSlider->setValue(0);
    m_playPauseBtn->setText("▶");
    updateLcdIdle();
}

bool ITunesPlayerWidget::isPlaying() const {
    return m_player->playbackState() == QMediaPlayer::PlayingState;
}

void ITunesPlayerWidget::onPlayPauseClicked() {
    if (isPlaying()) {
        pause();
    } else {
        if (m_player->playbackState() == QMediaPlayer::PausedState) {
            resume();
        } else {
            emit requestNextTrack();
        }
    }
}

void ITunesPlayerWidget::onPrevClicked() {
    if (m_player->position() > 3000) {
        m_player->setPosition(0);
    } else {
        emit requestPreviousTrack();
    }
}

void ITunesPlayerWidget::onNextClicked() {
    emit requestNextTrack();
}

void ITunesPlayerWidget::onPositionChanged(qint64 position) {
    if (m_isSliderSeeking) {
        return;
    }
    qint64 totalDur = (m_durationMs > 0) ? m_durationMs : m_player->duration();
    m_currentTimeLabel->setText(formatTime(position));
    m_totalTimeLabel->setText(formatRemainingTime(position, totalDur));
    if (totalDur > 0) {
        int sliderVal = static_cast<int>((position * 1000) / totalDur);
        sliderVal = qBound(0, sliderVal, 1000);
        m_timeSlider->blockSignals(true);
        m_timeSlider->setValue(sliderVal);
        m_timeSlider->blockSignals(false);
    }
}

void ITunesPlayerWidget::onDurationChanged(qint64 duration) {
    if (duration > 0) {
        m_durationMs = duration;
        m_totalTimeLabel->setText(formatRemainingTime(m_player->position(), duration));
    }
}

void ITunesPlayerWidget::onSliderPressed() {
    m_isSliderSeeking = true;
}

void ITunesPlayerWidget::onSliderReleased() {
    qint64 totalDur = (m_durationMs > 0) ? m_durationMs : m_player->duration();
    if (totalDur > 0) {
        qint64 targetMs = (static_cast<qint64>(m_timeSlider->value()) * totalDur) / 1000;
        targetMs = qBound<qint64>(0, targetMs, totalDur);
        m_targetSeekMs = targetMs;
        m_currentTimeLabel->setText(formatTime(targetMs));
        m_totalTimeLabel->setText(formatRemainingTime(targetMs, totalDur));
        m_player->setPosition(targetMs);
    }
    // Retener m_isSliderSeeking durante 350ms para evitar que los ticks de buffer viejos
    // de QMediaPlayer reviertan el scrubber a la posición anterior antes de completar el salto
    QTimer::singleShot(350, this, [this]() {
        m_isSliderSeeking = false;
        m_targetSeekMs = -1;
    });
}

void ITunesPlayerWidget::onSliderMoved(int position) {
    qint64 totalDur = (m_durationMs > 0) ? m_durationMs : m_player->duration();
    if (totalDur > 0) {
        qint64 previewMs = (static_cast<qint64>(position) * totalDur) / 1000;
        previewMs = qBound<qint64>(0, previewMs, totalDur);
        m_currentTimeLabel->setText(formatTime(previewMs));
        m_totalTimeLabel->setText(formatRemainingTime(previewMs, totalDur));
    }
}

void ITunesPlayerWidget::onVolumeChanged(int value) {
    if (m_audioOutput) {
        m_audioOutput->setVolume(value / 100.0f);
    }
}

void ITunesPlayerWidget::onMediaStatusChanged(QMediaPlayer::MediaStatus status) {
    if (status == QMediaPlayer::EndOfMedia) {
        emit requestNextTrack();
    }
}

void ITunesPlayerWidget::onPlayerStateChanged(QMediaPlayer::PlaybackState state) {
    if (state == QMediaPlayer::PlayingState) {
        m_playPauseBtn->setText("⏸");
        emit playbackStateChanged(true);
    } else {
        m_playPauseBtn->setText("▶");
        emit playbackStateChanged(false);
    }
}

QString ITunesPlayerWidget::formatTime(qint64 ms) const {
    if (ms <= 0) return "0:00";
    qint64 totalSecs = ms / 1000;
    qint64 mins = totalSecs / 60;
    qint64 secs = totalSecs % 60;
    return QString::asprintf("%lld:%02lld", mins, secs);
}

QString ITunesPlayerWidget::formatRemainingTime(qint64 currentMs, qint64 totalMs) const {
    if (totalMs <= 0) return "0:00";
    qint64 remMs = qMax<qint64>(0, totalMs - currentMs);
    qint64 totalSecs = remMs / 1000;
    qint64 mins = totalSecs / 60;
    qint64 secs = totalSecs % 60;
    return QString::asprintf("-%lld:%02lld", mins, secs);
}

void ITunesPlayerWidget::updateLcdIdle() {
    m_titleLabel->setText(" iPop");
    m_artistAlbumLabel->setText("Listo para reproducir");
    m_currentTimeLabel->setText("0:00");
    m_totalTimeLabel->setText("0:00");
}
