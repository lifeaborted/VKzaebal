#pragma once
#include <QObject>
#include <memory>
#include <QMap>
#include <QString>
#include <vector>
#include <string>
#include "core/api/vk/VkClient.h"
#include "core/api/spotify/SpotifyClient.h"
#include "core/api/soundcloud/SoundCloudClient.h"
#include "core/api/yandex/YandexClient.h"

#include "../../../../../Qt/6.11.1/msvc2022_64/include/QtCore/qtmetamacros.h"

class DatabaseManager;
class MiniaudioEngine;
class PlaylistManager;
class TrackDownloader;
class LyricsFetcher;
class NetworkStreamer;
class PlaybackController;
class SourceRouter;
class ConsoleController;
struct Track;

class ApplicationCore : public QObject {
    Q_OBJECT
public:
    explicit ApplicationCore(const QMap<QString, QString>& envVars, QObject* parent = nullptr);
    ~ApplicationCore() override;

    // Инициализация всех подсистем
    bool Initialize();
    // Запуск приложения
    void Start();

private:
    void EnsureDefaultConfig();
    void WireConnections();
    void RestoreSession();
    void SaveSession();

    // Слоты и методы для обработки бизнес-логики, вынесенные из main.cpp
    void InitPlaylistAndStart(bool isOnline);
    void OnAudioFetched(const std::vector<Track>& tracks);
    void OnFinishedFetching();

    // --- DI Контейнер (хранилище зависимостей) ---
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
};