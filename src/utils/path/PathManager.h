#pragma once
#include <QString>
#include <string>

class PathManager {
public:
    static void Init();

    static QString GetAppDataDir();
    static QString GetDownloadsDir();
    static QString GetCustomDownloadsDir();
    static void SetSessionDownloadsDir(const QString& dir);
    static QString GetLyricsDir();
    static QString GetLogsDir();
    static QString GetTempDir();

    static QString GetDbPath();
    static QString GetConfigPath();
    static QString GetPlaylistExportPath(const QString& filename = QString());
    static QString GetLogFilePath();

    static QString GetDownloadFilePath(const std::string& safeFilename, const QString& ext, const QString& customDir = QString());
    static QString GetLyricsFilePath(const std::string& artist, const std::string& title, bool isNewFile = false);

    static QString GetUltimateConfigPath();

private:
    static QString s_appDataDir;
    static QString s_downloadsDir;
    static QString s_sessionDownloadsDir;
    static QString s_lyricsDir;
    static QString s_logsDir;
    static QString s_tempDir;
    static bool s_initialized;
};
