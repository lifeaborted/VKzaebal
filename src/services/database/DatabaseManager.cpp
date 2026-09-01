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