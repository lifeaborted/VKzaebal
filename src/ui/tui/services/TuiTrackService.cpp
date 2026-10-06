#include "TuiTrackService.h"
#include "core/auth/router/SourceRouter.h"
#include "services/database/DatabaseManager.h"
#include "services/downloader/TrackDownloader.h"
#include "core/playlist/PlaylistManager.h"
#include "services/config/ConfigurationService.h"
#include "core/api/IAudioProvider.h"
#include "utils/logger/Logger.h"

namespace tui {

TuiTrackService::TuiTrackService(SourceRouter& router,
                                 DatabaseManager& dbManager,
                                 TrackDownloader& downloader,
                                 PlaylistManager& playlist,
                                 ConfigurationService* configService)
    : m_router(router),
      m_dbManager(dbManager),
      m_downloader(downloader),
      m_playlist(playlist),
      m_configService(configService) {}

std::vector<Track> TuiTrackService::SearchLocalTracks(const std::string& source, const QString& query) {
    std::vector<Track> candidateTracks;
    if (source == "All") {
        candidateTracks = m_dbManager.LoadAllSourcesTracks();
        if (candidateTracks.empty()) {
            candidateTracks = m_playlist.GetAllTracks();
        }
    } else {
        int dummyId = 0;
        candidateTracks = m_dbManager.LoadPlaylistTracksByName(source, dummyId);
        if (candidateTracks.empty()) {
            candidateTracks = m_dbManager.LoadTracks(source);
        }
        if (candidateTracks.empty()) {
            for (const auto& t : m_playlist.GetAllTracks()) {
                if (t.source == source) {
                    candidateTracks.push_back(t);
                }
            }
        }
    }

    std::vector<Track> matched;
    matched.reserve(candidateTracks.size());
    for (const auto& t : candidateTracks) {
        if (QString::fromStdString(t.title).contains(query, Qt::CaseInsensitive) ||
            QString::fromStdString(t.artist).contains(query, Qt::CaseInsensitive)) {
            matched.push_back(t);
        }
    }
    return matched;
}

std::unordered_map<std::string, std::string> TuiTrackService::GetTrackPlaylistMap(const std::vector<Track>& tracks) {
    std::unordered_map<std::string, std::string> result;
    auto playlists = m_dbManager.GetPlaylists();
    std::unordered_map<int, std::string> plNames;
    for (const auto& pl : playlists) {
        plNames[pl.id] = pl.name;
    }

    for (const auto& t : tracks) {
        if (t.id.empty()) continue;
        auto plIds = m_dbManager.GetPlaylistIdsContainingTrack(t.id);
        std::string joinedNames;
        for (int id : plIds) {
            auto it = plNames.find(id);
            if (it != plNames.end() && !it->second.empty()) {
                if (!joinedNames.empty()) {
                    joinedNames += ", ";
                }
                joinedNames += it->second;
            }
        }
        if (!joinedNames.empty()) {
            result[t.id] = joinedNames;
        }
    }
    return result;
}

std::unordered_set<std::string> TuiTrackService::LoadFavoriteTrackIds() {
    auto allTracks = m_dbManager.LoadAllSourcesTracks();
    std::unordered_set<std::string> ids;
    ids.reserve(allTracks.size());
    for (const auto& t : allTracks) {
        if (!t.id.empty()) {
            ids.insert(t.id);
        }
    }
    return ids;
}

void TuiTrackService::DownloadTrack(const Track& track,
                                   std::function<void(const std::string& msg)> onStatus) {
    if (track.id.empty()) return;

    if (!track.url.empty() && track.url.rfind("http", 0) == 0) {
        m_downloader.Download(track, track.url);
        if (onStatus) onStatus("Скачивание: " + track.title);
        return;
    }

    std::string src = track.source.empty() ? "VK" : track.source;
    IAudioProvider* provider = m_router.GetOrCreateProvider(src);
    if (!provider) {
        if (onStatus) onStatus("Ошибка: неизвестный сервис " + src);
        return;
    }

    if (onStatus) onStatus("Получение ссылки для " + track.title + "...");
    provider->FetchTrackUrl(track.id, [this, track, onStatus](const std::string& url, bool err) {
        if (!err && !url.empty()) {
            m_downloader.Download(track, url);
            if (onStatus) onStatus("Скачивание: " + track.title);
        } else {
            if (onStatus) onStatus("Ошибка скачивания: не удалось получить ссылку");
        }
    });
}

void TuiTrackService::ToggleLikeForTrack(const Track& track,
                                        std::unordered_set<std::string>& favoriteTrackIds,
                                        std::function<void(bool success, bool liked, const std::string& msg)> onComplete) {
    if (track.id.empty()) return;

    bool isLiked = (favoriteTrackIds.count(track.id) > 0);
    if (isLiked) {
        m_router.RemoveTrackFromFavorites(track, [this, track, &favoriteTrackIds, onComplete](bool ok, const std::string& err) {
            if (ok) {
                favoriteTrackIds.erase(track.id);
                m_dbManager.SetTrackIsLibrary(track.id, false);
                std::string activeSource = m_configService ? m_configService->GetActiveSource() : "";
                if (activeSource == track.source || (activeSource == "VK" && (track.source.empty() || track.source == "VK"))) {
                    int idx = m_playlist.FindTrackIndexById(track.id);
                    if (idx >= 0) {
                        m_playlist.RemoveTrack(idx);
                    }
                }
                if (onComplete) onComplete(true, false, "[Избранное] Трек удален: " + track.title);
            } else {
                if (onComplete) onComplete(false, true, "[Ошибка] " + err);
            }
        });
    } else {
        m_router.AddTrackToFavorites(track, [this, track, &favoriteTrackIds, onComplete](bool ok, const std::string& err) {
            if (ok) {
                favoriteTrackIds.insert(track.id);
                m_dbManager.SaveTracks({track}, true);
                m_dbManager.SetTrackIsLibrary(track.id, true);
                std::string activeSource = m_configService ? m_configService->GetActiveSource() : "";
                if (activeSource == track.source || (activeSource == "VK" && (track.source.empty() || track.source == "VK"))) {
                    int idx = m_playlist.FindTrackIndexById(track.id);
                    if (idx < 0) {
                        m_playlist.InsertTrack(0, track);
                    }
                }
                if (onComplete) onComplete(true, true, "[Избранное] Трек добавлен: " + track.title);
            } else {
                if (onComplete) onComplete(false, false, "[Ошибка] " + err);
            }
        });
    }
}

} // namespace tui
