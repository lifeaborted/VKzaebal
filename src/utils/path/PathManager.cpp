#include "PathManager.h"
#include <QStandardPaths>
#include <QDir>
#include <QRegularExpression>
#include <QCoreApplication>

QString PathManager::s_appDataDir;
QString PathManager::s_downloadsDir;
QString PathManager::s_lyricsDir;
QString PathManager::s_logsDir;
bool PathManager::s_initialized = false;

void PathManager::Init() {
    if (s_initialized) return;

#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    s_appDataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (s_appDataDir.isEmpty()) {
        s_appDataDir = QDir::currentPath() + "/VKAudioPlayer";
    }
#else
    // На десктопе сохраняем портативность: работаем в текущей директории приложения
    s_appDataDir = QDir::currentPath();
#endif

    QDir appDir(s_appDataDir);
    if (!appDir.exists()) {
        appDir.mkpath(".");
    }

    s_downloadsDir = s_appDataDir + "/downloads";
    s_lyricsDir = s_appDataDir + "/lyrics";
    s_logsDir = s_appDataDir + "/logs";

    QDir().mkpath(s_downloadsDir);
    QDir().mkpath(s_lyricsDir);
    QDir().mkpath(s_logsDir);

    s_initialized = true;
}

QString PathManager::GetAppDataDir() {
    if (!s_initialized) Init();
    return s_appDataDir;
}

QString PathManager::GetDownloadsDir() {
    if (!s_initialized) Init();
    return s_downloadsDir;
}

QString PathManager::GetLyricsDir() {
    if (!s_initialized) Init();
    return s_lyricsDir;
}

QString PathManager::GetLogsDir() {
    if (!s_initialized) Init();
    return s_logsDir;
}

QString PathManager::GetDbPath() {
    return GetAppDataDir() + "/player_data.db";
}

QString PathManager::GetConfigPath() {
    return GetAppDataDir() + "/config.ini";
}

QString PathManager::GetPlaylistExportPath(const QString& filename) {
    QString fname = filename.isEmpty() ? "playlist.txt" : filename;
    return GetAppDataDir() + "/" + fname;
}

QString PathManager::GetLogFilePath() {
    return GetLogsDir() + "/app.log";
}

QString PathManager::GetDownloadFilePath(const std::string& safeFilename, const QString& ext) {
    QString extension = ext.startsWith('.') ? ext : ("." + ext);
    return GetDownloadsDir() + "/" + QString::fromStdString(safeFilename) + extension;
}

QString PathManager::GetLyricsFilePath(const std::string& artist, const std::string& title, bool isNewFile) {
    if (isNewFile) {
        QString safeArtist = QString::fromStdString(artist).replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");
        QString safeTitle = QString::fromStdString(title).replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");
        return GetLyricsDir() + "/" + safeArtist + " - " + safeTitle + ".txt";
    }
    return GetLyricsDir() + "/lyric.txt";
}