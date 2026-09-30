#include "PlaylistManager.h"
#include "utils/logger/Logger.h"
#include <random>
#include <algorithm>

void PlaylistManager::AddTrack(const Track& track) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_tracks.push_back(track);
    m_playQueue.push_back(m_tracks.size() - 1);
}

void PlaylistManager::PlayTrackNow(const Track& track) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_activeTrack = track;
    int foundIdx = -1;
    for (size_t i = 0; i < m_tracks.size(); ++i) {
        if (m_tracks[i].id == track.id) {
            foundIdx = static_cast<int>(i);
            break;
        }
    }
    if (foundIdx == -1) {
        m_tracks.push_back(track);
        foundIdx = static_cast<int>(m_tracks.size() - 1);
        if (m_playQueue.empty()) {
            m_playQueue.push_back(foundIdx);
            m_queueIndex = 0;
        } else {
            m_playQueue.insert(m_playQueue.begin() + m_queueIndex + 1, foundIdx);
            m_queueIndex++;
        }
    } else {
        auto it = std::find(m_playQueue.begin(), m_playQueue.end(), foundIdx);
        if (it != m_playQueue.end()) {
            m_queueIndex = std::distance(m_playQueue.begin(), it);
        } else {
            m_playQueue.insert(m_playQueue.begin() + m_queueIndex + 1, foundIdx);
            m_queueIndex++;
        }
    }
}

bool PlaylistManager::HasTracks() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return !m_tracks.empty();
}

bool PlaylistManager::IsShuffle() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_isShuffle;
}

Track PlaylistManager::GetCurrentTrack() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_activeTrack.id.empty()) {
        return m_activeTrack;
    }
    if (m_tracks.empty() || m_queueIndex < 0 || m_queueIndex >= m_playQueue.size()) {
        return Track();
    }
    return m_tracks[m_playQueue[m_queueIndex]];
}

int PlaylistManager::GetCurrentAbsoluteIndex() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_tracks.empty() ? -1 : m_playQueue[m_queueIndex];
}

Track PlaylistManager::PeekNextTrack() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_tracks.empty() || m_playQueue.empty()) return Track();

    if (m_repeatMode == RepeatMode::One) return m_tracks[m_playQueue[m_queueIndex]];

    int previewIndex = m_queueIndex + 1;

    if (previewIndex >= m_playQueue.size()) {
        if (m_repeatMode == RepeatMode::All) {
            previewIndex = 0;
        } else {
            return Track();
        }
    }
    return m_tracks[m_playQueue[previewIndex]];
}

void PlaylistManager::RebuildQueue(bool keepCurrentTrack) {
    // Внимание: Этот метод вызывается ИЗ ДРУГИХ методов, которые УЖЕ залочили мьютекс.
    if (m_tracks.empty()) return;

    int currentTrackIndex = m_playQueue.empty() ? 0 : m_playQueue[m_queueIndex];

    m_playQueue.clear();
    for (int i = 0; i < m_tracks.size(); ++i) {
        m_playQueue.push_back(i);
    }

    if (m_isShuffle) {
        std::random_device rd;
        std::mt19937 g(rd());
        std::shuffle(m_playQueue.begin(), m_playQueue.end(), g);

        if (keepCurrentTrack) {
            auto it = std::find(m_playQueue.begin(), m_playQueue.end(), currentTrackIndex);
            if (it != m_playQueue.end()) {
                std::iter_swap(m_playQueue.begin(), it);
            }
        }
        m_queueIndex = 0;
    } else {
        if (keepCurrentTrack) {
            m_queueIndex = currentTrackIndex;
        } else {
            m_queueIndex = 0;
        }
    }
}

void PlaylistManager::Next() {
    Track nextTrack;
    bool shouldPlay = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_tracks.empty()) return;

        if (m_repeatMode == RepeatMode::One) {
            // Остаемся на том же треке
        } else {
            bool activeIsExternal = false;
            if (!m_activeTrack.id.empty()) {
                activeIsExternal = true;
                for (const auto& t : m_tracks) {
                    if (t.id == m_activeTrack.id) {
                        activeIsExternal = false;
                        break;
                    }
                }
            }

            if (!activeIsExternal) {
                m_queueIndex++;
                if (m_queueIndex >= m_playQueue.size()) {
                    if (m_repeatMode == RepeatMode::All) {
                        if (m_isShuffle) RebuildQueue(false);
                        else m_queueIndex = 0;
                    } else {
                        m_queueIndex--;
                        Logger::Log(LogLevel::INFO, "Playlist reached the end.");
                        return;
                    }
                }
            }
        }
        nextTrack = m_tracks[m_playQueue[m_queueIndex]];
        m_activeTrack = nextTrack;
        shouldPlay = true;
    } // Мьютекс разблокирован здесь

    // Дергаем коллбек ВНЕ блокировки мьютекса
    if (shouldPlay && OnTrackRequested) {
        OnTrackRequested(nextTrack);
    }
}

void PlaylistManager::Previous() {
    Track prevTrack;
    bool shouldPlay = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_tracks.empty()) return;

        if (m_repeatMode != RepeatMode::One) {
            bool activeIsExternal = false;
            if (!m_activeTrack.id.empty()) {
                activeIsExternal = true;
                for (const auto& t : m_tracks) {
                    if (t.id == m_activeTrack.id) {
                        activeIsExternal = false;
                        break;
                    }
                }
            }

            if (!activeIsExternal) {
                m_queueIndex--;
                if (m_queueIndex < 0) {
                    m_queueIndex = m_repeatMode == RepeatMode::All ? m_playQueue.size() - 1 : 0;
                }
            }
        }
        prevTrack = m_tracks[m_playQueue[m_queueIndex]];
        m_activeTrack = prevTrack;
        shouldPlay = true;
    }

    if (shouldPlay && OnTrackRequested) {
        OnTrackRequested(prevTrack);
    }
}

void PlaylistManager::JumpTo(int index) {
    Track targetTrack;
    bool shouldPlay = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (index < 0 || index >= m_tracks.size()) {
            Logger::Log(LogLevel::WARNING, "Invalid track index!");
            return;
        }

        auto it = std::find(m_playQueue.begin(), m_playQueue.end(), index);
        if (it != m_playQueue.end()) {
            m_queueIndex = std::distance(m_playQueue.begin(), it);
            targetTrack = m_tracks[m_playQueue[m_queueIndex]];
            m_activeTrack = targetTrack;
            shouldPlay = true;
        }
    }

    if (shouldPlay && OnTrackRequested) {
        OnTrackRequested(targetTrack);
    }
}

void PlaylistManager::JumpToQueueIndex(int index) {
    Track targetTrack;
    bool shouldPlay = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (index < 0 || index >= m_playQueue.size()) {
            Logger::Log(LogLevel::WARNING, "Invalid queue index!");
            return;
        }

        m_queueIndex = index;
        targetTrack = m_tracks[m_playQueue[m_queueIndex]];
        m_activeTrack = targetTrack;
        shouldPlay = true;
    }

    if (shouldPlay && OnTrackRequested) {
        OnTrackRequested(targetTrack);
    }
}

void PlaylistManager::ToggleShuffle() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_isShuffle = !m_isShuffle;
    RebuildQueue(!m_isShuffle);
    Logger::Log(LogLevel::INFO, std::string("Shuffle is now ") + (m_isShuffle ? "ON" : "OFF"));
}

void PlaylistManager::SetShuffle(bool enable) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!enable && !m_isShuffle) return;

    m_isShuffle = enable;
    RebuildQueue(!enable);
    Logger::Log(LogLevel::INFO, std::string("Shuffle is now ") + (m_isShuffle ? "ON (Reshuffled)" : "OFF"));
}

void PlaylistManager::ToggleRepeat() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_repeatMode == RepeatMode::All) {
        m_repeatMode = RepeatMode::One;
        Logger::Log(LogLevel::INFO, "Repeat Mode: ONE TRACK");
    } else if (m_repeatMode == RepeatMode::One) {
        m_repeatMode = RepeatMode::None;
        Logger::Log(LogLevel::INFO, "Repeat Mode: NONE");
    } else {
        m_repeatMode = RepeatMode::All;
        Logger::Log(LogLevel::INFO, "Repeat Mode: ALL TRACKS");
    }
}

size_t PlaylistManager::GetQueueSize() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_playQueue.size();
}

int PlaylistManager::GetCurrentQueueIndex() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_queueIndex;
}

std::vector<Track> PlaylistManager::GetQueueSlice(size_t offset, size_t count) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<Track> slice;
    if (offset >= m_playQueue.size()) return slice;
    size_t end = std::min(m_playQueue.size(), offset + count);
    slice.reserve(end - offset);
    for (size_t i = offset; i < end; ++i) {
        int trackIdx = m_playQueue[i];
        if (trackIdx >= 0 && trackIdx < static_cast<int>(m_tracks.size())) {
            slice.push_back(m_tracks[trackIdx]);
        }
    }
    return slice;
}

std::vector<Track> PlaylistManager::GetQueueTracks() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<Track> queue;
    for (int index : m_playQueue) {
        queue.push_back(m_tracks[index]);
    }
    return queue;
}

std::vector<Track> PlaylistManager::GetAllTracks() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_tracks;
}

void PlaylistManager::InsertTrack(int position, const Track& track) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    bool wasEmpty = m_playQueue.empty();

    if (position < 0) position = 0;
    if (position > m_tracks.size()) position = m_tracks.size();

    m_tracks.insert(m_tracks.begin() + position, track);

    for (int& idx : m_playQueue) {
        if (idx >= position) {
            idx++;
        }
    }

    if (m_isShuffle) {
        m_playQueue.push_back(position);
    } else {
        m_playQueue.insert(m_playQueue.begin() + position, position);

        if (!wasEmpty && position <= m_queueIndex) {
            m_queueIndex++;
        }
    }
}

void PlaylistManager::Clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_tracks.clear();
    m_playQueue.clear();
    m_queueIndex = 0;
    m_activeTrack = Track();
}

void PlaylistManager::ClearKeepActive() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_activeTrack.id.empty() && !m_tracks.empty() && m_queueIndex >= 0 && m_queueIndex < static_cast<int>(m_playQueue.size())) {
        m_activeTrack = m_tracks[m_playQueue[m_queueIndex]];
    }
    m_tracks.clear();
    m_playQueue.clear();
    m_queueIndex = 0;
}

void PlaylistManager::SetActiveTrack(const Track& track) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_activeTrack = track;
}

void PlaylistManager::AlignWithActiveTrack() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_activeTrack.id.empty() || m_tracks.empty()) return;

    for (size_t i = 0; i < m_tracks.size(); ++i) {
        if (m_tracks[i].id == m_activeTrack.id) {
            auto it = std::find(m_playQueue.begin(), m_playQueue.end(), static_cast<int>(i));
            if (it != m_playQueue.end()) {
                m_queueIndex = static_cast<int>(std::distance(m_playQueue.begin(), it));
            }
            break;
        }
    }
}

void PlaylistManager::RemoveTrack(int index) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (index < 0 || index >= static_cast<int>(m_tracks.size())) return;

    m_tracks.erase(m_tracks.begin() + index);
    if (m_tracks.empty()) {
        m_playQueue.clear();
        m_queueIndex = 0;
    } else {
        RebuildQueue(true);
        if (m_queueIndex >= static_cast<int>(m_tracks.size())) {
            m_queueIndex = static_cast<int>(m_tracks.size()) - 1;
        }
    }
}

int PlaylistManager::FindTrackIndexById(const std::string& trackId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (size_t i = 0; i < m_tracks.size(); ++i) {
        if (m_tracks[i].id == trackId) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void PlaylistManager::MoveTrack(int fromIndex, int toIndex) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (fromIndex < 0 || fromIndex >= static_cast<int>(m_tracks.size())) return;
    if (toIndex < 0 || toIndex >= static_cast<int>(m_tracks.size())) return;
    if (fromIndex == toIndex) return;

    std::string currentPlayingTrackId;
    if (m_queueIndex >= 0 && m_queueIndex < static_cast<int>(m_playQueue.size())) {
        int trackIdx = m_playQueue[m_queueIndex];
        if (trackIdx >= 0 && trackIdx < static_cast<int>(m_tracks.size())) {
            currentPlayingTrackId = m_tracks[trackIdx].id;
        }
    }

    Track t = std::move(m_tracks[fromIndex]);
    m_tracks.erase(m_tracks.begin() + fromIndex);
    m_tracks.insert(m_tracks.begin() + toIndex, std::move(t));

    if (!m_isShuffle) {
        m_playQueue.clear();
        m_playQueue.reserve(m_tracks.size());
        for (int i = 0; i < static_cast<int>(m_tracks.size()); ++i) {
            m_playQueue.push_back(i);
        }
    } else {
        for (int& idx : m_playQueue) {
            if (idx == fromIndex) {
                idx = toIndex;
            } else if (fromIndex < toIndex) {
                if (idx > fromIndex && idx <= toIndex) {
                    idx--;
                }
            } else {
                if (idx >= toIndex && idx < fromIndex) {
                    idx++;
                }
            }
        }
    }

    if (!currentPlayingTrackId.empty()) {
        for (size_t i = 0; i < m_playQueue.size(); ++i) {
            int trackIdx = m_playQueue[i];
            if (trackIdx >= 0 && trackIdx < static_cast<int>(m_tracks.size()) &&
                m_tracks[trackIdx].id == currentPlayingTrackId) {
                m_queueIndex = static_cast<int>(i);
                break;
            }
        }
    }
}

void PlaylistManager::RestoreShuffleQueue(const std::vector<std::string>& shuffledIds) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_isShuffle = true;
    m_playQueue.clear();

    for (const std::string& id : shuffledIds) {
        auto it = std::find_if(m_tracks.begin(), m_tracks.end(), [&](const Track& t){ return t.id == id; });
        if (it != m_tracks.end()) {
            m_playQueue.push_back(std::distance(m_tracks.begin(), it));
        }
    }

    if (m_playQueue.size() != m_tracks.size()) {
        m_playQueue.clear();
        for (int i = 0; i < m_tracks.size(); ++i) m_playQueue.push_back(i);

        std::random_device rd;
        std::mt19937 g(rd());
        std::shuffle(m_playQueue.begin(), m_playQueue.end(), g);
        Logger::Log(LogLevel::WARNING, "PlaylistManager: Shuffle queue mismatched, created a new one.");
    } else {
        Logger::Log(LogLevel::INFO, "PlaylistManager: Shuffle queue successfully restored from DB.");
    }
}

int PlaylistManager::GetRepeatMode() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return static_cast<int>(m_repeatMode);
}

void PlaylistManager::SetRepeatMode(int mode) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (mode >= 0 && mode <= 2) {
        m_repeatMode = static_cast<RepeatMode>(mode);
    }
}