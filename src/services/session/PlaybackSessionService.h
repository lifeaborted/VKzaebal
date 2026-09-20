#pragma once

#include <string>
#include <optional>
#include <vector>
#include "services/database/DatabaseManager.h"

class IAudioEngine;
class PlaylistManager;
class PlaybackController;
class ConfigurationService;
struct Track;

class PlaybackSessionService {
public:
    PlaybackSessionService() = default;
    ~PlaybackSessionService() = default;

    void RestoreSessionState(IAudioEngine& audio,
                             PlaylistManager& playlist,
                             PlaybackController& playbackCtrl,
                             const ConfigurationService& config);

    void SaveSessionState(const std::string& activeSource,
                          IAudioEngine& audio,
                          PlaylistManager& playlist,
                          DatabaseManager& dbManager,
                          ConfigurationService& config);

    std::optional<SourceSession> LoadSessionForSource(const std::string& source,
                                                     DatabaseManager& dbManager,
                                                     const ConfigurationService& config);

    bool PopulatePlaylistFromStorage(const std::string& activeSource,
                                     bool isOnline,
                                     DatabaseManager& dbManager,
                                     PlaylistManager& playlist,
                                     const ConfigurationService& config);

    void ApplySessionToPlayback(const SourceSession& session,
                                int savePosMode,
                                PlaylistManager& playlist,
                                PlaybackController& playbackCtrl);
};
