#pragma once
#include <QObject>
#include <QString>
#include <atomic>
#include <thread>
#include <functional>

class IAudioEngine;
class PlaylistManager;
class OAuthManager;
class DatabaseManager;
class IAudioProvider;
class TrackDownloader;
class LyricsFetcher;

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
        LyricsFetcher& lyricsFetcher);
    ~ConsoleController();

    void Start();
    void Stop();
    void SetState(ConsoleState state);
    ConsoleState GetState() const { return m_currentState; }

    void SetCurrentProvider(IAudioProvider* provider); // Сеттер для переключения источника

    std::function<void(bool)> OnGaplessModeChanged;

    signals:
        void QuitRequested();
        void OfflineModeRequested();
        void SourceChanged(const std::string& sourceName);

private:
    void InputLoop();
    void UiLoop();

    IAudioEngine& m_audio;
    PlaylistManager& m_playlist;
    OAuthManager& m_authManager;
    DatabaseManager& m_dbManager;
    TrackDownloader& m_downloader;
    LyricsFetcher& m_lyricsFetcher;
    IAudioProvider* m_currentProvider = nullptr;
    bool m_showVisualizer = true;

    std::atomic<ConsoleState> m_currentState;
    std::atomic<bool> m_isRunning;
    std::thread m_inputThread;
    std::thread m_uiThread;
};