#pragma once
#include <vector>
#include <string>
#include <functional>
#include "core/vk/Track.h"

class PlaylistManager {
public:
    PlaylistManager();
    ~PlaylistManager() = default;

    void AddTrack(const Track& track); // Принимаем весь объект Track
    void Next();
    void Previous();

    bool HasTracks() const;

    const Track& GetCurrentTrack() const; // Возвращаем ссылку на объект Track
    
    std::function<void(const Track& track)> OnTrackRequested;     // Коллбэк для оповещения о загрузке нового трека

private:
    std::vector<Track> m_tracks;
    int m_currentIndex = 0;
};