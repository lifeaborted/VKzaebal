#pragma once

#include <QObject>
#include <memory>
#include <functional>
#include <QMap>
#include <QString>
#include <vector>
#include <string>
#include <unordered_set>

#include "ui/IUiController.h"

class DatabaseManager;
class IAudioEngine;
class MiniaudioEngine;
class PlaylistManager;
class TrackDownloader;
class LyricsFetcher;
class NetworkStreamer;
class PlaybackController;
class SourceRouter;
class OAuthManager;
class ConfigurationService;
class PlaybackSessionService;
class QNetworkAccessManager;
class QTimer;
struct Track;

class ApplicationCore : public QObject {
    Q_OBJECT
public:
    using UiFactoryFn = std::function<std::unique_ptr<IUiController>(ApplicationCore&)>;

    explicit ApplicationCore(const QMap<QString, QString>& envVars, UiMode uiMode = UiMode::TUI, QObject* parent = nullptr);
    ~ApplicationCore() override;

    void SetUiMode(UiMode mode) { m_uiMode = mode; }
    UiMode GetUiMode() const { return m_uiMode; }

    void SetUiFactory(UiFactoryFn factory) { m_uiFactory = std::move(factory); }
    void SetUi(std::unique_ptr<IUiController> ui) { m_ui = std::move(ui); }
    IUiController* GetUi() const { return m_ui.get(); }

    bool Initialize();
    void Start();

    // --- Service Getters (DI / UiFactory) ---
    IAudioEngine& GetAudio() const;
    PlaylistManager& GetPlaylist() const;
    SourceRouter& GetRouter() const;
    OAuthManager* GetAuthManager() const;
    DatabaseManager& GetDbManager() const;
    TrackDownloader& GetDownloader() const;
    LyricsFetcher& GetLyricsFetcher() const;
    PlaybackController& GetPlaybackCtrl() const;
    ConfigurationService* GetConfigService() const;
    QNetworkAccessManager* GetNetworkManager() const;
    PlaybackSessionService* GetSessionService() const;
    NetworkStreamer& GetStreamer() const;

private:
    void WireConnections();
    void InitPlaylistAndStart(bool isOnline);
    void OnAudioFetched(const std::vector<Track>& tracks);
    void OnFinishedFetching();
    void HandleLogout(const std::string& service);

    // --- DI Контейнер (хранилище зависимостей и сервисов) ---
    std::unique_ptr<ConfigurationService> m_configService;
    std::unique_ptr<PlaybackSessionService> m_sessionService;
    std::unique_ptr<QNetworkAccessManager> m_networkManager;

    std::unique_ptr<DatabaseManager> m_dbManager;
    std::unique_ptr<MiniaudioEngine> m_audio;
    std::unique_ptr<PlaylistManager> m_playlist;
    std::unique_ptr<TrackDownloader> m_downloader;
    std::unique_ptr<LyricsFetcher> m_lyricsFetcher;
    std::unique_ptr<NetworkStreamer> m_streamer;
    std::unique_ptr<PlaybackController> m_playbackCtrl;
    std::unique_ptr<SourceRouter> m_router;

    std::unique_ptr<IUiController> m_ui;
    UiFactoryFn m_uiFactory;
    UiMode m_uiMode = UiMode::TUI;

    QMap<QString, QString> m_envVars;
    std::string m_activeSource;
    bool m_isPlaybackStarted = false;
    int m_syncIndex = 0;
    std::unordered_set<std::string> m_syncExistingIds;
    QTimer* m_audioPollTimer = nullptr;
};