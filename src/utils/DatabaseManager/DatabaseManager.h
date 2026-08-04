#pragma once
#include <QString>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <vector>
#include "core/vk/Track.h"

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
    void SaveQueue(const std::vector<Track>& currentQueue, bool isShuffle);
    
    // Вывод в TXT прямо из БД
    void ExportQueueToTxt(const QString& filename, bool isShuffle) const;
    std::vector<Track> LoadTracks();

private:
    QSqlDatabase m_db;
    void CreateTables();
};