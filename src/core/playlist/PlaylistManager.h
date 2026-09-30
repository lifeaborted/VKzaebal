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
    void PlayTrackNow(const Track& track);
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
    void RemoveTrack(int index);
    int FindTrackIndexById(const std::string& trackId) const;
    void MoveTrack(int fromIndex, int toIndex);

    void ToggleShuffle();
    void SetShuffle(bool enable);
    void ToggleRepeat();
    int GetRepeatMode() const;
    void SetRepeatMode(int mode);
    void RestoreShuffleQueue(const std::vector<std::string>& shuffledIds);

    void Clear();
    void ClearKeepActive();
    void SetActiveTrack(const Track& track);
    void AlignWithActiveTrack();

    std::function<void(const Track&)> OnTrackRequested;
    size_t GetQueueSize() const;
    int GetCurrentQueueIndex() const;
    std::vector<Track> GetQueueSlice(size_t offset, size_t count) const;
    std::vector<Track> GetQueueTracks() const;
    std::vector<Track> GetAllTracks() const;

private:
    std::vector<Track> m_tracks;
    std::vector<int> m_playQueue;
    int m_queueIndex = 0;
    Track m_activeTrack;

    bool m_isShuffle = false;
    RepeatMode m_repeatMode = RepeatMode::All;

    mutable std::mutex m_mutex; // Защита от одновременного доступа потоков

    void RebuildQueue(bool keepCurrentTrack);
};