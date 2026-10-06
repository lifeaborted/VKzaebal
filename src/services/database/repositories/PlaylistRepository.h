#pragma once
#include <QSqlDatabase>
#include <QSqlQuery>
#include <vector>
#include <string>
#include "../../../models/Track.h"

struct PlaylistInfo {
    int id = 0;
    std::string name;
    int trackCount = 0;
};

class PlaylistRepository {
public:
    explicit PlaylistRepository(QSqlDatabase& db);

    bool CreatePlaylist(const std::string& name);
    bool DeletePlaylist(int playlistId);
    bool DeletePlaylist(const std::string& name);
    std::vector<PlaylistInfo> GetPlaylists();
    bool AddTrackToPlaylist(int playlistId, const std::string& trackId);
    bool IsTrackInPlaylist(int playlistId, const std::string& trackId);
    std::vector<int> GetPlaylistIdsContainingTrack(const std::string& trackId);
    bool RemoveTrackFromPlaylist(int playlistId, int position);
    std::vector<Track> LoadPlaylistTracks(int playlistId);
    std::vector<Track> LoadPlaylistTracksByName(const std::string& name, int& outId);

private:
    QSqlDatabase& m_db;
};
