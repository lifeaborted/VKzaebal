#include "PlaybackSessionService.h"
#include "core/audio/IAudioEngine.h"
#include "core/playlist/PlaylistManager.h"
#include "core/audio/playback/PlaybackController.h"
#include "services/config/ConfigurationService.h"
#include "utils/logger/Logger.h"
#include "utils/path/PathManager.h"

#include <QFile>
#include <QDir>
#include <QSet>

void PlaybackSessionService::RestoreSessionState(IAudioEngine& audio,
                                                 PlaylistManager& playlist,
                                                 PlaybackController& playbackCtrl,
                                                 const ConfigurationService& config) {
    playlist.SetRepeatMode(config.GetRepeatMode());
    audio.SetVolume(config.GetVolume());
    playbackCtrl.SetCrossfadeEnabled(config.GetCrossfadeEnabled());
    playbackCtrl.SetStartPaused(!config.GetAutoPlay());
}

void PlaybackSessionService::SaveSessionState(const std::string& activeSource,
                                              IAudioEngine& audio,
                                              PlaylistManager& playlist,
                                              DatabaseManager& dbManager,
                                              ConfigurationService& config) {
    Logger::Log(LogLevel::INFO, "PlaybackSessionService: Saving session state...");
    config.SetVolume(audio.GetVolume());
    config.SetShuffle(playlist.IsShuffle());
    config.SetRepeatMode(playlist.GetRepeatMode());

    if (!activeSource.empty() && playlist.HasTracks()) {
        std::string currentTrackId = playlist.GetCurrentTrack().id;
        int currentIndex = playlist.GetCurrentAbsoluteIndex();
        double currentPos = audio.GetPositionSeconds();
        dbManager.SaveSourceSession(activeSource, currentTrackId, currentIndex, currentPos);
    }
}

std::optional<SourceSession> PlaybackSessionService::LoadSessionForSource(const std::string& source,
                                                                         DatabaseManager& dbManager,
                                                                         const ConfigurationService& config) {
    int savePosMode = config.GetSavePositionMode();
    if (savePosMode > 0) {
        return dbManager.LoadSourceSession(source);
    }
    return std::nullopt;
}

bool PlaybackSessionService::PopulatePlaylistFromStorage(const std::string& activeSource,
                                                         bool isOnline,
                                                         DatabaseManager& dbManager,
                                                         PlaylistManager& playlist,
                                                         const ConfigurationService& config) {
    if (playlist.HasTracks()) return true;

    std::vector<Track> cachedTracks;
    if (activeSource == "All") {
        cachedTracks = dbManager.LoadAllSourcesTracks();
    } else if (activeSource.rfind("Custom:", 0) == 0) {
        std::string plName = activeSource.substr(7);
        int plId = -1;
        cachedTracks = dbManager.LoadPlaylistTracksByName(plName, plId);
    } else {
        cachedTracks = dbManager.LoadTracks(activeSource);
    }

    if (isOnline) {
        for (const auto& t : cachedTracks) {
            playlist.AddTrack(t);
        }
    } else {
        QSet<QString> downloadedFiles;
        auto scanDir = [&](const QString& dirPath) {
            if (dirPath.isEmpty()) return;
            QDir dir(dirPath);
            if (dir.exists()) {
                const auto entries = dir.entryList(QDir::Files | QDir::NoDotAndDotDot);
                for (const auto& f : entries) {
                    downloadedFiles.insert(f.toLower());
                }
            }
        };

        scanDir(PathManager::GetDownloadsDir());
        QString custom = PathManager::GetCustomDownloadsDir();
        if (!custom.isEmpty()) scanDir(custom);

        for (const auto& t : cachedTracks) {
            QString safeBase = QString::fromStdString(t.GetSafeFilename()).toLower();
            if (downloadedFiles.contains(safeBase + ".mp3") ||
                downloadedFiles.contains(safeBase + ".aac")) {
                playlist.AddTrack(t);
            }
        }
    }

    if (config.GetShuffle()) {
        std::vector<std::string> savedQueue = dbManager.LoadQueueIds(activeSource, true);
        if (!savedQueue.empty()) playlist.RestoreShuffleQueue(savedQueue);
        else playlist.SetShuffle(true);
    }

    return playlist.HasTracks();
}

void PlaybackSessionService::ApplySessionToPlayback(const SourceSession& session,
                                                    int savePosMode,
                                                    PlaylistManager& playlist,
                                                    PlaybackController& playbackCtrl) {
    const auto& allTracks = playlist.GetAllTracks();
    int targetIndex = -1;

    // 1. Поиск по trackId
    if (!session.trackId.empty()) {
        for (size_t i = 0; i < allTracks.size(); ++i) {
            if (allTracks[i].id == session.trackId) {
                targetIndex = static_cast<int>(i);
                break;
            }
        }
    }

    // 2. Фолбэк на trackIndex
    if (targetIndex < 0 && session.trackIndex >= 0 && session.trackIndex < static_cast<int>(allTracks.size())) {
        targetIndex = session.trackIndex;
    }

    // 3. Выставляем сохраненную позицию, если режим 2 (track + time)
    if (savePosMode == 2 && session.positionSeconds > 0.0) {
        playbackCtrl.SetSavedPosition(session.positionSeconds, session.trackId);
    } else {
        playbackCtrl.SetSavedPosition(0.0, "");
    }

    if (targetIndex >= 0 && targetIndex < static_cast<int>(allTracks.size())) {
        playlist.JumpTo(targetIndex);
    } else {
        playlist.OnTrackRequested(playlist.GetCurrentTrack());
    }
}
