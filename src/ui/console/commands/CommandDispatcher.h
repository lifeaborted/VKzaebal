#pragma once
#include <string>
#include <map>
#include <functional>

class IAudioEngine;
class PlaylistManager;
class DatabaseManager;
class TrackDownloader;
class LyricsFetcher;
class IAudioProvider;

class CommandDispatcher {
public:
    CommandDispatcher(IAudioEngine& audio, PlaylistManager& playlist,
                      DatabaseManager& dbManager, TrackDownloader& downloader,
                      LyricsFetcher& lyricsFetcher);

    void SetCurrentProvider(IAudioProvider* provider);
    void SetPrintCallback(std::function<void(const std::string&)> printCb);

    std::function<void(const std::string&)> OnSourceChangeRequested;
    std::function<void(bool)> OnGaplessModeChanged;
    std::function<void()> OnVisualizerToggled;
    std::function<void()> OnQuitRequested;
    std::function<void(const std::string&)> OnLogoutRequested;

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

    std::map<std::string, std::function<void(const std::string&)>> m_commands;
};