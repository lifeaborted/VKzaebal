#pragma once

#include "ui/IUiController.h"
#include <memory>
#include <string>
#include <vector>
#include <atomic>
#include <thread>
#include "models/Track.h"

class IAudioEngine;
class PlaylistManager;
class DatabaseManager;
class TrackDownloader;
class LyricsFetcher;
class IAudioProvider;
class QNetworkAccessManager;

// ==============================================================================
// LightConsoleController
// ------------------------------------------------------------------------------
// Заготовка минимального легковесного CLI интерфейса для консолей.
// Идеально подходит для работы через SSH, в фоновых сервисах и на слабых системах:
// - Никакой тяжелой покадровой перерисовки всего терминала
// - Минимальный расход CPU и RAM
// - Простой построчный ввод/вывод команд
// ==============================================================================
class LightConsoleController : public IUiController {
    Q_OBJECT
public:
    LightConsoleController(
        IAudioEngine& audio,
        PlaylistManager& playlist,
        DatabaseManager& dbManager,
        TrackDownloader& downloader,
        LyricsFetcher& lyricsFetcher,
        QNetworkAccessManager* networkManager = nullptr,
        QObject* parent = nullptr);
    ~LightConsoleController() override;

    void Start() override;
    void Stop() override;

    void SetStatusMessage(const std::string& msg) override;
    void SetCurrentProvider(IAudioProvider* provider) override;
    void SetWaitingAuth(bool waiting) override;
    void OnTrackChanged(const Track& track) override;
    void OnAudioFetched(const std::vector<Track>& tracks) override;
    void OnFinishedFetching() override;

private:
    void InputLoop();
    void ProcessCommand(const std::string& cmd);
    void PrintPrompt();
    void PrintHelp();
    void PrintNowPlaying();
    void PrintQueue();

    IAudioEngine& m_audio;
    PlaylistManager& m_playlist;
    DatabaseManager& m_dbManager;
    TrackDownloader& m_downloader;
    LyricsFetcher& m_lyricsFetcher;
    IAudioProvider* m_currentProvider = nullptr;

    std::atomic<bool> m_isRunning{false};
    std::atomic<bool> m_isWaitingAuth{false};
    std::thread m_inputThread;
};
