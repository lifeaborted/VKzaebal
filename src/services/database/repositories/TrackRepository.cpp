#include "TrackRepository.h"
#include "utils/logger/Logger.h"
#include "utils/path/PathManager.h"

#include <QThreadPool>
#include <QUuid>
#include <QVariant>
#include <QSqlError>
#include <unordered_map>

TrackRepository::TrackRepository(QSqlDatabase& db)
    : m_db(db) {}

void TrackRepository::SaveTracks(const std::vector<Track>& tracks, bool isLibrary) {
    QThreadPool::globalInstance()->start([tracks, isLibrary]() {
        QString connectionName = QUuid::createUuid().toString();
        {
            QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connectionName);
            db.setConnectOptions("QSQLITE_BUSY_TIMEOUT=5000");
            db.setDatabaseName(PathManager::GetDbPath());

            if (db.open()) {
                db.transaction();
                QSqlQuery query(db);
                query.prepare(
                    "INSERT INTO Tracks (source, external_id, artist, title, duration, cover_url, lyrics_id, lyrics, is_library) "
                    "VALUES (:source, :external_id, :artist, :title, :duration, :cover_url, :lyrics_id, :lyrics, :is_library) "
                    "ON CONFLICT(source, external_id) DO UPDATE SET "
                    "artist = excluded.artist, "
                    "title = excluded.title, "
                    "duration = excluded.duration, "
                    "cover_url = excluded.cover_url, "
                    "lyrics_id = CASE WHEN excluded.lyrics_id != '' THEN excluded.lyrics_id ELSE Tracks.lyrics_id END, "
                    "lyrics = CASE WHEN excluded.lyrics != '' THEN excluded.lyrics ELSE Tracks.lyrics END, "
                    "is_library = CASE WHEN excluded.is_library = 1 THEN 1 ELSE Tracks.is_library END"
                );

                for (const auto& track : tracks) {
                    query.bindValue(":source", QString::fromStdString(track.source));
                    query.bindValue(":external_id", QString::fromStdString(track.id));
                    query.bindValue(":artist", QString::fromStdString(track.artist));
                    query.bindValue(":title", QString::fromStdString(track.title));
                    query.bindValue(":duration", track.duration);
                    query.bindValue(":cover_url", QString::fromStdString(track.coverUrl));
                    query.bindValue(":lyrics_id", QString::fromStdString(track.lyrics_id));
                    query.bindValue(":lyrics", QString::fromStdString(track.lyrics));
                    query.bindValue(":is_library", isLibrary ? 1 : 0);
                    query.exec();
                }
                db.commit();
                db.close();
            } else {
                Logger::Log(LogLevel::ERROR, "DB: Failed to open threaded connection for SaveTracks.");
            }
        }
        QSqlDatabase::removeDatabase(connectionName);
    });
}

void TrackRepository::SetTrackIsLibrary(const std::string& trackId, bool isLibrary) {
    if (trackId.empty()) return;
    QSqlQuery query(m_db);
    query.prepare("UPDATE Tracks SET is_library = :isLib WHERE external_id = :id");
    query.bindValue(":isLib", isLibrary ? 1 : 0);
    query.bindValue(":id", QString::fromStdString(trackId));
    if (!query.exec()) {
        Logger::Log(LogLevel::ERROR, "DB: Failed to set is_library for track " + trackId);
    }
}

Track TrackRepository::TrackFromSqlRecord(const QSqlQuery& query) {
    Track t;
    t.dbId = query.value(0).toLongLong();
    t.id = query.value(1).toString().toStdString();
    t.artist = query.value(2).toString().toStdString();
    t.title = query.value(3).toString().toStdString();
    t.duration = query.value(4).toInt();
    t.coverUrl = query.value(5).toString().toStdString();
    t.lyrics_id = query.value(6).toString().toStdString();
    t.lyrics = query.value(7).toString().toStdString();
    t.source = query.value(8).toString().toStdString();
    auto usPos = t.id.find('_');
    if (usPos != std::string::npos) {
        t.ownerId = t.id.substr(0, usPos);
    }
    return t;
}

std::vector<Track> TrackRepository::LoadTracks(const std::string& source, const std::vector<std::string>& standardOrder) {
    std::vector<Track> tracks;
    QSqlQuery query(m_db);

    if (source == "Offline") {
        query.prepare("SELECT id, external_id, artist, title, duration, cover_url, lyrics_id, lyrics, source "
                      "FROM Tracks "
                      "WHERE is_library = 1 "
                      "ORDER BY id ASC");
    } else {
        query.prepare("SELECT id, external_id, artist, title, duration, cover_url, lyrics_id, lyrics, source "
                      "FROM Tracks "
                      "WHERE source = :source AND is_library = 1 "
                      "ORDER BY id ASC");
        query.bindValue(":source", QString::fromStdString(source));
    }

    if (query.exec()) {
        tracks.reserve(512);
        while (query.next()) {
            tracks.push_back(TrackFromSqlRecord(query));
        }
    }

    if (source != "Offline" && !standardOrder.empty()) {
        std::unordered_map<std::string, Track> trackMap;
        trackMap.reserve(tracks.size());
        for (auto& t : tracks) {
            trackMap.emplace(t.id, std::move(t));
        }

        std::vector<Track> ordered;
        ordered.reserve(tracks.size());
        for (const auto& id : standardOrder) {
            auto it = trackMap.find(id);
            if (it != trackMap.end()) {
                ordered.push_back(std::move(it->second));
                trackMap.erase(it);
            }
        }
        for (auto& pair : trackMap) {
            ordered.push_back(std::move(pair.second));
        }
        tracks = std::move(ordered);
    }

    Logger::Log(LogLevel::INFO, "DB: Loaded " + std::to_string(tracks.size()) + " tracks for source " + source);
    return tracks;
}

void TrackRepository::UpdateTrackLyrics(const std::string& trackId, const std::string& lyrics) {
    QSqlQuery query(m_db);
    query.prepare("UPDATE Tracks SET lyrics = :lyrics WHERE external_id = :id");
    query.bindValue(":lyrics", QString::fromStdString(lyrics));
    query.bindValue(":id", QString::fromStdString(trackId));
    if (!query.exec()) {
        Logger::Log(LogLevel::ERROR, "DB: Failed to update lyrics for track " + trackId);
    }
}

void TrackRepository::ClearTracksForSource(const std::string& source) {
    if (source == "all" || source == "ALL") {
        QSqlQuery q1(m_db);
        q1.exec("DELETE FROM Tracks WHERE external_id NOT IN (SELECT track_id FROM PlaylistTracks)");
        q1.exec("UPDATE Tracks SET is_library = 0");
    } else {
        QSqlQuery q1(m_db);
        q1.prepare("DELETE FROM Tracks WHERE source = :source AND external_id NOT IN (SELECT track_id FROM PlaylistTracks)");
        q1.bindValue(":source", QString::fromStdString(source));
        q1.exec();

        QSqlQuery q2(m_db);
        q2.prepare("UPDATE Tracks SET is_library = 0 WHERE source = :source");
        q2.bindValue(":source", QString::fromStdString(source));
        q2.exec();
    }

    QSqlQuery qClean(m_db);
    qClean.exec("DELETE FROM PlaylistTracks WHERE track_id NOT IN (SELECT external_id FROM Tracks)");
}

std::vector<Track> TrackRepository::LoadAllSourcesTracks() {
    std::vector<Track> tracks;
    QSqlQuery query(m_db);
    query.prepare(
        "SELECT id, external_id, artist, title, duration, cover_url, lyrics_id, lyrics, source "
        "FROM Tracks "
        "WHERE is_library = 1 "
        "ORDER BY CASE source "
        "    WHEN 'VK' THEN 1 "
        "    WHEN 'Spotify' THEN 2 "
        "    WHEN 'SoundCloud' THEN 3 "
        "    WHEN 'Yandex' THEN 4 "
        "    WHEN 'YouTube' THEN 5 "
        "    ELSE 6 END, id ASC"
    );

    if (query.exec()) {
        tracks.reserve(1024);
        while (query.next()) {
            tracks.push_back(TrackFromSqlRecord(query));
        }
    }
    return tracks;
}
