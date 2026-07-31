#pragma once
#include <vector>
#include <string>
#include <functional>

class PlaylistManager {
public:
    PlaylistManager();
    ~PlaylistManager() = default;

    void AddTrack(const std::string& url);
    void Next();
    void Previous();

    std::string GetCurrentTrack() const;
    bool HasTracks() const;

    // Коллбэк для оповещения о загрузки нового трека
    std::function<void(const std::string&)> OnTrackRequested;

private:
    std::vector<std::string> m_tracks;
    int m_currentIndex;
};