#include "PlaylistManager.h"
#include "utils/logger/logger.h"

PlaylistManager::PlaylistManager() : m_currentIndex(-1) {}

void PlaylistManager::AddTrack(const std::string& url) {
    m_tracks.push_back(url);
    if (m_currentIndex == -1) {
        m_currentIndex = 0; // Инициализируем индекс первым добавленным треком
    }
}

void PlaylistManager::Next() {
    if (m_tracks.empty()) return;

    m_currentIndex++;
    if (m_currentIndex >= static_cast<int>(m_tracks.size())) {
        m_currentIndex = 0; // Зацикливаем плейлист
        Logger::Log(LogLevel::INFO, "PlaylistManager: Reached end of playlist, looping back to start.");
    }

    if (OnTrackRequested) {
        OnTrackRequested(GetCurrentTrack());
    }
}

void PlaylistManager::Previous() {
    if (m_tracks.empty()) return;

    m_currentIndex--;
    if (m_currentIndex < 0) {
        m_currentIndex = static_cast<int>(m_tracks.size()) - 1; // Переход на последний трек
    }

    if (OnTrackRequested) {
        OnTrackRequested(GetCurrentTrack());
    }
}

std::string PlaylistManager::GetCurrentTrack() const {
    if (m_currentIndex >= 0 && m_currentIndex < static_cast<int>(m_tracks.size())) {
        return m_tracks[m_currentIndex];
    }
    return "";
}

bool PlaylistManager::HasTracks() const {
    return !m_tracks.empty();
}