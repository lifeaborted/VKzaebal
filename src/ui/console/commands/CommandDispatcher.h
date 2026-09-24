#pragma once
#include <string>
#include <map>
#include <functional>
#include <memory>
#include "models/Track.h"

class IAudioEngine;
class PlaylistManager;
class DatabaseManager;
class TrackDownloader;
class LyricsFetcher;
class IAudioProvider;
class IDialogService;
class IAudioCaptureService;
class QNetworkAccessManager;
class SourceRouter;

// --- 1. Контекст команд ---
struct CommandContext {
    IAudioEngine& audio;
    PlaylistManager& playlist;
    DatabaseManager& dbManager;
    TrackDownloader& downloader;
    LyricsFetcher& lyricsFetcher;
    IAudioProvider* currentProvider;

    IDialogService& dialogService;
    IAudioCaptureService& audioCapture;
    QNetworkAccessManager* networkManager;

    std::function<void(const std::string&)> print;
    std::function<void(const std::string&)> onSourceChange;
    std::function<void(bool)> onGaplessMode;
    std::function<void()> onVisualizerToggle;
    std::function<void()> onQuit;
    std::function<void(const std::string&)> onLogout;
    std::function<void()> onReloadUi;
    std::function<void(const Track&)> onSelectPlaylist;
    std::function<void()> onSelectPlaylistToPlay;

    SourceRouter* router = nullptr;
};

// --- 2. Абстракция паттерна Command ---
class IConsoleCommand {
public:
    virtual ~IConsoleCommand() = default;
    virtual void Execute(const std::string& arg, CommandContext& ctx) = 0;
};

// --- 3. Диспетчер команд ---
class CommandDispatcher {
public:
    CommandDispatcher(IAudioEngine& audio, PlaylistManager& playlist,
                      DatabaseManager& dbManager, TrackDownloader& downloader,
                      LyricsFetcher& lyricsFetcher,
                      IDialogService* dialogService = nullptr,
                      IAudioCaptureService* audioCapture = nullptr,
                      QNetworkAccessManager* networkManager = nullptr);
    ~CommandDispatcher();

    void SetCurrentProvider(IAudioProvider* provider);
    void SetSourceRouter(SourceRouter* router);
    void SetPrintCallback(std::function<void(const std::string&)> printCb);

    std::function<void(const std::string&)> OnSourceChangeRequested;
    std::function<void(bool)> OnGaplessModeChanged;
    std::function<void()> OnVisualizerToggled;
    std::function<void()> OnQuitRequested;
    std::function<void(const std::string&)> OnLogoutRequested;
    std::function<void()> OnReloadUiRequested;
    std::function<void(const Track&)> OnSelectPlaylistRequested;
    std::function<void()> OnSelectPlaylistToPlayRequested;

    void Dispatch(const std::string& input);

private:
    void RegisterCommands();
    void Print(const std::string& msg);

    IAudioEngine& m_audio;
    PlaylistManager& m_playlist;
    DatabaseManager& m_dbManager;
    TrackDownloader& m_downloader;
    LyricsFetcher& m_lyricsFetcher;
    IAudioProvider* m_currentProvider = nullptr;
    SourceRouter* m_router = nullptr;

    std::unique_ptr<IDialogService> m_ownedDialogService;
    std::unique_ptr<IAudioCaptureService> m_ownedAudioCapture;
    IDialogService& m_dialogService;
    IAudioCaptureService& m_audioCapture;
    QNetworkAccessManager* m_networkManager = nullptr;

    std::function<void(const std::string&)> m_printCb;

    std::map<std::string, std::unique_ptr<IConsoleCommand>> m_commands;
};