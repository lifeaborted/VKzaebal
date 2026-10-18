#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <memory>
#include <deque>
#include <mutex>
#include "models/Track.h"

class ApplicationCore;
class AudioCaptureService;

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
    Q_PROPERTY(QString currentTrackSource READ currentTrackSource NOTIFY currentTrackChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(bool isSearching READ isSearching NOTIFY isSearchingChanged)
    Q_PROPERTY(QStringList availableSources READ availableSources CONSTANT)
    Q_PROPERTY(int activeTabIndex READ activeTabIndex WRITE setActiveTabIndex NOTIFY activeTabIndexChanged)
    Q_PROPERTY(bool isShuffle READ isShuffle WRITE setShuffle NOTIFY shuffleChanged)
    Q_PROPERTY(int repeatMode READ repeatMode WRITE setRepeatMode NOTIFY repeatModeChanged)
    Q_PROPERTY(QString currentLyrics READ currentLyrics NOTIFY currentLyricsChanged)
    Q_PROPERTY(QStringList customPlaylists READ customPlaylists NOTIFY customPlaylistsChanged)
    Q_PROPERTY(QString fullLogs READ fullLogs NOTIFY logsUpdated)
    Q_PROPERTY(bool isCurrentFavorite READ isCurrentFavorite NOTIFY currentTrackChanged)

    // Crossfade properties
    Q_PROPERTY(bool isCrossfade READ isCrossfade WRITE setCrossfade NOTIFY crossfadeChanged)
    Q_PROPERTY(int crossfadeDuration READ crossfadeDuration WRITE setCrossfadeDuration NOTIFY crossfadeDurationChanged)

    // Audio Cache properties
    Q_PROPERTY(bool isAudioCacheEnabled READ isAudioCacheEnabled WRITE setAudioCacheEnabled NOTIFY audioCacheChanged)
    Q_PROPERTY(int audioCacheMaxMb READ audioCacheMaxMb WRITE setAudioCacheMaxMb NOTIFY audioCacheChanged)
    Q_PROPERTY(int audioCacheUsedMb READ audioCacheUsedMb NOTIFY audioCacheChanged)

    // Shazam properties
    Q_PROPERTY(bool isShazamRecognizing READ isShazamRecognizing NOTIFY shazamStateChanged)
    Q_PROPERTY(QString shazamStatus READ shazamStatus NOTIFY shazamStateChanged)
    Q_PROPERTY(QString shazamResultTitle READ shazamResultTitle NOTIFY shazamStateChanged)
    Q_PROPERTY(QString shazamResultArtist READ shazamResultArtist NOTIFY shazamStateChanged)

    // Authorization properties
    Q_PROPERTY(bool isAuthorized READ isAuthorized NOTIFY isAuthorizedChanged)
    Q_PROPERTY(QString authStep READ authStep NOTIFY authStepChanged)
    Q_PROPERTY(QString authStatusMessage READ authStatusMessage NOTIFY authStatusMessageChanged)
    Q_PROPERTY(QString authMethod READ authMethod NOTIFY authStepChanged)
    Q_PROPERTY(QString authPhoneMask READ authPhoneMask NOTIFY authStepChanged)
    Q_PROPERTY(QString authCaptchaImg READ authCaptchaImg NOTIFY authStepChanged)
    Q_PROPERTY(QString authError READ authError NOTIFY authStepChanged)
    Q_PROPERTY(bool isAuthDialogOpen READ isAuthDialogOpen WRITE setAuthDialogOpen NOTIFY authDialogOpenChanged)

    // WebView Auth properties
    Q_PROPERTY(bool isWebViewAuthOpen READ isWebViewAuthOpen WRITE setWebViewAuthOpen NOTIFY webViewAuthOpenChanged)
    Q_PROPERTY(QString webViewAuthUrl READ webViewAuthUrl NOTIFY webViewAuthUrlChanged)
    Q_PROPERTY(QString webViewAuthService READ webViewAuthService NOTIFY webViewAuthServiceChanged)
    Q_PROPERTY(QString searchSource READ searchSource WRITE setSearchSource NOTIFY searchSourceChanged)

    // Equalizer properties
    Q_PROPERTY(bool isEqualizerEnabled READ isEqualizerEnabled WRITE setEqualizerEnabled NOTIFY equalizerChanged)
    Q_PROPERTY(QString equalizerPreset READ equalizerPreset WRITE setEqualizerPreset NOTIFY equalizerChanged)

public:
    explicit AndroidPlayerBridge(ApplicationCore& core, TrackListModel& trackModel, QObject* parent = nullptr);
    ~AndroidPlayerBridge() override;

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
    QString currentTrackSource() const { return m_currentTrackSource.isEmpty() ? m_currentSource : m_currentTrackSource; }
    QString statusMessage() const { return m_statusMessage; }
    bool isSearching() const { return m_isSearching; }
    QStringList availableSources() const;
    int activeTabIndex() const { return m_activeTabIndex; }

    // Auth getters
    bool isAuthorized() const { return m_isAuthorized; }
    QString authStep() const { return m_authStep; }
    QString authStatusMessage() const { return m_authStatusMessage; }
    QString authMethod() const { return m_authMethod; }
    QString authPhoneMask() const { return m_authPhoneMask; }
    QString authCaptchaImg() const { return m_authCaptchaImg; }
    QString authError() const { return m_authError; }
    bool isAuthDialogOpen() const { return m_isAuthDialogOpen; }

    // WebView Auth getters
    bool isWebViewAuthOpen() const { return m_isWebViewAuthOpen; }
    QString webViewAuthUrl() const { return m_webViewAuthUrl; }
    QString webViewAuthService() const { return m_webViewAuthService; }
    QString searchSource() const { return m_searchSource; }

    // Shuffle & Logs getters
    bool isShuffle() const;
    QString fullLogs() const;
    int repeatMode() const;
    QString currentLyrics() const { return m_currentLyrics; }
    QStringList customPlaylists() const;
    bool isCurrentFavorite() const;

    // Crossfade getters
    bool isCrossfade() const;
    int crossfadeDuration() const;

    // Audio Cache getters
    bool isAudioCacheEnabled() const;
    int audioCacheMaxMb() const;
    int audioCacheUsedMb() const;

    // Shazam getters
    bool isShazamRecognizing() const { return m_isShazamRecognizing; }
    QString shazamStatus() const { return m_shazamStatus; }
    QString shazamResultTitle() const { return m_shazamResultTitle; }
    QString shazamResultArtist() const { return m_shazamResultArtist; }

    // Equalizer getters
    bool isEqualizerEnabled() const;
    QString equalizerPreset() const;

    // Callbacks from AndroidUiController
    void UpdateCurrentTrack(const Track& track);
    void SetStatus(const QString& msg);
    void OnAudioFetched(const std::vector<Track>& tracks);
    void OnFinishedFetching();

public slots:
    void setActiveTabIndex(int index);
    void setAuthDialogOpen(bool open);
    void setWebViewAuthOpen(bool open);
    void setSearchSource(const QString& src);
    void setCrossfade(bool enabled);
    void setCrossfadeDuration(int seconds);
    void setAudioCacheEnabled(bool enabled);
    void setAudioCacheMaxMb(int mb);
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

    // Shuffle & Repeat slots
    void toggleShuffle();
    void setShuffle(bool enable);
    void toggleRepeat();
    void setRepeatMode(int mode);

    // Lyrics & Offline download slots
    void fetchLyrics();
    void downloadTrack(int index);
    void downloadCurrentTrack();
    void deleteDownloadedTrack(int index);
    void toggleCurrentFavorite();

    // Custom Playlists slots
    void createPlaylist(const QString& name);
    void deletePlaylist(const QString& name);
    void addTrackToPlaylist(const QString& playlistName, int trackIndex);
    void playCustomPlaylist(const QString& name);
    void removeTrackFromQueue(int index);
    void exportPlaylistTxt();

    // Logs slots
    void clearLogs();
    void refreshLogs();
    void copyLogsToClipboard();

    // Favorites slots
    void addTrackToFavorites(int index);
    void removeTrackFromFavorites(int index);

    // WebView Auth slots
    void startWebViewAuth(const QString& service = "");
    void closeWebViewAuth();
    void onWebViewUrlChanged(const QString& url);
    void onScTokenIntercepted(const QString& token);
    void onYtAuthIntercepted(const QString& cookies);

    // Native Auth slots
    void startVkLogin(const QString& login, const QString& password = "");
    void submitVkPassword(const QString& password);
    void submitVkCode(const QString& code);
    void submitVkCaptcha(const QString& captchaKey);
    void saveToken(const QString& service, const QString& token, const QString& secret = "");
    void logout(const QString& service = "");
    void checkCurrentSourceAuth();
    void openAuthDialog();
    void closeAuthDialog();
    void cancelAuth();

    // Shazam slots
    void startShazamRecognition();
    void resetShazam();

    // Audio Cache slots
    void clearAudioCache();

    // Equalizer slots
    void setEqualizerEnabled(bool enabled);
    void setEqualizerPreset(const QString& preset);
    Q_INVOKABLE float getEqualizerBand(int band) const;
    Q_INVOKABLE void setEqualizerBand(int band, float gainDb);
    Q_INVOKABLE void resetEqualizer();
    Q_INVOKABLE QVariantList getEqualizerBands() const;
    Q_INVOKABLE void applyPreset(const QString& presetName);

    // Sorting & Queue slots
    Q_INVOKABLE void sortTracks(const QString& mode);
    Q_INVOKABLE void shuffleTracks();
    Q_INVOKABLE void openFavoritesForSource(const QString& source);
    Q_INVOKABLE void addTrackToQueue(int index);
    Q_INVOKABLE bool isTrackInQueue(int index) const;

    // Network Playlist Import
    Q_INVOKABLE void importNetworkPlaylist(const QString& url);

    // Cover Cache
    Q_INVOKABLE QString getCoverUrl(const QString& remoteUrl, bool isThumbnail = false);

    // Backward-compatible alias
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

    // Auth signals
    void isAuthorizedChanged(bool authorized);
    void authStepChanged(const QString& step);
    void authStatusMessageChanged(const QString& msg);
    void authDialogOpenChanged(bool open);
    void authRequired(const QString& source);
    void authSuccess(const QString& source);

    // WebView Auth signals
    void webViewAuthOpenChanged(bool open);
    void webViewAuthUrlChanged(const QString& url);
    void webViewAuthServiceChanged(const QString& service);
    void searchSourceChanged(const QString& src);

    // Shuffle & Logs signals
    void shuffleChanged(bool shuffle);
    void repeatModeChanged(int mode);
    void currentLyricsChanged();
    void customPlaylistsChanged();
    void logsUpdated();
    void newLogLine(const QString& line);

    // Crossfade, Cache & Shazam signals
    void crossfadeChanged(bool enabled);
    void crossfadeDurationChanged(int seconds);
    void audioCacheChanged();
    void shazamStateChanged();
    void equalizerChanged();

private slots:
    void pollAudioState();

private:
    void addLogLine(const QString& line);
    mutable std::mutex m_logMutex;
    std::deque<QString> m_logLines;
    bool m_isShuffleCached{false};
    int m_repeatModeCached{1};
    QString m_currentLyrics{""};

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
    QString m_currentRemoteCoverUrl{""};
    QString m_currentSource{"VK"};
    QString m_currentTrackSource{""};
    QString m_statusMessage{""};
    bool m_isSearching{false};
    int m_activeTabIndex{0}; // 0 = Мои треки, 1 = Поиск

    // Auth state
    bool m_isAuthorized{false};
    QString m_authStep{"idle"}; // "idle", "authenticating", "need_password", "need_code", "need_captcha", "success", "error"
    QString m_authStatusMessage{""};
    QString m_authMethod{""};
    QString m_authPhoneMask{""};
    QString m_authCaptchaImg{""};
    QString m_authCaptchaSid{""};
    QString m_authError{""};
    bool m_isAuthDialogOpen{false};

    // WebView Auth state
    bool m_isWebViewAuthOpen{false};
    QString m_webViewAuthUrl{""};
    QString m_webViewAuthService{""};
    QString m_searchSource{"All"};

    std::vector<Track> m_myTracks;
    std::string m_lastSearchQuery;

    // Shazam state
    bool m_isShazamRecognizing{false};
    QString m_shazamStatus{"Нажмите кнопку для распознавания трека"};
    QString m_shazamResultTitle{""};
    QString m_shazamResultArtist{""};
    std::unique_ptr<AudioCaptureService> m_audioCapture;

    // Async tasks & caching
    void loadTracksForSourceAsync(const QString& sourceName, bool isSwitchSource);
    void updateCustomPlaylistsCache();
    void updateAudioCacheUsedMb();
    int m_currentSearchGen{0};
    int m_loadTracksGen{0};
    QStringList m_customPlaylistsCached;
    int m_audioCacheUsedMbCached{0};
};

} // namespace ui::android
