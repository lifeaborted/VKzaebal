#pragma once
#include <vector>
#include <functional>
#include <mutex>
#include "../../models/Track.h"

enum class RepeatMode { None, All, One };

class PlaylistManager {
public:
    PlaylistManager() = default;

    void AddTrack(const Track& track);
    bool HasTracks() const;
    bool IsShuffle() const;

    Track GetCurrentTrack() const;
    int GetCurrentAbsoluteIndex() const;
    Track PeekNextTrack() const;

    void Next();
    void Previous();
    void JumpTo(int index);
    void JumpToQueueIndex(int queueIndex);
    
    void InsertTrack(int position, const Track& track);

    void ToggleShuffle();
    void SetShuffle(bool enable);
    void ToggleRepeat();
    int GetRepeatMode() const;
    void SetRepeatMode(int mode);
    void RestoreShuffleQueue(const std::vector<std::string>& shuffledIds);

    void Clear();

    std::function<void(const Track&)> OnTrackRequested;
    std::vector<Track> GetQueueTracks() const;
    std::vector<Track> GetAllTracks() const;

private:
    std::vector<Track> m_tracks;
    std::vector<int> m_playQueue;
    int m_queueIndex = 0;

    bool m_isShuffle = false;
    RepeatMode m_repeatMode = RepeatMode::All;

    mutable std::mutex m_mutex; // Защита от одновременного доступа потоков

    void RebuildQueue(bool keepCurrentTrack);
};