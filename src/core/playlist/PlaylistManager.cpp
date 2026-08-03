#include "PlaylistManager.h"
#include "utils/logger/logger.h"
#include <random>
#include <algorithm>

void PlaylistManager::AddTrack(const Track& track) {
    m_tracks.push_back(track);
    m_playQueue.push_back(m_tracks.size() - 1);
}

bool PlaylistManager::HasTracks() const {
    return !m_tracks.empty();
}

Track PlaylistManager::GetCurrentTrack() const {
    if (m_tracks.empty() || m_queueIndex < 0 || m_queueIndex >= m_playQueue.size()) {
        return Track(); // Возвращаем пустой трек
    }
    return m_tracks[m_playQueue[m_queueIndex]];
}

Track PlaylistManager::PeekNextTrack() const {
    if (m_tracks.empty() || m_playQueue.empty()) return Track();

    if (m_repeatMode == RepeatMode::One) return GetCurrentTrack();

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
    if (m_tracks.empty()) return;

    // Запоминаем абсолютный индекс текущего трека в массиве m_tracks
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
            // Перемещаем текущий играющий трек на нулевую позицию в новой очереди
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
    if (m_tracks.empty()) return;

    if (m_repeatMode == RepeatMode::One) {
        // Остаемся на том же индексе
    } else {
        m_queueIndex++;
        if (m_queueIndex >= m_playQueue.size()) {
            if (m_repeatMode == RepeatMode::All) {
                if (m_isShuffle) RebuildQueue(false); // Рероллим шафл по кругу
                else m_queueIndex = 0;
            } else {
                m_queueIndex--; // Стопаримся в конце
                Logger::Log(LogLevel::INFO, "Playlist reached the end.");
                return;
            }
        }
    }

    if (OnTrackRequested) {
        OnTrackRequested(GetCurrentTrack());
    }
}

void PlaylistManager::Previous() {
    if (m_tracks.empty()) return;

    if (m_repeatMode != RepeatMode::One) {
        m_queueIndex--;
        if (m_queueIndex < 0) {
            m_queueIndex = m_repeatMode == RepeatMode::All ? m_playQueue.size() - 1 : 0;
        }
    }

    if (OnTrackRequested) {
        OnTrackRequested(GetCurrentTrack());
    }
}

void PlaylistManager::JumpTo(int index) {
    if (index < 0 || index >= m_tracks.size()) {
        Logger::Log(LogLevel::WARNING, "Invalid track index!");
        return;
    }

    // Находим этот трек в текущей очереди
    auto it = std::find(m_playQueue.begin(), m_playQueue.end(), index);
    if (it != m_playQueue.end()) {
        m_queueIndex = std::distance(m_playQueue.begin(), it);
        if (OnTrackRequested) {
            OnTrackRequested(GetCurrentTrack());
        }
    }
}

void PlaylistManager::ToggleShuffle() {
    m_isShuffle = !m_isShuffle;
    RebuildQueue(true); // Перестраиваем очередь, но оставляем текущий трек
    Logger::Log(LogLevel::INFO, std::string("Shuffle is now ") + (m_isShuffle ? "ON" : "OFF"));
}

void PlaylistManager::ToggleRepeat() {
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
    std::vector<Track> queue;
    for (int index : m_playQueue) {
        queue.push_back(m_tracks[index]);
    }
    return queue;
}