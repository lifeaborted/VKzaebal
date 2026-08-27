#pragma once

#include <memory>
#include <QObject>
#include <QString>
#include <atomic>
#include <functional>
#include <thread>

// Forward declarations
class IAudioEngine;
class PlaylistManager;
class OAuthManager;
class DatabaseManager;
class IAudioProvider;
class TrackDownloader;
class LyricsFetcher;
class CommandDispatcher;
class ConsoleRenderer;
class QTimer;

enum class ConsoleState {
    COMMAND_MODE,
    WAITING_TOKEN_URL,
    SELECT_SOURCE
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
        QObject* parent = nullptr);
    ~ConsoleController();

    void Start();
    void Stop();
    void SetState(ConsoleState state);
    ConsoleState GetState() const { return m_currentState; }

    void SetCurrentProvider(IAudioProvider* provider);

    std::function<void(bool)> OnGaplessModeChanged;

    signals:
        void QuitRequested();
    void OfflineModeRequested();
    void SourceChanged(const std::string& sourceName);

private slots:
    void OnUiTick();

private:
    void InputLoop();

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
    std::thread m_inputThread;
};