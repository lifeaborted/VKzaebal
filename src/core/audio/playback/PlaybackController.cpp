#include "PlaybackController.h"
#include "core/audio/IAudioEngine.h"
#include "core/playlist/PlaylistManager.h"
#include "core/api/IAudioProvider.h"
#include "services/network/NetworkStreamer.h"
#include "utils/logger/Logger.h"
#include "utils/path/PathManager.h"

#include <QFile>
#include <QTimer>
#include <QPointer>
#include <QCoreApplication>
#include <QSettings>

PlaybackController::PlaybackController(IAudioEngine& audio, PlaylistManager& playlist, NetworkStreamer& streamer, QObject* parent)
    : QObject(parent), m_audio(audio), m_playlist(playlist), m_streamer(streamer) {}

void PlaybackController::SetCurrentProvider(IAudioProvider* provider) {
    m_currentProvider = provider;
}

void PlaybackController::SetProviderResolver(std::function<IAudioProvider*(const std::string& source)> resolver) {
    m_providerResolver = resolver;
}

void PlaybackController::SetCrossfadeEnabled(bool enabled) {
    m_crossfadeEnabled = enabled;
}

void PlaybackController::SetSavedPosition(double pos, const std::string& trackId) {
    m_savedPosition = pos;
    m_savedPositionTrackId = trackId;
}

void PlaybackController::ClearState() {
    m_streamer.StopDownload();
    m_audio.ClearBuffers(false, 0);
    m_audio.Pause();
    m_preloadedTrack = Track();
    m_cachedNextUrl = "";
    m_savedPosition = 0.0;
    m_savedPositionTrackId = "";
}

void PlaybackController::HandleTrackFinished() {
    Logger::Log(LogLevel::INFO, "PlaybackController: Auto-switching to next track...");
    m_playlist.Next();
}

void PlaybackController::HandleTrackNearEnd() {
    Track nextTrack = m_playlist.PeekNextTrack();
    if (nextTrack.id.empty()) return;

    IAudioProvider* provider = (m_providerResolver && !nextTrack.source.empty()) ? m_providerResolver(nextTrack.source) : m_currentProvider;
    if (!provider) return;

    QPointer<PlaybackController> safeThis(this);
    provider->FetchTrackUrl(nextTrack.id, [safeThis, nextTrack](const std::string& freshUrl, bool isNetworkError) {
        if (!safeThis) return;
        if (!isNetworkError && !freshUrl.empty()) {
            safeThis->m_cachedNextUrl = freshUrl;
            safeThis->m_preloadedTrack = nextTrack;
            Logger::Log(LogLevel::INFO, "PlaybackController: Next track URL pre-fetched successfully from " + nextTrack.source);
        }
    });
}

void PlaybackController::AttemptPlay(const Track& track, int attempt) {
    int currentGen = (attempt == 1) ? ++m_playbackGeneration : m_playbackGeneration.load();

    if (attempt == 1) {
        if (m_savedPositionTrackId != track.id) {
            m_savedPosition = 0.0;
            m_savedPositionTrackId = "";
        }
    }

    QString localPath = PathManager::GetDownloadFilePath(track.GetSafeFilename(), "mp3");
    if (!QFile::exists(localPath)) {
        localPath = PathManager::GetDownloadFilePath(track.GetSafeFilename(), "aac");
    }
    bool isDownloaded = QFile::exists(localPath);

    if (attempt == 1 && !m_cachedNextUrl.empty() && m_preloadedTrack.id == track.id && !isDownloaded) {
        if (m_audio.PlayStream(m_cachedNextUrl, track.duration, m_crossfadeEnabled, track.GetSafeFilename())) {
            m_cachedNextUrl = "";
            m_skipCount = 0;

            if (m_savedPosition > 0.0 && m_savedPositionTrackId == track.id) {
                m_audio.SetPositionSeconds(m_savedPosition);
                m_savedPosition = 0.0;
                m_savedPositionTrackId = "";
            }

            if (m_startPaused) { m_audio.Pause(); m_startPaused = false; }
            return;
        }
    } else if (attempt == 1) {
        Logger::Log(LogLevel::INFO, "[Загрузка] " + track.artist + " - " + track.title + "...");
        // При переключении трека НЕМЕДЛЕННО останавливаем предыдущий стрим и аудиопоток,
        // чтобы старый трек не продолжал играть в фоне во время сетевой загрузки
        m_streamer.StopDownload();
        m_audio.Pause();
        m_audio.ClearBuffers(false, track.duration);
    }

    if (isDownloaded) {
        m_skipCount = 0;
        if (m_audio.PlayStream("", track.duration, m_crossfadeEnabled, track.GetSafeFilename())) {

            if (m_savedPosition > 0.0 && m_savedPositionTrackId == track.id) {
                m_audio.SetPositionSeconds(m_savedPosition);
                m_savedPosition = 0.0;
                m_savedPositionTrackId = "";
            }

            if (m_startPaused) { m_audio.Pause(); m_startPaused = false; }
        } else {
            m_playlist.Next();
        }
        return;
    }

    QPointer<PlaybackController> safeThis(this);
    auto executePlay = [safeThis, track, attempt, currentGen](const std::string& freshUrl, bool isNetworkError) {
        if (!safeThis) return;
        if (currentGen != safeThis->m_playbackGeneration.load()) return;

        if (!isNetworkError && freshUrl.empty()) {
            safeThis->m_skipCount++;
            if (safeThis->m_skipCount >= 5) {
                Logger::Log(LogLevel::ERROR, "Слишком много ошибок подряд. Остановка.");
                safeThis->m_skipCount = 0;
                return;
            }

            Logger::Log(LogLevel::WARNING, "Track is restricted or token invalid. Skipping...");
            safeThis->m_playlist.Next();
            return;
        }

        safeThis->m_skipCount = 0;

        if (!freshUrl.empty()) {
            safeThis->m_streamer.StopDownload();
            safeThis->m_audio.ClearBuffers(safeThis->m_crossfadeEnabled, track.duration);
            safeThis->m_streamer.SetTrackDuration(track.duration);
            safeThis->m_streamer.StartDownload(freshUrl);
            safeThis->m_audio.Resume();

            if (safeThis->m_savedPosition > 0.0 && safeThis->m_savedPositionTrackId == track.id) {
                double posToSeek = safeThis->m_savedPosition;
                safeThis->m_savedPosition = 0.0;
                safeThis->m_savedPositionTrackId = "";

                safeThis->m_audio.SetPositionSeconds(posToSeek);
            }

            if (safeThis->m_startPaused) {
                safeThis->m_audio.Pause();
                safeThis->m_startPaused = false;
            }
            return;
        }

        if (attempt < 3) {
            Logger::Log(LogLevel::INFO, "Retrying stream in 2 seconds...");
            QTimer::singleShot(2000, safeThis.data(), [safeThis, track, attempt, currentGen]() {
                if (safeThis && safeThis->m_playbackGeneration.load() == currentGen) {
                    safeThis->AttemptPlay(track, attempt + 1);
                }
            });
        } else {
            safeThis->m_playlist.Next();
        }
    };

    IAudioProvider* provider = (m_providerResolver && !track.source.empty()) ? m_providerResolver(track.source) : m_currentProvider;

    if (provider) {
        provider->FetchTrackUrl(track.id, executePlay);
    } else if (!isDownloaded) {
        QMetaObject::invokeMethod(this, [safeThis, currentGen]() {
            if (safeThis && safeThis->m_playbackGeneration.load() == currentGen) {
                safeThis->m_playlist.Next();
            }
        }, Qt::QueuedConnection);
    }
}