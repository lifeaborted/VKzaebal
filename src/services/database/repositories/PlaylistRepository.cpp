#include "PlaylistRepository.h"
#include "TrackRepository.h"
#include "utils/logger/Logger.h"

#include <QVariant>
#include <QSqlError>

PlaylistRepository::PlaylistRepository(QSqlDatabase& db)
    : m_db(db) {}

bool PlaylistRepository::CreatePlaylist(const std::string& name) {
    if (name.empty()) return false;
    QSqlQuery query(m_db);
    query.prepare("INSERT INTO Playlists (name) VALUES (:name)");
    query.bindValue(":name", QString::fromStdString(name));
    if (!query.exec()) {
        Logger::Log(LogLevel::WARNING, "DB: Failed to create playlist '" + name + "': " + query.lastError().text().toStdString());
        return false;
    }
    Logger::Log(LogLevel::INFO, "DB: Created playlist '" + name + "'");
    return true;
}

bool PlaylistRepository::DeletePlaylist(int playlistId) {
    QSqlQuery q1(m_db);
    q1.prepare("DELETE FROM PlaylistTracks WHERE playlist_id = :id");
    q1.bindValue(":id", playlistId);
    q1.exec();

    QSqlQuery q2(m_db);
    q2.prepare("DELETE FROM Playlists WHERE id = :id");
    q2.bindValue(":id", playlistId);
    return q2.exec();
}

bool PlaylistRepository::DeletePlaylist(const std::string& name) {
    QSqlQuery query(m_db);
    query.prepare("SELECT id FROM Playlists WHERE name = :name COLLATE NOCASE");
    query.bindValue(":name", QString::fromStdString(name));
    if (query.exec() && query.next()) {
        int id = query.value(0).toInt();
        return DeletePlaylist(id);
    }
    return false;
}

std::vector<PlaylistInfo> PlaylistRepository::GetPlaylists() {
    std::vector<PlaylistInfo> list;
    QSqlQuery query(m_db);
    query.prepare("SELECT p.id, p.name, COUNT(pt.track_id) "
                  "FROM Playlists p "
                  "LEFT JOIN PlaylistTracks pt ON p.id = pt.playlist_id "
                  "GROUP BY p.id, p.name "
                  "ORDER BY p.id ASC");
    if (query.exec()) {
        while (query.next()) {
            PlaylistInfo info;
            info.id = query.value(0).toInt();
            info.name = query.value(1).toString().toStdString();
            info.trackCount = query.value(2).toInt();
            list.push_back(info);
        }
    }
    return list;
}

bool PlaylistRepository::AddTrackToPlaylist(int playlistId, const std::string& trackId) {
    if (trackId.empty() || playlistId <= 0) return false;

    if (IsTrackInPlaylist(playlistId, trackId)) {
        return false;
    }

    QSqlQuery posQuery(m_db);
    posQuery.prepare("SELECT COALESCE(MAX(position), -1) + 1 FROM PlaylistTracks WHERE playlist_id = :pid");
    posQuery.bindValue(":pid", playlistId);
    int nextPos = 0;
    if (posQuery.exec() && posQuery.next()) {
        nextPos = posQuery.value(0).toInt();
    }

    QSqlQuery insQuery(m_db);
    insQuery.prepare("INSERT OR REPLACE INTO PlaylistTracks (playlist_id, position, track_id) VALUES (:pid, :pos, :tid)");
    insQuery.bindValue(":pid", playlistId);
    insQuery.bindValue(":pos", nextPos);
    insQuery.bindValue(":tid", QString::fromStdString(trackId));
    return insQuery.exec();
}

bool PlaylistRepository::IsTrackInPlaylist(int playlistId, const std::string& trackId) {
    if (trackId.empty() || playlistId <= 0) return false;
    QSqlQuery q(m_db);
    q.prepare("SELECT 1 FROM PlaylistTracks WHERE playlist_id = :pid AND track_id = :tid LIMIT 1;");
    q.bindValue(":pid", playlistId);
    q.bindValue(":tid", QString::fromStdString(trackId));
    return q.exec() && q.next();
}

std::vector<int> PlaylistRepository::GetPlaylistIdsContainingTrack(const std::string& trackId) {
    std::vector<int> ids;
    if (trackId.empty()) return ids;
    QSqlQuery q(m_db);
    q.prepare("SELECT DISTINCT playlist_id FROM PlaylistTracks WHERE track_id = :tid;");
    q.bindValue(":tid", QString::fromStdString(trackId));
    if (q.exec()) {
        while (q.next()) {
            ids.push_back(q.value(0).toInt());
        }
    }
    return ids;
}

bool PlaylistRepository::RemoveTrackFromPlaylist(int playlistId, int position) {
    QSqlQuery delQuery(m_db);
    delQuery.prepare("DELETE FROM PlaylistTracks WHERE playlist_id = :pid AND position = :pos");
    delQuery.bindValue(":pid", playlistId);
    delQuery.bindValue(":pos", position);
    if (!delQuery.exec()) return false;

    QSqlQuery shiftQuery(m_db);
    shiftQuery.prepare("UPDATE PlaylistTracks SET position = position - 1 WHERE playlist_id = :pid AND position > :pos");
    shiftQuery.bindValue(":pid", playlistId);
    shiftQuery.bindValue(":pos", position);
    return shiftQuery.exec();
}

std::vector<Track> PlaylistRepository::LoadPlaylistTracks(int playlistId) {
    std::vector<Track> tracks;
    QSqlQuery query(m_db);
    query.prepare("SELECT t.id, t.external_id, t.artist, t.title, t.duration, t.cover_url, t.lyrics_id, t.lyrics, t.source "
                  "FROM PlaylistTracks pt "
                  "JOIN Tracks t ON pt.track_id = t.external_id "
                  "WHERE pt.playlist_id = :pid "
                  "ORDER BY pt.position ASC");
    query.bindValue(":pid", playlistId);
    if (query.exec()) {
        tracks.reserve(128);
        while (query.next()) {
            tracks.push_back(TrackRepository::TrackFromSqlRecord(query));
        }
    }
    return tracks;
}

std::vector<Track> PlaylistRepository::LoadPlaylistTracksByName(const std::string& name, int& outId) {
    outId = -1;
    QSqlQuery query(m_db);
    query.prepare("SELECT id FROM Playlists WHERE name = :name COLLATE NOCASE");
    query.bindValue(":name", QString::fromStdString(name));
    if (query.exec() && query.next()) {
        outId = query.value(0).toInt();
        return LoadPlaylistTracks(outId);
    }
    return {};
}
