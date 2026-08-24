#pragma once
#include <QString>
#include <string>

class PathManager {
public:
    static void Init();

    static QString GetAppDataDir();
    static QString GetDownloadsDir();
    static QString GetLyricsDir();
    static QString GetLogsDir();

    static QString GetDbPath();
    static QString GetConfigPath();
    static QString GetPlaylistExportPath(const QString& filename = QString());
    static QString GetLogFilePath();

    static QString GetDownloadFilePath(const std::string& safeFilename, const QString& ext);
    static QString GetLyricsFilePath(const std::string& artist, const std::string& title, bool isNewFile = false);

    static QString GetCavaConfigPath();

private:
    static QString s_appDataDir;
    static QString s_downloadsDir;
    static QString s_lyricsDir;
    static QString s_logsDir;
    static bool s_initialized;
};
