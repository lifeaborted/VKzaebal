#pragma once
#include <vector>
#include <functional>
#include "core/vk/Track.h"

enum class RepeatMode { None, All, One };

class PlaylistManager {
public:
    PlaylistManager() = default;

    void AddTrack(const Track& track);
    bool HasTracks() const;
    Track GetCurrentTrack() const;
    Track GetNextTrackPreview() const;

    void Next();
    void Previous();
    void JumpTo(int index); // index от 0

    void ToggleShuffle();
    void ToggleRepeat();

    std::function<void(const Track&)> OnTrackRequested;

private:
    std::vector<Track> m_tracks;
    std::vector<int> m_playQueue; // Хранит индексы треков
    int m_queueIndex = 0;         // Текущая позиция в очереди

    bool m_isShuffle = false;
    RepeatMode m_repeatMode = RepeatMode::All;

    void RebuildQueue(bool keepCurrentTrack);
};