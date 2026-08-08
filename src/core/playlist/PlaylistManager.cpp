#include "PlaylistManager.h"
#include "utils/logger/Logger.h"
#include <random>
#include <algorithm>

void PlaylistManager::AddTrack(const Track& track) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_tracks.push_back(track);
    m_playQueue.push_back(m_tracks.size() - 1);
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
        nextTrack = m_tracks[m_playQueue[m_queueIndex]];
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
            m_queueIndex--;
            if (m_queueIndex < 0) {
                m_queueIndex = m_repeatMode == RepeatMode::All ? m_playQueue.size() - 1 : 0;
            }
        }
        prevTrack = m_tracks[m_playQueue[m_queueIndex]];
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