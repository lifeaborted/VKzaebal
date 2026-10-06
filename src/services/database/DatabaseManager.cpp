#include "DatabaseManager.h"
#include "utils/logger/Logger.h"
#include "utils/path/PathManager.h"

#include <QSqlError>
#include <QVariant>
#include <QStringList>

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

    m_trackRepo = std::make_unique<TrackRepository>(m_db);
    m_playlistRepo = std::make_unique<PlaylistRepository>(m_db);
    m_sessionRepo = std::make_unique<SessionRepository>(m_db);
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

    // Проверяем наличие shuffle_queue и standard_queue в SourceSessions
    QSqlQuery checkSessions("PRAGMA table_info(SourceSessions)", m_db);
    bool hasShuffleQueue = false;
    bool hasStandardQueue = false;
    bool sessionsExists = false;
    while (checkSessions.next()) {
        sessionsExists = true;
        QString col = checkSessions.value(1).toString();
        if (col == "shuffle_queue") {
            hasShuffleQueue = true;
        } else if (col == "standard_queue") {
            hasStandardQueue = true;
        }
    }
    if (sessionsExists && !hasShuffleQueue) {
        QSqlQuery q(m_db);
        q.exec("ALTER TABLE SourceSessions ADD COLUMN shuffle_queue TEXT;");
    }
    if (sessionsExists && !hasStandardQueue) {
        QSqlQuery q(m_db);
        q.exec("ALTER TABLE SourceSessions ADD COLUMN standard_queue TEXT;");
    }

    // Проверяем наличие is_library в Tracks
    QSqlQuery checkTracksLib("PRAGMA table_info(Tracks)", m_db);
    bool hasIsLibrary = false;
    bool tracksTableExists = false;
    while (checkTracksLib.next()) {
        tracksTableExists = true;
        if (checkTracksLib.value(1).toString() == "is_library") {
            hasIsLibrary = true;
        }
    }
    if (tracksTableExists && !hasIsLibrary) {
        QSqlQuery q(m_db);
        q.exec("ALTER TABLE Tracks ADD COLUMN is_library INTEGER DEFAULT 1;");
        // Треки, которые числятся в пользовательских плейлистах, но не были зафиксированы как библиотека
        q.exec("UPDATE Tracks SET is_library = 0 WHERE external_id IN (SELECT track_id FROM PlaylistTracks) "
               "AND external_id NOT IN (SELECT track_id FROM SourceSessions);");
        Logger::Log(LogLevel::INFO, "DB: Added is_library column to Tracks table.");
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
               "is_library INTEGER DEFAULT 1, "
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
               "standard_queue TEXT, "
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

void DatabaseManager::SaveTracks(const std::vector<Track>& tracks, bool isLibrary) {
    m_trackRepo->SaveTracks(tracks, isLibrary);
}

void DatabaseManager::SetTrackIsLibrary(const std::string& trackId, bool isLibrary) {
    m_trackRepo->SetTrackIsLibrary(trackId, isLibrary);
}

Track DatabaseManager::TrackFromSqlRecord(const QSqlQuery& query) {
    return TrackRepository::TrackFromSqlRecord(query);
}

std::vector<Track> DatabaseManager::LoadTracks(const std::string& source) {
    std::vector<std::string> standardOrder;
    if (source != "Offline") {
        standardOrder = m_sessionRepo->LoadQueueIds(source, false);
    }
    return m_trackRepo->LoadTracks(source, standardOrder);
}

void DatabaseManager::UpdateTrackLyrics(const std::string& trackId, const std::string& lyrics) {
    m_trackRepo->UpdateTrackLyrics(trackId, lyrics);
}

void DatabaseManager::ClearTracksForSource(const std::string& source) {
    m_trackRepo->ClearTracksForSource(source);
    if (source == "all" || source == "ALL") {
        QSqlQuery q2(m_db);
        q2.exec("UPDATE SourceSessions SET shuffle_queue = '', standard_queue = ''");
        Logger::Log(LogLevel::INFO, "DB: Cleared all tracks and queues from database.");
    } else {
        QSqlQuery q2(m_db);
        q2.prepare("UPDATE SourceSessions SET shuffle_queue = '', standard_queue = '' WHERE source = :source");
        q2.bindValue(":source", QString::fromStdString(source));
        q2.exec();
        Logger::Log(LogLevel::INFO, "DB: Cleared tracks and queue for source: " + source);
    }
}

std::vector<Track> DatabaseManager::LoadAllSourcesTracks() {
    return m_trackRepo->LoadAllSourcesTracks();
}

void DatabaseManager::SaveQueue(const std::vector<Track>& currentQueue, const std::string& source, bool isShuffle) {
    m_sessionRepo->SaveQueue(currentQueue, source, isShuffle);
}

std::vector<std::string> DatabaseManager::LoadQueueIds(const std::string& source, bool isShuffle) const {
    return m_sessionRepo->LoadQueueIds(source, isShuffle);
}

void DatabaseManager::ExportQueueToTxt(const std::vector<Track>& queue, const QString& filename, bool isShuffle) const {
    m_sessionRepo->ExportQueueToTxt(queue, filename, isShuffle);
}

bool DatabaseManager::CreatePlaylist(const std::string& name) {
    return m_playlistRepo->CreatePlaylist(name);
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
        m_sessionRepo->ClearSourceSession("Custom:" + plName);
    }
    return m_playlistRepo->DeletePlaylist(playlistId);
}

bool DatabaseManager::DeletePlaylist(const std::string& name) {
    if (!name.empty()) {
        m_sessionRepo->ClearSourceSession("Custom:" + name);
    }
    return m_playlistRepo->DeletePlaylist(name);
}

std::vector<PlaylistInfo> DatabaseManager::GetPlaylists() {
    return m_playlistRepo->GetPlaylists();
}

bool DatabaseManager::AddTrackToPlaylist(int playlistId, const std::string& trackId) {
    return m_playlistRepo->AddTrackToPlaylist(playlistId, trackId);
}

bool DatabaseManager::IsTrackInPlaylist(int playlistId, const std::string& trackId) {
    return m_playlistRepo->IsTrackInPlaylist(playlistId, trackId);
}

std::vector<int> DatabaseManager::GetPlaylistIdsContainingTrack(const std::string& trackId) {
    return m_playlistRepo->GetPlaylistIdsContainingTrack(trackId);
}

bool DatabaseManager::RemoveTrackFromPlaylist(int playlistId, int position) {
    return m_playlistRepo->RemoveTrackFromPlaylist(playlistId, position);
}

std::vector<Track> DatabaseManager::LoadPlaylistTracks(int playlistId) {
    return m_playlistRepo->LoadPlaylistTracks(playlistId);
}

std::vector<Track> DatabaseManager::LoadPlaylistTracksByName(const std::string& name, int& outId) {
    return m_playlistRepo->LoadPlaylistTracksByName(name, outId);
}

void DatabaseManager::SaveSourceSession(const std::string& source, const std::string& trackId, int trackIndex, double positionSeconds) {
    m_sessionRepo->SaveSourceSession(source, trackId, trackIndex, positionSeconds);
}

std::optional<SourceSession> DatabaseManager::LoadSourceSession(const std::string& source) const {
    return m_sessionRepo->LoadSourceSession(source);
}

void DatabaseManager::ClearSourceSession(const std::string& source) {
    m_sessionRepo->ClearSourceSession(source);
}