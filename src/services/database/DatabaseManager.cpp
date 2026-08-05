#include "DatabaseManager.h"
#include "utils/logger/Logger.h"
#include <QSqlError>
#include <QVariant>
#include <QFile>
#include <QTextStream>
#include <QStringList>

DatabaseManager::DatabaseManager() {
    m_db = QSqlDatabase::addDatabase("QSQLITE");
    m_db.setDatabaseName("player_data.db");
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
               "vk_id TEXT PRIMARY KEY, "
               "artist TEXT, "
               "title TEXT, "
               "duration TEXT, "
               "cover_url TEXT, "
               "lyrics_id TEXT, "
               "lyrics TEXT)");

    query.exec("ALTER TABLE Tracks ADD COLUMN lyrics_id TEXT");
    query.exec("ALTER TABLE Tracks ADD COLUMN lyrics TEXT");

    query.exec("CREATE TABLE IF NOT EXISTS PlayQueue ("
               "position INTEGER, "
               "vk_id TEXT, "
               "is_shuffle INTEGER, "
               "PRIMARY KEY(position, is_shuffle))");
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

void DatabaseManager::ExportQueueToTxt(const QString& filename, bool isShuffle) const {
    QSqlQuery query;
    query.prepare("SELECT q.position, t.artist, t.title, t.duration "
                  "FROM PlayQueue q "
                  "JOIN Tracks t ON q.vk_id = t.vk_id "
                  "WHERE q.is_shuffle = :shuffle "
                  "ORDER BY q.position ASC");
    query.bindValue(":shuffle", isShuffle ? 1 : 0);

    if (!query.exec()) {
        Logger::Log(LogLevel::ERROR, "DB: Failed to generate playlist export.");
        return;
    }

    QFile file(filename);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
        out << "=== ТЕКУЩИЙ ПЛЕЙЛИСТ ===\n";
        out << "Режим: " << (isShuffle ? "SHUFFLE" : "СТАНДАРТНЫЙ") << "\n";
        out << "-------------------------\n\n";

        while (query.next()) {
            int pos = query.value(0).toInt() + 1;
            QString artist = query.value(1).toString();
            QString title = query.value(2).toString();
            QString duration = query.value(3).toString();

            out << "[" << pos << "]. " << artist << " - " << title << " [" << duration << "]\n";
        }
        file.close();
        Logger::Log(LogLevel::INFO, "Main: Playlist exported to " + filename.toStdString());
    }
}

void DatabaseManager::SaveTracks(const std::vector<Track>& tracks) {
    QSqlQuery query;
    m_db.transaction();

    query.prepare("INSERT OR REPLACE INTO Tracks (vk_id, artist, title, duration, cover_url, lyrics_id, lyrics) "
                  "VALUES (:id, :artist, :title, :duration, :cover_url, :lyrics_id, :lyrics)");

    for (const auto& track : tracks) {
        query.bindValue(":id", QString::fromStdString(track.id));
        query.bindValue(":artist", QString::fromStdString(track.artist));
        query.bindValue(":title", QString::fromStdString(track.title));
        query.bindValue(":duration", QString::fromStdString(track.GetFormattedDuration()));
        query.bindValue(":cover_url", QString::fromStdString(track.coverUrl));
        query.bindValue(":lyrics_id", QString::fromStdString(track.lyrics_id));
        query.bindValue(":lyrics", QString::fromStdString(track.lyrics));
        query.exec();
    }
    m_db.commit();
}

void DatabaseManager::SaveQueue(const std::vector<Track>& currentQueue, bool isShuffle) {
    QSqlQuery query;
    m_db.transaction();

    query.prepare("DELETE FROM PlayQueue WHERE is_shuffle = :is_shuffle");
    query.bindValue(":is_shuffle", isShuffle ? 1 : 0);
    query.exec();

    query.prepare("INSERT INTO PlayQueue (position, vk_id, is_shuffle) VALUES (:pos, :id, :shuffle)");
    for (size_t i = 0; i < currentQueue.size(); ++i) {
        query.bindValue(":pos", static_cast<int>(i));
        query.bindValue(":id", QString::fromStdString(currentQueue[i].id));
        query.bindValue(":shuffle", isShuffle ? 1 : 0);
        query.exec();
    }
    m_db.commit();
}

std::vector<Track> DatabaseManager::LoadTracks() {
    std::vector<Track> tracks;
    QSqlQuery query("SELECT vk_id, artist, title, duration, cover_url, lyrics_id, lyrics FROM Tracks");

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

        tracks.push_back(t);
    }

    Logger::Log(LogLevel::INFO, "DB: Loaded " + std::to_string(tracks.size()) + " tracks from local cache.");
    return tracks;
}

void DatabaseManager::UpdateTrackLyrics(const std::string& trackId, const std::string& lyrics) {
    QSqlQuery query;
    query.prepare("UPDATE Tracks SET lyrics = :lyrics WHERE vk_id = :id");
    query.bindValue(":lyrics", QString::fromStdString(lyrics));
    query.bindValue(":id", QString::fromStdString(trackId));
    if (!query.exec()) {
        Logger::Log(LogLevel::ERROR, "DB: Failed to update lyrics for track " + trackId);
    }
}