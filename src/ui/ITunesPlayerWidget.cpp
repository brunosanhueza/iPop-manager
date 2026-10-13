#include "ITunesPlayerWidget.h"

#include <QUrl>
#include <QFileInfo>
#include <QFrame>
#include <QDebug>

ITunesPlayerWidget::ITunesPlayerWidget(QWidget* parent)
    : QWidget(parent)
{
    m_player = new QMediaPlayer(this);
    m_audioOutput = new QAudioOutput(this);
    m_player->setAudioOutput(m_audioOutput);
    m_audioOutput->setVolume(0.8f);

    auto* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(12, 6, 12, 6);
    mainLayout->setSpacing(14);

    // 1. Controles de Transporte (Izquierda)
    auto* transportBox = new QHBoxLayout();
    transportBox->setSpacing(8);

    m_prevBtn = new QPushButton("⏮", this);
    m_prevBtn->setFixedSize(36, 36);
    m_prevBtn->setToolTip("Pista anterior");
    m_prevBtn->setStyleSheet(
        "QPushButton { background-color: #334155; color: #F1F5F9; border-radius: 18px; font-size: 14px; border: 1px solid #475569; } "
        "QPushButton:hover { background-color: #475569; } "
        "QPushButton:pressed { background-color: #1E293B; }"
    );
    connect(m_prevBtn, &QPushButton::clicked, this, &ITunesPlayerWidget::onPrevClicked);
    transportBox->addWidget(m_prevBtn);

    m_playPauseBtn = new QPushButton("▶", this);
    m_playPauseBtn->setFixedSize(44, 44);
    m_playPauseBtn->setToolTip("Reproducir / Pausar");
    m_playPauseBtn->setStyleSheet(
        "QPushButton { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #38BDF8, stop:1 #2563EB); "
        "color: white; border-radius: 22px; font-size: 18px; font-weight: bold; border: 1px solid #60A5FA; } "
        "QPushButton:hover { background: #38BDF8; } "
        "QPushButton:pressed { background: #1D4ED8; }"
    );
    connect(m_playPauseBtn, &QPushButton::clicked, this, &ITunesPlayerWidget::onPlayPauseClicked);
    transportBox->addWidget(m_playPauseBtn);

    m_nextBtn = new QPushButton("⏭", this);
    m_nextBtn->setFixedSize(36, 36);
    m_nextBtn->setToolTip("Siguiente pista");
    m_nextBtn->setStyleSheet(
        "QPushButton { background-color: #334155; color: #F1F5F9; border-radius: 18px; font-size: 14px; border: 1px solid #475569; } "
        "QPushButton:hover { background-color: #475569; } "
        "QPushButton:pressed { background-color: #1E293B; }"
    );
    connect(m_nextBtn, &QPushButton::clicked, this, &ITunesPlayerWidget::onNextClicked);
    transportBox->addWidget(m_nextBtn);

    mainLayout->addLayout(transportBox);

    // 2. Pantalla LCD Central (Estilo iTunes)
    auto* lcdFrame = new QFrame(this);
    lcdFrame->setStyleSheet(
        "QFrame { background-color: #020617; border: 1px solid #1E293B; border-radius: 8px; padding: 4px 10px; }"
    );
    lcdFrame->setMinimumWidth(380);
    lcdFrame->setMaximumWidth(580);

    auto* lcdLayout = new QVBoxLayout(lcdFrame);
    lcdLayout->setContentsMargins(8, 4, 8, 4);
    lcdLayout->setSpacing(2);

    auto* topInfoLayout = new QHBoxLayout();
    m_titleLabel = new QLabel(" iPop Media Player", lcdFrame);
    m_titleLabel->setStyleSheet("color: #F8FAFC; font-weight: bold; font-size: 13px;");
    topInfoLayout->addWidget(m_titleLabel);

    topInfoLayout->addStretch();

    m_artistAlbumLabel = new QLabel("Listo para reproducir", lcdFrame);
    m_artistAlbumLabel->setStyleSheet("color: #94A3B8; font-size: 11px;");
    topInfoLayout->addWidget(m_artistAlbumLabel);
    lcdLayout->addLayout(topInfoLayout);

    // Scrubber / Barra de progreso
    auto* scrubberLayout = new QHBoxLayout();
    scrubberLayout->setSpacing(6);

    m_currentTimeLabel = new QLabel("0:00", lcdFrame);
    m_currentTimeLabel->setFixedWidth(36);
    m_currentTimeLabel->setStyleSheet("color: #64748B; font-size: 11px; font-family: monospace;");
    scrubberLayout->addWidget(m_currentTimeLabel);

    m_timeSlider = new QSlider(Qt::Horizontal, lcdFrame);
    m_timeSlider->setRange(0, 1000);
    m_timeSlider->setValue(0);
    m_timeSlider->setStyleSheet(
        "QSlider::groove:horizontal { height: 4px; background: #334155; border-radius: 2px; } "
        "QSlider::sub-page:horizontal { background: #38BDF8; border-radius: 2px; } "
        "QSlider::handle:horizontal { background: #FFFFFF; width: 10px; height: 10px; margin: -3px 0; border-radius: 5px; } "
        "QSlider::handle:horizontal:hover { background: #38BDF8; transform: scale(1.2); }"
    );
    connect(m_timeSlider, &QSlider::sliderPressed, this, &ITunesPlayerWidget::onSliderPressed);
    connect(m_timeSlider, &QSlider::sliderReleased, this, &ITunesPlayerWidget::onSliderReleased);
    connect(m_timeSlider, &QSlider::sliderMoved, this, &ITunesPlayerWidget::onSliderMoved);
    scrubberLayout->addWidget(m_timeSlider, 1);

    m_totalTimeLabel = new QLabel("0:00", lcdFrame);
    m_totalTimeLabel->setFixedWidth(40);
    m_totalTimeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_totalTimeLabel->setStyleSheet("color: #64748B; font-size: 11px; font-family: monospace;");
    scrubberLayout->addWidget(m_totalTimeLabel);

    lcdLayout->addLayout(scrubberLayout);
    mainLayout->addWidget(lcdFrame, 1);

    // 3. Control de Volumen (Derecha)
    auto* volBox = new QHBoxLayout();
    volBox->setSpacing(6);

    auto* volDownLbl = new QLabel("🔈", this);
    volDownLbl->setStyleSheet("color: #94A3B8; font-size: 13px;");
    volBox->addWidget(volDownLbl);

    m_volumeSlider = new QSlider(Qt::Horizontal, this);
    m_volumeSlider->setRange(0, 100);
    m_volumeSlider->setValue(80);
    m_volumeSlider->setFixedWidth(80);
    m_volumeSlider->setStyleSheet(
        "QSlider::groove:horizontal { height: 4px; background: #334155; border-radius: 2px; } "
        "QSlider::sub-page:horizontal { background: #94A3B8; border-radius: 2px; } "
        "QSlider::handle:horizontal { background: #E2E8F0; width: 10px; height: 10px; margin: -3px 0; border-radius: 5px; }"
    );
    connect(m_volumeSlider, &QSlider::valueChanged, this, &ITunesPlayerWidget::onVolumeChanged);
    volBox->addWidget(m_volumeSlider);

    auto* volUpLbl = new QLabel("🔊", this);
    volUpLbl->setStyleSheet("color: #94A3B8; font-size: 13px;");
    volBox->addWidget(volUpLbl);

    mainLayout->addLayout(volBox);

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

    m_titleLabel->setText(m_currentTrack.title.isEmpty() ? "Pista sin título" : m_currentTrack.title);
    m_artistAlbumLabel->setText(QString("%1 — %2").arg(m_currentTrack.artist).arg(m_currentTrack.album));
    m_currentTimeLabel->setText("0:00");
    m_totalTimeLabel->setText(formatTime(m_durationMs));

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
    if (!m_isSliderSeeking) {
        m_currentTimeLabel->setText(formatTime(position));
        if (m_durationMs > 0) {
            int sliderVal = static_cast<int>((position * 1000) / m_durationMs);
            m_timeSlider->blockSignals(true);
            m_timeSlider->setValue(sliderVal);
            m_timeSlider->blockSignals(false);
        }
    }
}

void ITunesPlayerWidget::onDurationChanged(qint64 duration) {
    if (duration > 0) {
        m_durationMs = duration;
        m_totalTimeLabel->setText(formatTime(duration));
    }
}

void ITunesPlayerWidget::onSliderPressed() {
    m_isSliderSeeking = true;
}

void ITunesPlayerWidget::onSliderReleased() {
    m_isSliderSeeking = false;
    if (m_durationMs > 0) {
        qint64 targetMs = (static_cast<qint64>(m_timeSlider->value()) * m_durationMs) / 1000;
        m_player->setPosition(targetMs);
    }
}

void ITunesPlayerWidget::onSliderMoved(int position) {
    if (m_durationMs > 0) {
        qint64 previewMs = (static_cast<qint64>(position) * m_durationMs) / 1000;
        m_currentTimeLabel->setText(formatTime(previewMs));
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

void ITunesPlayerWidget::updateLcdIdle() {
    m_titleLabel->setText(" iPop Media Player");
    m_artistAlbumLabel->setText("Listo para reproducir");
    m_currentTimeLabel->setText("0:00");
    m_totalTimeLabel->setText("0:00");
}
