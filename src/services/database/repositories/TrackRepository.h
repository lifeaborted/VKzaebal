#pragma once
#include <QSqlDatabase>
#include <QSqlQuery>
#include <vector>
#include <string>
#include "../../../models/Track.h"

class TrackRepository {
public:
    explicit TrackRepository(QSqlDatabase& db);

    void SaveTracks(const std::vector<Track>& tracks, bool isLibrary = true);
    void SetTrackIsLibrary(const std::string& trackId, bool isLibrary);
    std::vector<Track> LoadTracks(const std::string& source, const std::vector<std::string>& standardOrder = {});
    std::vector<Track> LoadAllSourcesTracks();
    void ClearTracksForSource(const std::string& source);
    void UpdateTrackLyrics(const std::string& trackId, const std::string& lyrics);

    static Track TrackFromSqlRecord(const QSqlQuery& query);

private:
    QSqlDatabase& m_db;
};
