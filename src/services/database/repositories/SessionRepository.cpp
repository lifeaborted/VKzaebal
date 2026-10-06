#include "SessionRepository.h"
#include "utils/logger/Logger.h"
#include "utils/path/PathManager.h"

#include <QThreadPool>
#include <QUuid>
#include <QJsonDocument>
#include <QJsonArray>
#include <QFile>
#include <QTextStream>
#include <QVariant>
#include <QSqlError>

SessionRepository::SessionRepository(QSqlDatabase& db)
    : m_db(db) {}

void SessionRepository::SaveQueue(const std::vector<Track>& currentQueue, const std::string& source, bool isShuffle) {
    if (source.empty()) return;

    QThreadPool::globalInstance()->start([currentQueue, source, isShuffle]() {
        QString connectionName = QUuid::createUuid().toString();
        {
            QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connectionName);
            db.setConnectOptions("QSQLITE_BUSY_TIMEOUT=5000");
            db.setDatabaseName(PathManager::GetDbPath());

            if (db.open()) {
                QSqlQuery query(db);
                QJsonArray arr;
                for (const auto& track : currentQueue) {
                    arr.append(QString::fromStdString(track.id));
                }
                QString jsonQueue = QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));

                if (isShuffle) {
                    query.prepare(
                        "INSERT INTO SourceSessions (source, shuffle_queue, updated_at) "
                        "VALUES (:source, :queue, CURRENT_TIMESTAMP) "
                        "ON CONFLICT(source) DO UPDATE SET "
                        "shuffle_queue = excluded.shuffle_queue, "
                        "updated_at = CURRENT_TIMESTAMP"
                    );
                } else {
                    query.prepare(
                        "INSERT INTO SourceSessions (source, standard_queue, updated_at) "
                        "VALUES (:source, :queue, CURRENT_TIMESTAMP) "
                        "ON CONFLICT(source) DO UPDATE SET "
                        "standard_queue = excluded.standard_queue, "
                        "updated_at = CURRENT_TIMESTAMP"
                    );
                }
                query.bindValue(":source", QString::fromStdString(source));
                query.bindValue(":queue", jsonQueue);
                query.exec();
                db.close();
            } else {
                Logger::Log(LogLevel::ERROR, "DB: Failed to open threaded connection for SaveQueue.");
            }
        }
        QSqlDatabase::removeDatabase(connectionName);
    });
}

std::vector<std::string> SessionRepository::LoadQueueIds(const std::string& source, bool isShuffle) const {
    std::vector<std::string> ids;
    if (source.empty()) return ids;

    QSqlQuery query(m_db);
    if (isShuffle) {
        query.prepare("SELECT shuffle_queue FROM SourceSessions WHERE source = :source");
    } else {
        query.prepare("SELECT standard_queue FROM SourceSessions WHERE source = :source");
    }
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

void SessionRepository::ExportQueueToTxt(const std::vector<Track>& queue, const QString& filename, bool isShuffle) const {
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

void SessionRepository::SaveSourceSession(const std::string& source, const std::string& trackId, int trackIndex, double positionSeconds) {
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

std::optional<SourceSession> SessionRepository::LoadSourceSession(const std::string& source) const {
    if (source.empty()) return std::nullopt;
    QSqlQuery query(m_db);
    query.prepare("SELECT source, track_id, track_index, position_seconds, shuffle_queue, standard_queue FROM SourceSessions WHERE source = :source");
    query.bindValue(":source", QString::fromStdString(source));
    if (query.exec() && query.next()) {
        SourceSession session;
        session.source = query.value(0).toString().toStdString();
        session.trackId = query.value(1).toString().toStdString();
        session.trackIndex = query.value(2).toInt();
        session.positionSeconds = query.value(3).toDouble();
        session.shuffleQueue = query.value(4).toString().toStdString();
        session.standardQueue = query.value(5).toString().toStdString();
        return session;
    }
    return std::nullopt;
}

void SessionRepository::ClearSourceSession(const std::string& source) {
    if (source.empty()) return;
    QSqlQuery query(m_db);
    query.prepare("DELETE FROM SourceSessions WHERE source = :source");
    query.bindValue(":source", QString::fromStdString(source));
    query.exec();
}
