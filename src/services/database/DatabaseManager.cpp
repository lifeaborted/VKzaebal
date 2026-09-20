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
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

namespace {
int ParseDurationSeconds(const QString& durationStr) {
    const QStringList parts = durationStr.split(':');
    if (parts.size() == 3) {
        return parts[0].toInt() * 3600 + parts[1].toInt() * 60 + parts[2].toInt();
    } else if (parts.size() == 2) {
        return parts[0].toInt() * 60 + parts[1].toInt();
    } else if (parts.size() == 1) {
        return parts[0].toInt();
    }
    return 0;
}
}

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
    QSqlQuery pragma(m_db);
    pragma.exec("PRAGMA journal_mode = WAL;");
    pragma.exec("PRAGMA synchronous = NORMAL;");
    pragma.exec("PRAGMA busy_timeout = 5000;");
    pragma.exec("PRAGMA cache_size = -2000;");

    MigrateSchemaIfNeeded();
    CreateTables();
    return true;
}

void DatabaseManager::MigrateSchemaIfNeeded() {
    QSqlQuery checkQuery("PRAGMA table_info(Tracks)", m_db);
    bool hasExternalId = false;
    bool hasIntDuration = false;
    bool tableExists = false;

    while (checkQuery.next()) {
        tableExists = true;
        QString colName = checkQuery.value(1).toString();
        QString colType = checkQuery.value(2).toString().toUpper();
        if (colName == "external_id") hasExternalId = true;
        if (colName == "duration" && colType.contains("INT")) hasIntDuration = true;
    }

    if (tableExists && (!hasExternalId || !hasIntDuration)) {
        Logger::Log(LogLevel::INFO, "DB: Migrating Tracks table to new schema (INTEGER PK + external_id + INTEGER duration)...");

        m_db.transaction();
        QSqlQuery q(m_db);

        q.exec("ALTER TABLE Tracks RENAME TO Tracks_old;");

        q.exec("CREATE TABLE IF NOT EXISTS Tracks ("
               "id INTEGER PRIMARY KEY AUTOINCREMENT, "
               "source TEXT NOT NULL, "
               "external_id TEXT NOT NULL, "
               "artist TEXT, "
               "title TEXT, "
               "duration INTEGER DEFAULT 0, "
               "cover_url TEXT, "
               "lyrics_id TEXT, "
               "lyrics TEXT, "
               "UNIQUE(source, external_id))");

        q.exec("CREATE INDEX IF NOT EXISTS idx_tracks_source_id ON Tracks(source, id)");
        q.exec("CREATE INDEX IF NOT EXISTS idx_tracks_external_id ON Tracks(external_id)");

        QSqlQuery selectOld("SELECT id, source, artist, title, duration, cover_url, lyrics_id, lyrics FROM Tracks_old", m_db);
        QSqlQuery insertNew(m_db);
        insertNew.prepare("INSERT OR IGNORE INTO Tracks (source, external_id, artist, title, duration, cover_url, lyrics_id, lyrics) "
                          "VALUES (:source, :external_id, :artist, :title, :duration, :cover_url, :lyrics_id, :lyrics)");

        int migratedCount = 0;
        while (selectOld.next()) {
            QString extId = selectOld.value(0).toString();
            QString src = selectOld.value(1).toString();
            QString artist = selectOld.value(2).toString();
            QString title = selectOld.value(3).toString();
            QString durStr = selectOld.value(4).toString();
            int durSec = ParseDurationSeconds(durStr);
            QString cover = selectOld.value(5).toString();
            QString lyricsId = selectOld.value(6).toString();
            QString lyrics = selectOld.value(7).toString();

            insertNew.bindValue(":source", src);
            insertNew.bindValue(":external_id", extId);
            insertNew.bindValue(":artist", artist);
            insertNew.bindValue(":title", title);
            insertNew.bindValue(":duration", durSec);
            insertNew.bindValue(":cover_url", cover);
            insertNew.bindValue(":lyrics_id", lyricsId);
            insertNew.bindValue(":lyrics", lyrics);
            insertNew.exec();
            migratedCount++;
        }

        q.exec("DROP TABLE Tracks_old;");
        m_db.commit();

        Logger::Log(LogLevel::INFO, "DB: Successfully migrated " + std::to_string(migratedCount) + " tracks to new schema.");
    }

    // Проверяем наличие shuffle_queue в SourceSessions
    QSqlQuery checkSessions("PRAGMA table_info(SourceSessions)", m_db);
    bool hasShuffleQueue = false;
    bool sessionsExists = false;
    while (checkSessions.next()) {
        sessionsExists = true;
        if (checkSessions.value(1).toString() == "shuffle_queue") {
            hasShuffleQueue = true;
        }
    }
    if (sessionsExists && !hasShuffleQueue) {
        QSqlQuery q(m_db);
        q.exec("ALTER TABLE SourceSessions ADD COLUMN shuffle_queue TEXT;");
    }
}

void DatabaseManager::CreateTables() {
    QSqlQuery query(m_db);
    query.exec("CREATE TABLE IF NOT EXISTS Settings (key TEXT PRIMARY KEY, value TEXT)");

    query.exec("CREATE TABLE IF NOT EXISTS Tracks ("
               "id INTEGER PRIMARY KEY AUTOINCREMENT, "
               "source TEXT NOT NULL, "
               "external_id TEXT NOT NULL, "
               "artist TEXT, "
               "title TEXT, "
               "duration INTEGER DEFAULT 0, "
               "cover_url TEXT, "
               "lyrics_id TEXT, "
               "lyrics TEXT, "
               "UNIQUE(source, external_id))");

    query.exec("CREATE INDEX IF NOT EXISTS idx_tracks_source_id ON Tracks(source, id)");
    query.exec("CREATE INDEX IF NOT EXISTS idx_tracks_external_id ON Tracks(external_id)");
    query.exec("DROP INDEX IF EXISTS idx_tracks_source");
    query.exec("DROP INDEX IF EXISTS idx_tracks_lookup");

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
               "FOREIGN KEY(playlist_id) REFERENCES Playlists(id) ON DELETE CASCADE)");

    query.exec("DROP INDEX IF EXISTS idx_playlist_tracks_lookup");

    query.exec("CREATE TABLE IF NOT EXISTS SourceSessions ("
               "source TEXT PRIMARY KEY, "
               "track_id TEXT, "
               "track_index INTEGER, "
               "position_seconds REAL, "
               "shuffle_queue TEXT, "
               "updated_at DATETIME DEFAULT CURRENT_TIMESTAMP)");
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
    if (source.empty()) return;

    QThreadPool::globalInstance()->start([currentQueue, source, isShuffle]() {
        QString connectionName = QUuid::createUuid().toString();
        {
            QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connectionName);
            db.setConnectOptions("QSQLITE_BUSY_TIMEOUT=5000");
            db.setDatabaseName(PathManager::GetDbPath());

            if (db.open()) {
                QSqlQuery query(db);
                if (isShuffle) {
                    QJsonArray arr;
                    for (const auto& track : currentQueue) {
                        arr.append(QString::fromStdString(track.id));
                    }
                    QString jsonQueue = QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));

                    query.prepare(
                        "INSERT INTO SourceSessions (source, shuffle_queue, updated_at) "
                        "VALUES (:source, :queue, CURRENT_TIMESTAMP) "
                        "ON CONFLICT(source) DO UPDATE SET "
                        "shuffle_queue = excluded.shuffle_queue, "
                        "updated_at = CURRENT_TIMESTAMP"
                    );
                    query.bindValue(":source", QString::fromStdString(source));
                    query.bindValue(":queue", jsonQueue);
                    query.exec();
                } else {
                    query.prepare("UPDATE SourceSessions SET shuffle_queue = '' WHERE source = :source");
                    query.bindValue(":source", QString::fromStdString(source));
                    query.exec();
                }
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
                query.prepare(
                    "INSERT INTO Tracks (source, external_id, artist, title, duration, cover_url, lyrics_id, lyrics) "
                    "VALUES (:source, :external_id, :artist, :title, :duration, :cover_url, :lyrics_id, :lyrics) "
                    "ON CONFLICT(source, external_id) DO UPDATE SET "
                    "artist = excluded.artist, "
                    "title = excluded.title, "
                    "duration = excluded.duration, "
                    "cover_url = excluded.cover_url, "
                    "lyrics_id = CASE WHEN excluded.lyrics_id != '' THEN excluded.lyrics_id ELSE Tracks.lyrics_id END, "
                    "lyrics = CASE WHEN excluded.lyrics != '' THEN excluded.lyrics ELSE Tracks.lyrics END"
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
    QSqlQuery query(m_db);

    if (source == "Offline") {
        query.prepare("SELECT id, external_id, artist, title, duration, cover_url, lyrics_id, lyrics, source "
                      "FROM Tracks");
    } else {
        query.prepare("SELECT id, external_id, artist, title, duration, cover_url, lyrics_id, lyrics, source "
                      "FROM Tracks "
                      "WHERE source = :source "
                      "ORDER BY id ASC");
        query.bindValue(":source", QString::fromStdString(source));
    }

    if (query.exec()) {
        tracks.reserve(512);
        while (query.next()) {
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

            tracks.push_back(std::move(t));
        }
    }

    Logger::Log(LogLevel::INFO, "DB: Loaded " + std::to_string(tracks.size()) + " tracks for source " + source);
    return tracks;
}

void DatabaseManager::UpdateTrackLyrics(const std::string& trackId, const std::string& lyrics) {
    QSqlQuery query(m_db);
    query.prepare("UPDATE Tracks SET lyrics = :lyrics WHERE external_id = :id");
    query.bindValue(":lyrics", QString::fromStdString(lyrics));
    query.bindValue(":id", QString::fromStdString(trackId));
    if (!query.exec()) {
        Logger::Log(LogLevel::ERROR, "DB: Failed to update lyrics for track " + trackId);
    }
}

std::vector<std::string> DatabaseManager::LoadQueueIds(const std::string& source, bool isShuffle) const {
    std::vector<std::string> ids;
    if (!isShuffle || source.empty()) return ids;

    QSqlQuery query(m_db);
    query.prepare("SELECT shuffle_queue FROM SourceSessions WHERE source = :source");
    query.bindValue(":source", QString::fromStdString(source));
    if (query.exec() && query.next()) {
        QString jsonStr = query.value(0).toString();
        if (!jsonStr.isEmpty()) {
            QJsonDocument doc = QJsonDocument::fromJson(jsonStr.toUtf8());
            if (doc.isArray()) {
                QJsonArray arr = doc.array();
                ids.reserve(arr.size());
                for (const auto& val : arr) {
                    ids.push_back(val.toString().toStdString());
                }
            }
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
        q2.exec("UPDATE SourceSessions SET shuffle_queue = ''");
        Logger::Log(LogLevel::INFO, "DB: Cleared all tracks and queues from database.");
    } else {
        QSqlQuery q1(m_db);
        q1.prepare("DELETE FROM Tracks WHERE source = :source");
        q1.bindValue(":source", QString::fromStdString(source));
        if (!q1.exec()) {
            Logger::Log(LogLevel::ERROR, "DB: Failed to clear Tracks for source " + source + ": " + q1.lastError().text().toStdString());
        }

        QSqlQuery q2(m_db);
        q2.prepare("UPDATE SourceSessions SET shuffle_queue = '' WHERE source = :source");
        q2.bindValue(":source", QString::fromStdString(source));
        q2.exec();

        Logger::Log(LogLevel::INFO, "DB: Cleared tracks and queue for source: " + source);
    }

    QSqlQuery qClean(m_db);
    qClean.exec("DELETE FROM PlaylistTracks WHERE track_id NOT IN (SELECT external_id FROM Tracks)");
}

std::vector<Track> DatabaseManager::LoadAllSourcesTracks() {
    std::vector<Track> tracks;
    QSqlQuery query(m_db);
    // Порядок согласно списку source: 1 - VK, 2 - Spotify, 3 - SoundCloud, 4 - Yandex, 5 - YouTube
    query.prepare(
        "SELECT id, external_id, artist, title, duration, cover_url, lyrics_id, lyrics, source "
        "FROM Tracks "
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
            tracks.push_back(std::move(t));
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
    std::string plName;
    {
        QSqlQuery q(m_db);
        q.prepare("SELECT name FROM Playlists WHERE id = :id");
        q.bindValue(":id", playlistId);
        if (q.exec() && q.next()) {
            plName = q.value(0).toString().toStdString();
        }
    }
    if (!plName.empty()) {
        ClearSourceSession("Custom:" + plName);
    }

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
    query.prepare("SELECT t.id, t.external_id, t.artist, t.title, t.duration, t.cover_url, t.lyrics_id, t.lyrics, t.source "
                  "FROM PlaylistTracks pt "
                  "JOIN Tracks t ON pt.track_id = t.external_id "
                  "WHERE pt.playlist_id = :pid "
                  "ORDER BY pt.position ASC");
    query.bindValue(":pid", playlistId);
    if (query.exec()) {
        tracks.reserve(128);
        while (query.next()) {
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
            tracks.push_back(std::move(t));
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

void DatabaseManager::SaveSourceSession(const std::string& source, const std::string& trackId, int trackIndex, double positionSeconds) {
    if (source.empty()) return;
    QSqlQuery query(m_db);
    query.prepare("INSERT INTO SourceSessions (source, track_id, track_index, position_seconds, updated_at) "
                  "VALUES (:source, :track_id, :track_index, :position_seconds, CURRENT_TIMESTAMP) "
                  "ON CONFLICT(source) DO UPDATE SET "
                  "track_id = excluded.track_id, "
                  "track_index = excluded.track_index, "
                  "position_seconds = excluded.position_seconds, "
                  "updated_at = CURRENT_TIMESTAMP");
    query.bindValue(":source", QString::fromStdString(source));
    query.bindValue(":track_id", QString::fromStdString(trackId));
    query.bindValue(":track_index", trackIndex);
    query.bindValue(":position_seconds", positionSeconds > 0.0 ? positionSeconds : 0.0);
    if (!query.exec()) {
        Logger::Log(LogLevel::WARNING, "DB: Failed to save source session for '" + source + "': " + query.lastError().text().toStdString());
    }
}

std::optional<SourceSession> DatabaseManager::LoadSourceSession(const std::string& source) const {
    if (source.empty()) return std::nullopt;
    QSqlQuery query(m_db);
    query.prepare("SELECT source, track_id, track_index, position_seconds, shuffle_queue FROM SourceSessions WHERE source = :source");
    query.bindValue(":source", QString::fromStdString(source));
    if (query.exec() && query.next()) {
        SourceSession session;
        session.source = query.value(0).toString().toStdString();
        session.trackId = query.value(1).toString().toStdString();
        session.trackIndex = query.value(2).toInt();
        session.positionSeconds = query.value(3).toDouble();
        session.shuffleQueue = query.value(4).toString().toStdString();
        return session;
    }
    return std::nullopt;
}

void DatabaseManager::ClearSourceSession(const std::string& source) {
    if (source.empty()) return;
    QSqlQuery query(m_db);
    query.prepare("DELETE FROM SourceSessions WHERE source = :source");
    query.bindValue(":source", QString::fromStdString(source));
    query.exec();
}