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
    bool IsShuffle() const { return m_isShuffle; }

    Track GetCurrentTrack() const;
    int GetCurrentAbsoluteIndex() const { return m_tracks.empty() ? -1 : m_playQueue[m_queueIndex]; }
    Track PeekNextTrack() const;

    void Next();
    void Previous();
    void JumpTo(int index);

    void ToggleShuffle();
    void SetShuffle(bool enable);
    void ToggleRepeat();

    std::function<void(const Track&)> OnTrackRequested;
    std::vector<Track> GetQueueTracks() const;
    std::vector<Track> GetAllTracks() const { return m_tracks; }

private:
    std::vector<Track> m_tracks;
    std::vector<int> m_playQueue; // Хранит индексы треков
    int m_queueIndex = 0;         // Текущая позиция в очереди

    bool m_isShuffle = false;
    RepeatMode m_repeatMode = RepeatMode::All;

    void RebuildQueue(bool keepCurrentTrack);
};