#include "PlaybackController.h"
#include "core/audio/IAudioEngine.h"
#include "core/playlist/PlaylistManager.h"
#include "core/api/IAudioProvider.h"
#include "services/network/NetworkStreamer.h"
#include "utils/logger/Logger.h"
#include "utils/path/PathManager.h"

#include <QFile>
#include <QTimer>
#include <iostream>
#include <QCoreApplication>

PlaybackController::PlaybackController(IAudioEngine& audio, PlaylistManager& playlist, NetworkStreamer& streamer, QObject* parent)
    : QObject(parent), m_audio(audio), m_playlist(playlist), m_streamer(streamer) {}

void PlaybackController::SetCurrentProvider(IAudioProvider* provider) {
    m_currentProvider = provider;
}

void PlaybackController::SetCrossfadeEnabled(bool enabled) {
    m_crossfadeEnabled = enabled;
}

void PlaybackController::SetSavedPosition(double pos) {
    m_savedPosition = pos;
}

void PlaybackController::ClearState() {
    m_streamer.StopDownload();
    m_audio.ClearBuffers(false, 0);
    m_audio.Pause();
    m_preloadedTrack = Track();
    m_cachedNextUrl = "";
}

void PlaybackController::HandleTrackFinished() {
    Logger::Log(LogLevel::INFO, "PlaybackController: Auto-switching to next track...");
    m_playlist.Next();
}

void PlaybackController::HandleTrackNearEnd() {
    Track nextTrack = m_playlist.PeekNextTrack();
    if (nextTrack.id.empty()) return;

    if (!m_currentProvider) return;
    m_currentProvider->FetchTrackUrl(nextTrack.id, [this, nextTrack](const std::string& freshUrl, bool isNetworkError) {
        if (!isNetworkError && !freshUrl.empty()) {
            m_cachedNextUrl = freshUrl;
            m_preloadedTrack = nextTrack;
            Logger::Log(LogLevel::INFO, "PlaybackController: Next track URL pre-fetched successfully.");
        }
    });
}

void PlaybackController::AttemptPlay(const Track& track, int attempt) {
    int currentGen = (attempt == 1) ? ++m_playbackGeneration : m_playbackGeneration.load();

    QString localPath = PathManager::GetDownloadFilePath(track.GetSafeFilename(), "mp3");
    if (!QFile::exists(localPath)) {
        localPath = PathManager::GetDownloadFilePath(track.GetSafeFilename(), "aac");
    }
    bool isDownloaded = QFile::exists(localPath);

    if (attempt == 1 && !m_cachedNextUrl.empty() && m_preloadedTrack.id == track.id && !isDownloaded) {
        if (m_audio.PlayStream(m_cachedNextUrl, track.duration, m_crossfadeEnabled, track.GetSafeFilename())) {
            m_cachedNextUrl = "";
            m_skipCount = 0;
            if (m_savedPosition > 0.0) {
                m_audio.SetPositionSeconds(m_savedPosition);
                m_savedPosition = 0.0;
            }
            std::cout << "\r\033[2K\033[1A\r\033[2K\n> ";
            std::cout.flush();
            return;
        }
    } else if (attempt == 1) {
        std::cout << "\r\033[2K\033[1A\r\033[2K";
        std::cout << "[Загрузка] " << track.artist << " - " << track.title << "...\n\n> ";
        std::cout.flush();
    }

    if (isDownloaded) {
        m_skipCount = 0;
        if (m_audio.PlayStream("", track.duration, m_crossfadeEnabled, track.GetSafeFilename())) {
            if (m_savedPosition > 0.0) {
                m_audio.SetPositionSeconds(m_savedPosition);
                m_savedPosition = 0.0;
            }
            std::cout << "\r\033[2K\033[1A\r\033[2K\033[1A\r\033[2K\n> ";
            std::cout.flush();
        } else {
            m_playlist.Next();
        }
        return;
    }

    auto executePlay = [this, track, attempt, currentGen](const std::string& freshUrl, bool isNetworkError) {
        if (currentGen != m_playbackGeneration.load()) return;

        if (!isNetworkError && freshUrl.empty()) {
            m_skipCount++;
            if (m_skipCount >= 5) {
                Logger::Log(LogLevel::ERROR, "Too many unplayable tracks. Stopping loop.");
                m_skipCount = 0;
                std::cout << "\r\033[2K\033[1A\r\033[2K[Внимание] Ошибка сети/токена. Воспроизведение остановлено.\n\n> ";
                std::cout.flush();
                return;
            }

            Logger::Log(LogLevel::WARNING, "Track is restricted or token invalid. Skipping...");
            m_playlist.Next();
            return;
        }

        m_skipCount = 0;

        if (!freshUrl.empty()) {
            m_streamer.StopDownload();
            m_audio.ClearBuffers(m_crossfadeEnabled, track.duration);
            m_streamer.StartDownload(freshUrl);
            m_audio.Resume();

            if (m_savedPosition > 0.0) {
                m_audio.SetPositionSeconds(m_savedPosition);
                m_savedPosition = 0.0;
            }
            std::cout << "\r\033[2K\033[1A\r\033[2K\033[1A\r\033[2K\n> ";
            std::cout.flush();
            return;
        }

        if (attempt < 3) {
            Logger::Log(LogLevel::INFO, "Retrying stream in 2 seconds...");
            QTimer::singleShot(2000, [this, track, attempt]() { AttemptPlay(track, attempt + 1); });
        } else {
            m_playlist.Next();
        }
    };

    if (m_currentProvider) {
        m_currentProvider->FetchTrackUrl(track.id, executePlay);
    } else if (!isDownloaded) {
        QMetaObject::invokeMethod(QCoreApplication::instance(), [this]() {
            m_playlist.Next();
        }, Qt::QueuedConnection);
    }
}