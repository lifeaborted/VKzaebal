#pragma once

#include <memory>
#include <QObject>
#include <QPointer>
#include <QString>
#include <atomic>
#include <functional>
#include <thread>

// Forward declarations
#include "models/Track.h"
#include "services/database/DatabaseManager.h"

class IAudioEngine;
class PlaylistManager;
class OAuthManager;
class IAudioProvider;
class TrackDownloader;
class LyricsFetcher;
class CommandDispatcher;
class ConsoleRenderer;
class QNetworkAccessManager;
class QTimer;

enum class ConsoleState {
    COMMAND_MODE,
    WAITING_TOKEN_URL,
    SELECT_SOURCE,
    SELECT_PLAYLIST,
    SELECT_PLAYLIST_TO_PLAY,
    CREATE_PLAYLIST_NAME
};

class ConsoleController : public QObject {
    Q_OBJECT
public:
    ConsoleController(
        IAudioEngine& audio,
        PlaylistManager& playlist,
        OAuthManager& authManager,
        DatabaseManager& dbManager,
        TrackDownloader& downloader,
        LyricsFetcher& lyricsFetcher,
        QNetworkAccessManager* networkManager = nullptr,
        QObject* parent = nullptr);
    ~ConsoleController();

    void Start();
    void Stop();
    void SetState(ConsoleState state);
    ConsoleState GetState() const { return m_currentState; }

    void SetCurrentProvider(IAudioProvider* provider);
    void SetStatusMessage(const std::string& msg);

    std::function<void(bool)> OnGaplessModeChanged;

    signals:
        void QuitRequested();
    void OfflineModeRequested();
    void SourceChanged(const std::string& sourceName);
    void LogoutRequested(const std::string& service);

private slots:
    void OnUiTick();

private:
    void InputLoop(std::shared_ptr<std::atomic<bool>> isAlive);
    void ProcessInput(const std::string& rawInput);

    IAudioEngine& m_audio;
    PlaylistManager& m_playlist;
    OAuthManager& m_authManager;
    DatabaseManager& m_dbManager;
    TrackDownloader& m_downloader;
    LyricsFetcher& m_lyricsFetcher;
    IAudioProvider* m_currentProvider = nullptr;

    std::unique_ptr<CommandDispatcher> m_dispatcher;
    std::unique_ptr<ConsoleRenderer> m_renderer;

    std::atomic<ConsoleState> m_currentState;
    std::atomic<bool> m_isRunning;

    QTimer* m_uiTimer;
    std::shared_ptr<std::atomic<bool>> m_inputAlive;
    std::thread m_inputThread;

    Track m_pendingTrackToAdd;
    std::vector<PlaylistInfo> m_cachedPlaylists;
};