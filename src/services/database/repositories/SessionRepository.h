#pragma once
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QString>
#include <vector>
#include <string>
#include <optional>
#include "../../../models/Track.h"

struct SourceSession {
    std::string source;
    std::string trackId;
    int trackIndex = 0;
    double positionSeconds = 0.0;
    std::string shuffleQueue;
    std::string standardQueue;
};

class SessionRepository {
public:
    explicit SessionRepository(QSqlDatabase& db);

    void SaveQueue(const std::vector<Track>& currentQueue, const std::string& source, bool isShuffle);
    std::vector<std::string> LoadQueueIds(const std::string& source, bool isShuffle) const;
    void ExportQueueToTxt(const std::vector<Track>& queue, const QString& filename, bool isShuffle) const;

    void SaveSourceSession(const std::string& source, const std::string& trackId, int trackIndex, double positionSeconds);
    std::optional<SourceSession> LoadSourceSession(const std::string& source) const;
    void ClearSourceSession(const std::string& source);

private:
    QSqlDatabase& m_db;
};
