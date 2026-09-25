#pragma once

#include <QObject>
#include <memory>
#include <QMap>
#include <QString>
#include <vector>
#include <string>
#include <unordered_set>

class DatabaseManager;
class MiniaudioEngine;
class PlaylistManager;
class TrackDownloader;
class LyricsFetcher;
class NetworkStreamer;
class PlaybackController;
class SourceRouter;
class ConsoleController;
class ConfigurationService;
class PlaybackSessionService;
class QNetworkAccessManager;
class QTimer;
struct Track;

class ApplicationCore : public QObject {
    Q_OBJECT
public:
    explicit ApplicationCore(const QMap<QString, QString>& envVars, QObject* parent = nullptr);
    ~ApplicationCore() override;

    bool Initialize();
    void Start();

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
    std::unique_ptr<ConsoleController> m_console;

    QMap<QString, QString> m_envVars;
    std::string m_activeSource;
    bool m_isPlaybackStarted = false;
    int m_vkSyncIndex = 0;
    std::unordered_set<std::string> m_syncExistingIds;
    QTimer* m_audioPollTimer = nullptr;
};