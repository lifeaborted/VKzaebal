#include "DatabaseManager.h"
#include "utils/logger/Logger.h"
#include "utils/path/PathManager.h"

#include <QSqlError>
#include <QVariant>
#include <QFile>
#include <QTextStream>
#include <QStringList>
#include <QtConcurrent>
#include <QUuid>

DatabaseManager::DatabaseManager() {
    m_db = QSqlDatabase::addDatabase("QSQLITE");
    m_db.setConnectOptions("QSQLITE_BUSY_TIMEOUT=5000");
    m_db.setDatabaseName(PathManager::GetDbPath());
}

DatabaseManager::~DatabaseManager() {
    if (m_db.isOpen()) {
        m_db.close();
    }
}

bool DatabaseManager::Init() {
    if (!m_db.open()) {
        Logger::Log(LogLevel::ERROR, "DB: Failed to open database: " + m_db.lastError().text().toStdString());
        return false;
    }
    CreateTables();
    return true;
}

void DatabaseManager::CreateTables() {
    QSqlQuery query;
    query.exec("CREATE TABLE IF NOT EXISTS Settings (key TEXT PRIMARY KEY, value TEXT)");

    query.exec("CREATE TABLE IF NOT EXISTS Tracks ("
               "id TEXT PRIMARY KEY, "
               "source TEXT, "
               "artist TEXT, "
               "title TEXT, "
               "duration TEXT, "
               "cover_url TEXT, "
               "lyrics_id TEXT, "
               "lyrics TEXT)");

    query.exec("CREATE TABLE IF NOT EXISTS PlayQueue ("
               "position INTEGER, "
               "id TEXT, "
               "source TEXT, "
               "is_shuffle INTEGER, "
               "PRIMARY KEY(position, source, is_shuffle))");

    query.exec("CREATE INDEX IF NOT EXISTS idx_playqueue_lookup ON PlayQueue(source, is_shuffle)");
    query.exec("CREATE INDEX IF NOT EXISTS idx_tracks_source ON Tracks(source)");

    query.exec("PRAGMA foreign_keys = ON");

    query.exec("CREATE TABLE IF NOT EXISTS Playlists ("
               "id INTEGER PRIMARY KEY AUTOINCREMENT, "
               "name TEXT UNIQUE NOT NULL, "
               "created_at DATETIME DEFAULT CURRENT_TIMESTAMP)");

    query.exec("CREATE TABLE IF NOT EXISTS PlaylistTracks ("
               "playlist_id INTEGER, "
               "position INTEGER, "
               "track_id TEXT, "
               "PRIMARY KEY(playlist_id, position), "
               "FOREIGN KEY(playlist_id) REFERENCES Playlists(id) ON DELETE CASCADE, "
               "FOREIGN KEY(track_id) REFERENCES Tracks(id) ON DELETE CASCADE)");

    query.exec("CREATE INDEX IF NOT EXISTS idx_playlist_tracks_lookup ON PlaylistTracks(playlist_id, position)");
}

void DatabaseManager::SetSetting(const QString& key, const QString& value) {
    QSqlQuery query;
    query.prepare("INSERT OR REPLACE INTO Settings (key, value) VALUES (:key, :value)");
    query.bindValue(":key", key);
    query.bindValue(":value", value);
    query.exec();
}

QString DatabaseManager::GetSetting(const QString& key) const {
    QSqlQuery query;
    query.prepare("SELECT value FROM Settings WHERE key = :key");
    query.bindValue(":key", key);
    if (query.exec() && query.next()) {
        return query.value(0).toString();
    }
    return "";
}

void DatabaseManager::ClearSetting(const QString& key) {
    QSqlQuery query;
    query.prepare("DELETE FROM Settings WHERE key = :key");
    query.bindValue(":key", key);
    query.exec();
}

void DatabaseManager::SaveQueue(const std::vector<Track>& currentQueue, const std::string& source, bool isShuffle) {
    QThreadPool::globalInstance()->start([currentQueue, source, isShuffle]() {
        QString connectionName = QUuid::createUuid().toString();
        {
            QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connectionName);
            db.setConnectOptions("QSQLITE_BUSY_TIMEOUT=5000");
            db.setDatabaseName(PathManager::GetDbPath());

            if (db.open()) {
                db.transaction();
                QSqlQuery query(db);

                query.prepare("DELETE FROM PlayQueue WHERE is_shuffle = :is_shuffle AND source = :source");
                query.bindValue(":is_shuffle", isShuffle ? 1 : 0);
                query.bindValue(":source", QString::fromStdString(source));
                query.exec();

                query.prepare("INSERT INTO PlayQueue (position, id, source, is_shuffle) VALUES (:pos, :id, :source, :shuffle)");
                for (size_t i = 0; i < currentQueue.size(); ++i) {
                    query.bindValue(":pos", static_cast<int>(i));
                    query.bindValue(":id", QString::fromStdString(currentQueue[i].id));
                    query.bindValue(":source", QString::fromStdString(source));
                    query.bindValue(":shuffle", isShuffle ? 1 : 0);
                    query.exec();
                }
                db.commit();
                db.close();
            } else {
                Logger::Log(LogLevel::ERROR, "DB: Failed to open threaded connection for SaveQueue.");
            }
        }
        QSqlDatabase::removeDatabase(connectionName);
    });
}

void DatabaseManager::ExportQueueToTxt(const std::vector<Track>& queue, const QString& filename, bool isShuffle) const {
    QThreadPool::globalInstance()->start([queue, filename, isShuffle]() {
        QString exportPath = PathManager::GetPlaylistExportPath(filename);
        QFile file(exportPath);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&file);
            out << "=== ТЕКУЩИЙ ПЛЕЙЛИСТ ===\n";
            out << "Режим: " << (isShuffle ? "SHUFFLE" : "СТАНДАРТНЫЙ") << "\n";
            out << "-------------------------\n\n";

            for (size_t i = 0; i < queue.size(); ++i) {
                out << "[" << (i + 1) << "]. " << QString::fromStdString(queue[i].artist)
                    << " - " << QString::fromStdString(queue[i].title)
                    << " [" << QString::fromStdString(queue[i].GetFormattedDuration()) << "]\n";
            }
            file.close();
        } else {
            Logger::Log(LogLevel::ERROR, "DB: Failed to generate playlist export.");
        }
    });
}

void DatabaseManager::SaveTracks(const std::vector<Track>& tracks) {
    QThreadPool::globalInstance()->start([tracks]() {
        QString connectionName = QUuid::createUuid().toString();
        {
            QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connectionName);
            db.setConnectOptions("QSQLITE_BUSY_TIMEOUT=5000");
            db.setDatabaseName(PathManager::GetDbPath());

            if (db.open()) {
                db.transaction();
                QSqlQuery query(db);
                query.prepare("INSERT OR REPLACE INTO Tracks (id, source, artist, title, duration, cover_url, lyrics_id, lyrics) "
                              "VALUES (:id, :source, :artist, :title, :duration, :cover_url, :lyrics_id, :lyrics)");

                for (const auto& track : tracks) {
                    query.bindValue(":id", QString::fromStdString(track.id));
                    query.bindValue(":source", QString::fromStdString(track.source));
                    query.bindValue(":artist", QString::fromStdString(track.artist));
                    query.bindValue(":title", QString::fromStdString(track.title));
                    query.bindValue(":duration", QString::fromStdString(track.GetFormattedDuration()));
                    query.bindValue(":cover_url", QString::fromStdString(track.coverUrl));
                    query.bindValue(":lyrics_id", QString::fromStdString(track.lyrics_id));
                    query.bindValue(":lyrics", QString::fromStdString(track.lyrics));
                    query.exec();
                }
                db.commit();
                db.close();
            } else {
                Logger::Log(LogLevel::ERROR, "DB: Failed to open threaded connection.");
            }
        }
        QSqlDatabase::removeDatabase(connectionName);
    });
}

std::vector<Track> DatabaseManager::LoadTracks(const std::string& source) {
    std::vector<Track> tracks;
    QSqlQuery query;

    if (source == "Offline") {
        query.prepare("SELECT id, artist, title, duration, cover_url, lyrics_id, lyrics, source "
                      "FROM Tracks");
    } else {
        query.prepare("SELECT t.id, t.artist, t.title, t.duration, t.cover_url, t.lyrics_id, t.lyrics, t.source "
                      "FROM PlayQueue q "
                      "JOIN Tracks t ON q.id = t.id "
                      "WHERE q.is_shuffle = 0 AND q.source = :source "
                      "ORDER BY q.position ASC");
        query.bindValue(":source", QString::fromStdString(source));
    }

    query.exec();

    while (query.next()) {
        Track t;
        t.id = query.value(0).toString().toStdString();
        t.artist = query.value(1).toString().toStdString();
        t.title = query.value(2).toString().toStdString();

        QString durationStr = query.value(3).toString();
        QStringList parts = durationStr.split(':');
        if (parts.size() == 2) {
            t.duration = parts[0].toInt() * 60 + parts[1].toInt();
        } else {
            t.duration = 0;
        }

        t.coverUrl = query.value(4).toString().toStdString();
        t.lyrics_id = query.value(5).toString().toStdString();
        t.lyrics = query.value(6).toString().toStdString();
        t.source = query.value(7).toString().toStdString();

        tracks.push_back(t);
    }

    Logger::Log(LogLevel::INFO, "DB: Loaded " + std::to_string(tracks.size()) + " tracks for source " + source);
    return tracks;
}

void DatabaseManager::UpdateTrackLyrics(const std::string& trackId, const std::string& lyrics) {
    QSqlQuery query;
    query.prepare("UPDATE Tracks SET lyrics = :lyrics WHERE id = :id");
    query.bindValue(":lyrics", QString::fromStdString(lyrics));
    query.bindValue(":id", QString::fromStdString(trackId));
    if (!query.exec()) {
        Logger::Log(LogLevel::ERROR, "DB: Failed to update lyrics for track " + trackId);
    }
}

std::vector<std::string> DatabaseManager::LoadQueueIds(const std::string& source, bool isShuffle) const {
    std::vector<std::string> ids;
    QSqlQuery query;
    query.prepare("SELECT id FROM PlayQueue WHERE source = :source AND is_shuffle = :shuffle ORDER BY position ASC");
    query.bindValue(":source", QString::fromStdString(source));
    query.bindValue(":shuffle", isShuffle ? 1 : 0);
    if (query.exec()) {
        while (query.next()) {
            ids.push_back(query.value(0).toString().toStdString());
        }
    }
    return ids;
}

void DatabaseManager::ClearTracksForSource(const std::string& source) {
    if (source == "all" || source == "ALL") {
        QSqlQuery q1(m_db);
        if (!q1.exec("DELETE FROM Tracks")) {
            Logger::Log(LogLevel::ERROR, "DB: Failed to clear Tracks: " + q1.lastError().text().toStdString());
        }
        QSqlQuery q2(m_db);
        if (!q2.exec("DELETE FROM PlayQueue")) {
            Logger::Log(LogLevel::ERROR, "DB: Failed to clear PlayQueue: " + q2.lastError().text().toStdString());
        }
        Logger::Log(LogLevel::INFO, "DB: Cleared all tracks and queues from database.");
    } else {
        QSqlQuery q1(m_db);
        q1.prepare("DELETE FROM Tracks WHERE source = :source");
        q1.bindValue(":source", QString::fromStdString(source));
        if (!q1.exec()) {
            Logger::Log(LogLevel::ERROR, "DB: Failed to clear Tracks for source " + source + ": " + q1.lastError().text().toStdString());
        }

        QSqlQuery q2(m_db);
        q2.prepare("DELETE FROM PlayQueue WHERE source = :source");
        q2.bindValue(":source", QString::fromStdString(source));
        if (!q2.exec()) {
            Logger::Log(LogLevel::ERROR, "DB: Failed to clear PlayQueue for source " + source + ": " + q2.lastError().text().toStdString());
        }

        Logger::Log(LogLevel::INFO, "DB: Cleared tracks and queue for source: " + source);
    }

    QSqlQuery qClean(m_db);
    qClean.exec("DELETE FROM PlaylistTracks WHERE track_id NOT IN (SELECT id FROM Tracks)");
}

std::vector<Track> DatabaseManager::LoadAllSourcesTracks() {
    std::vector<Track> tracks;
    QSqlQuery query(m_db);
    // Порядок согласно списку source: 1 - VK, 2 - Spotify, 3 - SoundCloud, 4 - Yandex, 5 - YouTube
    query.prepare(
        "SELECT id, artist, title, duration, cover_url, lyrics_id, lyrics, source "
        "FROM Tracks "
        "ORDER BY CASE source "
        "    WHEN 'VK' THEN 1 "
        "    WHEN 'Spotify' THEN 2 "
        "    WHEN 'SoundCloud' THEN 3 "
        "    WHEN 'Yandex' THEN 4 "
        "    WHEN 'YouTube' THEN 5 "
        "    ELSE 6 END, rowid ASC"
    );

    if (query.exec()) {
        while (query.next()) {
            Track t;
            t.id = query.value(0).toString().toStdString();
            t.artist = query.value(1).toString().toStdString();
            t.title = query.value(2).toString().toStdString();

            QString durationStr = query.value(3).toString();
            QStringList parts = durationStr.split(':');
            if (parts.size() == 2) {
                t.duration = parts[0].toInt() * 60 + parts[1].toInt();
            } else {
                t.duration = durationStr.toInt();
            }

            t.coverUrl = query.value(4).toString().toStdString();
            t.lyrics_id = query.value(5).toString().toStdString();
            t.lyrics = query.value(6).toString().toStdString();
            t.source = query.value(7).toString().toStdString();
            tracks.push_back(t);
        }
    }
    return tracks;
}

bool DatabaseManager::CreatePlaylist(const std::string& name) {
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

bool DatabaseManager::DeletePlaylist(int playlistId) {
    QSqlQuery q1(m_db);
    q1.prepare("DELETE FROM PlaylistTracks WHERE playlist_id = :id");
    q1.bindValue(":id", playlistId);
    q1.exec();

    QSqlQuery q2(m_db);
    q2.prepare("DELETE FROM Playlists WHERE id = :id");
    q2.bindValue(":id", playlistId);
    return q2.exec();
}

bool DatabaseManager::DeletePlaylist(const std::string& name) {
    QSqlQuery query(m_db);
    query.prepare("SELECT id FROM Playlists WHERE name = :name COLLATE NOCASE");
    query.bindValue(":name", QString::fromStdString(name));
    if (query.exec() && query.next()) {
        int id = query.value(0).toInt();
        return DeletePlaylist(id);
    }
    return false;
}

std::vector<PlaylistInfo> DatabaseManager::GetPlaylists() {
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

bool DatabaseManager::AddTrackToPlaylist(int playlistId, const std::string& trackId) {
    if (trackId.empty() || playlistId <= 0) return false;

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

bool DatabaseManager::RemoveTrackFromPlaylist(int playlistId, int position) {
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

std::vector<Track> DatabaseManager::LoadPlaylistTracks(int playlistId) {
    std::vector<Track> tracks;
    QSqlQuery query(m_db);
    query.prepare("SELECT t.id, t.artist, t.title, t.duration, t.cover_url, t.lyrics_id, t.lyrics, t.source "
                  "FROM PlaylistTracks pt "
                  "JOIN Tracks t ON pt.track_id = t.id "
                  "WHERE pt.playlist_id = :pid "
                  "ORDER BY pt.position ASC");
    query.bindValue(":pid", playlistId);
    if (query.exec()) {
        while (query.next()) {
            Track t;
            t.id = query.value(0).toString().toStdString();
            t.artist = query.value(1).toString().toStdString();
            t.title = query.value(2).toString().toStdString();

            QString durationStr = query.value(3).toString();
            QStringList parts = durationStr.split(':');
            if (parts.size() == 2) {
                t.duration = parts[0].toInt() * 60 + parts[1].toInt();
            } else {
                t.duration = durationStr.toInt();
            }

            t.coverUrl = query.value(4).toString().toStdString();
            t.lyrics_id = query.value(5).toString().toStdString();
            t.lyrics = query.value(6).toString().toStdString();
            t.source = query.value(7).toString().toStdString();
            tracks.push_back(t);
        }
    }
    return tracks;
}

std::vector<Track> DatabaseManager::LoadPlaylistTracksByName(const std::string& name, int& outId) {
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