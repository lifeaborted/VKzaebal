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
    : QObject(parent), m_audio(audio), m_playlist(playlist), m_streamer(streamer) {
    m_debounceTimer = new QTimer(this);
    m_debounceTimer->setSingleShot(true);
    connect(m_debounceTimer, &QTimer::timeout, this, [this]() {
        ExecuteAttemptPlay(m_pendingTrack, 1, m_pendingGen);
    });
}

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

void PlaybackController::CancelProviderFetch() {
    if (m_currentProvider) {
        m_currentProvider->CancelFetchTrackUrl();
    }
}

void PlaybackController::ClearState() {
    if (m_debounceTimer) {
        m_debounceTimer->stop();
    }
    m_playbackGeneration.fetch_add(1, std::memory_order_relaxed);
    CancelProviderFetch();
    m_streamer.StopDownload();
    m_audio.ClearBuffers(false, 0);
    m_audio.Pause();
    m_preloadedTrack = Track();
    m_cachedNextUrl = "";
    m_savedPosition = 0.0;
    m_savedPositionTrackId = "";
}

void PlaybackController::CancelPlaybackAndRetries() {
    if (m_debounceTimer) {
        m_debounceTimer->stop();
    }
    m_playbackGeneration.fetch_add(1, std::memory_order_relaxed);
    CancelProviderFetch();
    m_streamer.StopDownload();
    m_audio.Pause();
    m_audio.ClearBuffers(false, 0);
    Logger::Log(LogLevel::INFO, "PlaybackController: Playback and retries cancelled.");
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

    if (m_debounceTimer) {
        m_debounceTimer->stop();
    }

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

    // 1. Локальный трек (скачан на диск) - играть немедленно без дебаунса
    if (isDownloaded) {
        CancelProviderFetch();
        m_skipCount = 0;
        m_streamer.StopDownload();
        bool shouldCrossfade = m_crossfadeEnabled && m_audio.IsPlaying();
        if (m_audio.PlayStream("", track.duration, shouldCrossfade, track.GetSafeFilename())) {
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

    // 2. Предзагруженный сетевой URL - играть немедленно без дебаунса
    if (attempt == 1 && !m_cachedNextUrl.empty() && m_preloadedTrack.id == track.id) {
        CancelProviderFetch();
        std::string urlToPlay = m_cachedNextUrl;
        m_cachedNextUrl = "";
        m_preloadedTrack = Track();
        m_skipCount = 0;

        m_streamer.StopDownload();
        bool shouldCrossfade = m_crossfadeEnabled && m_audio.IsPlaying();
        m_audio.ClearBuffers(shouldCrossfade, track.duration);
        m_streamer.SetTrackDuration(track.duration);
        m_streamer.StartDownload(urlToPlay);
        m_audio.Resume();

        if (m_savedPosition > 0.0 && m_savedPositionTrackId == track.id) {
            m_audio.SetPositionSeconds(m_savedPosition);
            m_savedPosition = 0.0;
            m_savedPositionTrackId = "";
        }

        if (m_startPaused) { m_audio.Pause(); m_startPaused = false; }
        return;
    }

    // 3. Сетевой трек (ссылка еще не получена)
    // Мгновенно останавливаем предыдущий аудиопоток и отменяем предыдущий запрос ссылки
    CancelProviderFetch();
    m_streamer.StopDownload();
    if (!m_crossfadeEnabled || !m_audio.IsPlaying()) {
        m_audio.Pause();
        m_audio.ClearBuffers(false, track.duration);
    }

    if (attempt == 1) {
        Logger::Log(LogLevel::INFO, "[Загрузка] " + track.artist + " - " + track.title + "...");
        m_pendingTrack = track;
        m_pendingGen = currentGen;
        m_debounceTimer->start(180); // 180ms debounce
    } else {
        ExecuteAttemptPlay(track, attempt, currentGen);
    }
}

void PlaybackController::ExecuteAttemptPlay(const Track& track, int attempt, int expectedGen) {
    if (expectedGen != m_playbackGeneration.load()) {
        return;
    }

    IAudioProvider* provider = (m_providerResolver && !track.source.empty()) ? m_providerResolver(track.source) : m_currentProvider;
    if (!provider) {
        if (expectedGen == m_playbackGeneration.load()) {
            m_playlist.Next();
        }
        return;
    }

    QPointer<PlaybackController> safeThis(this);
    auto executePlay = [safeThis, track, attempt, expectedGen](const std::string& freshUrl, bool isNetworkError) {
        if (!safeThis) return;
        if (expectedGen != safeThis->m_playbackGeneration.load()) return;

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
            bool shouldCrossfade = safeThis->m_crossfadeEnabled && safeThis->m_audio.IsPlaying();
            safeThis->m_audio.ClearBuffers(shouldCrossfade, track.duration);
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
            QTimer::singleShot(2000, safeThis.data(), [safeThis, track, attempt, expectedGen]() {
                if (safeThis && safeThis->m_playbackGeneration.load() == expectedGen) {
                    safeThis->ExecuteAttemptPlay(track, attempt + 1, expectedGen);
                }
            });
        } else {
            safeThis->m_playlist.Next();
        }
    };

    provider->FetchTrackUrl(track.id, executePlay);
}