#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QMediaPlayer>
#include <QAudioOutput>
#include "Track.h"

class SeekSlider : public QSlider {
    Q_OBJECT

public:
    explicit SeekSlider(Qt::Orientation orientation, QWidget* parent = nullptr);
    ~SeekSlider() override = default;

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    int valueFromPosition(qreal x) const;
    bool m_isDragging{false};
};

class ITunesPlayerWidget : public QWidget {
    Q_OBJECT

public:
    explicit ITunesPlayerWidget(QWidget* parent = nullptr);
    ~ITunesPlayerWidget() override;

    void loadAndPlayTrack(const Track& track, const QString& localFilePath);
    void pause();
    void resume();
    void stop();

    bool isPlaying() const;
    uint32_t currentTrackId() const { return m_currentTrack.id; }

signals:
    void requestNextTrack();
    void requestPreviousTrack();
    void playbackStateChanged(bool isPlaying);

private slots:
    void onPlayPauseClicked();
    void onPrevClicked();
    void onNextClicked();
    void onPositionChanged(qint64 position);
    void onDurationChanged(qint64 duration);
    void onSliderMoved(int position);
    void onSliderPressed();
    void onSliderReleased();
    void onVolumeChanged(int value);
    void onMediaStatusChanged(QMediaPlayer::MediaStatus status);
    void onPlayerStateChanged(QMediaPlayer::PlaybackState state);

private:
    QString formatTime(qint64 ms) const;
    QString formatRemainingTime(qint64 currentMs, qint64 totalMs) const;
    void updateLcdIdle();

    QMediaPlayer* m_player{nullptr};
    QAudioOutput* m_audioOutput{nullptr};

    QPushButton* m_prevBtn{nullptr};
    QPushButton* m_playPauseBtn{nullptr};
    QPushButton* m_nextBtn{nullptr};

    QLabel* m_titleLabel{nullptr};
    QLabel* m_artistAlbumLabel{nullptr};
    QLabel* m_currentTimeLabel{nullptr};
    QLabel* m_totalTimeLabel{nullptr};
    SeekSlider* m_timeSlider{nullptr};
    SeekSlider* m_volumeSlider{nullptr};

    Track m_currentTrack;
    bool m_isSliderSeeking{false};
    qint64 m_targetSeekMs{-1};
    qint64 m_durationMs{0};
};
