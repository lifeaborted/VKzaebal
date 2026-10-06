#pragma once

#include <string>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <functional>
#include <QString>
#include "models/Track.h"

class SourceRouter;
class DatabaseManager;
class TrackDownloader;
class PlaylistManager;
class ConfigurationService;

namespace tui {

class TuiTrackService {
public:
    TuiTrackService(SourceRouter& router,
                    DatabaseManager& dbManager,
                    TrackDownloader& downloader,
                    PlaylistManager& playlist,
                    ConfigurationService* configService = nullptr);

    std::vector<Track> SearchLocalTracks(const std::string& source, const QString& query);
    std::unordered_map<std::string, std::string> GetTrackPlaylistMap(const std::vector<Track>& tracks);

    void DownloadTrack(const Track& track,
                       std::function<void(const std::string& msg)> onStatus);

    void ToggleLikeForTrack(const Track& track,
                            std::unordered_set<std::string>& favoriteTrackIds,
                            std::function<void(bool success, bool liked, const std::string& msg)> onComplete);

    std::unordered_set<std::string> LoadFavoriteTrackIds();

private:
    SourceRouter& m_router;
    DatabaseManager& m_dbManager;
    TrackDownloader& m_downloader;
    PlaylistManager& m_playlist;
    ConfigurationService* m_configService = nullptr;
};

} // namespace tui
