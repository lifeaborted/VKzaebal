#pragma once
#include <QObject>
#include <QString>
#include <atomic>
#include <thread>
#include <functional>
#include "core/vk/api/Client.h"
#include "services/downloader/TrackDownloader.h"
#include "core/lyrics/LyricsFetcher.h"

class IAudioEngine;
class PlaylistManager;
class Manager;
class DatabaseManager;

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
        Manager& authManager,
        DatabaseManager& dbManager,
        Client& vkClient,
        TrackDownloader& downloader,
        LyricsFetcher& lyricsFetcher,
        QObject* parent = nullptr
        );
    ~ConsoleController();

    void Start();
    void Stop();
    void SetState(ConsoleState state);
    ConsoleState GetState() const { return m_currentState; }

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
    Manager& m_authManager;
    DatabaseManager& m_dbManager;
    Client& m_vkClient;
    TrackDownloader& m_downloader;
    LyricsFetcher& m_lyricsFetcher;

    std::atomic<ConsoleState> m_currentState;
    std::atomic<bool> m_isRunning;
    std::thread m_inputThread;
    std::thread m_uiThread;
};