#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <memory>
#include "models/Track.h"

class ApplicationCore;

namespace ui::android {

class TrackListModel;

class AndroidPlayerBridge : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool isPlaying READ isPlaying NOTIFY isPlayingChanged)
    Q_PROPERTY(qint64 positionMs READ positionMs NOTIFY positionChanged)
    Q_PROPERTY(qint64 durationMs READ durationMs NOTIFY durationChanged)
    Q_PROPERTY(QString formattedPosition READ formattedPosition NOTIFY positionChanged)
    Q_PROPERTY(QString formattedDuration READ formattedDuration NOTIFY durationChanged)
    Q_PROPERTY(float volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(QString currentTitle READ currentTitle NOTIFY currentTrackChanged)
    Q_PROPERTY(QString currentArtist READ currentArtist NOTIFY currentTrackChanged)
    Q_PROPERTY(QString currentCoverUrl READ currentCoverUrl NOTIFY currentTrackChanged)
    Q_PROPERTY(QString currentSource READ currentSource NOTIFY currentSourceChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(bool isSearching READ isSearching NOTIFY isSearchingChanged)
    Q_PROPERTY(QStringList availableSources READ availableSources CONSTANT)
    Q_PROPERTY(int activeTabIndex READ activeTabIndex WRITE setActiveTabIndex NOTIFY activeTabIndexChanged)

public:
    explicit AndroidPlayerBridge(ApplicationCore& core, TrackListModel& trackModel, QObject* parent = nullptr);
    ~AndroidPlayerBridge() override = default;

    bool isPlaying() const { return m_isPlaying; }
    qint64 positionMs() const { return m_positionMs; }
    qint64 durationMs() const { return m_durationMs; }
    QString formattedPosition() const;
    QString formattedDuration() const;
    float volume() const { return m_volume; }
    QString currentTitle() const { return m_currentTitle; }
    QString currentArtist() const { return m_currentArtist; }
    QString currentCoverUrl() const { return m_currentCoverUrl; }
    QString currentSource() const { return m_currentSource; }
    QString statusMessage() const { return m_statusMessage; }
    bool isSearching() const { return m_isSearching; }
    QStringList availableSources() const;
    int activeTabIndex() const { return m_activeTabIndex; }
    void setActiveTabIndex(int index);

    // Callbacks from AndroidUiController
    void UpdateCurrentTrack(const Track& track);
    void SetStatus(const QString& msg);
    void OnAudioFetched(const std::vector<Track>& tracks);
    void OnFinishedFetching();

public slots:
    void play();
    void pause();
    void togglePlay();
    void seek(qint64 positionMs);
    void setVolume(float v);
    void nextTrack();
    void prevTrack();
    void search(const QString& query);
    void playTrackAtIndex(int index);
    void switchSource(const QString& sourceName);
    void loadMyTracks();
    void loginVk(const QString& login, const QString& password, const QString& code2fa = "");

signals:
    void isPlayingChanged(bool isPlaying);
    void positionChanged(qint64 positionMs);
    void durationChanged(qint64 durationMs);
    void volumeChanged(float volume);
    void currentTrackChanged();
    void currentSourceChanged(const QString& source);
    void statusMessageChanged(const QString& msg);
    void isSearchingChanged(bool searching);
    void activeTabIndexChanged(int index);
    void authRequired(const QString& source);
    void authSuccess(const QString& source);

private slots:
    void pollAudioState();

private:
    ApplicationCore& m_core;
    TrackListModel& m_trackModel;
    QTimer* m_pollTimer{nullptr};

    bool m_isPlaying{false};
    qint64 m_positionMs{0};
    qint64 m_durationMs{0};
    float m_volume{1.0f};

    QString m_currentTitle{"Не играет"};
    QString m_currentArtist{""};
    QString m_currentCoverUrl{""};
    QString m_currentSource{"vk"};
    QString m_statusMessage{""};
    bool m_isSearching{false};
    int m_activeTabIndex{0}; // 0 = Мои треки, 1 = Поиск

    std::vector<Track> m_myTracks;
    std::string m_lastSearchQuery;
};

} // namespace ui::android
