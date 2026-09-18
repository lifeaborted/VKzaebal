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

// --- 1. Контекст команд ---
struct CommandContext {
    IAudioEngine& audio;
    PlaylistManager& playlist;
    DatabaseManager& dbManager;
    TrackDownloader& downloader;
    LyricsFetcher& lyricsFetcher;
    IAudioProvider* currentProvider;

    std::function<void(const std::string&)> print;
    std::function<void(const std::string&)> onSourceChange;
    std::function<void(bool)> onGaplessMode;
    std::function<void()> onVisualizerToggle;
    std::function<void()> onQuit;
    std::function<void(const std::string&)> onLogout;
    std::function<void()> onReloadUi;
    std::function<void(const Track&)> onSelectPlaylist;
    std::function<void()> onSelectPlaylistToPlay;
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
                      LyricsFetcher& lyricsFetcher);
    ~CommandDispatcher();

    void SetCurrentProvider(IAudioProvider* provider);
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

    std::function<void(const std::string&)> m_printCb;

    std::map<std::string, std::unique_ptr<IConsoleCommand>> m_commands;
};