#pragma once
#include <QString>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <vector>
#include <optional>
#include "models/Track.h"

struct PlaylistInfo {
    int id = 0;
    std::string name;
    int trackCount = 0;
};

struct SourceSession {
    std::string source;
    std::string trackId;
    int trackIndex = 0;
    double positionSeconds = 0.0;
    std::string shuffleQueue;
};

class DatabaseManager {
public:
    DatabaseManager();
    ~DatabaseManager();

    bool Init();

    // --- Настройки (Токен) ---
    void SetSetting(const QString& key, const QString& value);
    QString GetSetting(const QString& key) const;
    void ClearSetting(const QString& key);

    // --- Треки и очередь ---
    void SaveTracks(const std::vector<Track>& tracks);
    void SaveQueue(const std::vector<Track>& currentQueue, const std::string& source, bool isShuffle);
    std::vector<std::string> LoadQueueIds(const std::string& source, bool isShuffle) const;

    // Вывод в TXT прямо из БД
    void ExportQueueToTxt(const std::vector<Track>& queue, const QString& filename, bool isShuffle) const;
    std::vector<Track> LoadTracks(const std::string& source);
    void ClearTracksForSource(const std::string& source);

    // Общий плейлист (все источники)
    std::vector<Track> LoadAllSourcesTracks();

    // --- Пользовательские плейлисты ---
    bool CreatePlaylist(const std::string& name);
    bool DeletePlaylist(const std::string& name);
    bool DeletePlaylist(int playlistId);
    std::vector<PlaylistInfo> GetPlaylists();
    bool AddTrackToPlaylist(int playlistId, const std::string& trackId);
    bool RemoveTrackFromPlaylist(int playlistId, int position);
    std::vector<Track> LoadPlaylistTracks(int playlistId);
    std::vector<Track> LoadPlaylistTracksByName(const std::string& name, int& outId);

    // Обновление локального кэша текста
    void UpdateTrackLyrics(const std::string& trackId, const std::string& lyrics);

    // --- Сессии воспроизведения источников ---
    void SaveSourceSession(const std::string& source, const std::string& trackId, int trackIndex, double positionSeconds);
    std::optional<SourceSession> LoadSourceSession(const std::string& source) const;
    void ClearSourceSession(const std::string& source);

private:
    QSqlDatabase m_db;
    void CreateTables();
    void MigrateSchemaIfNeeded();
};