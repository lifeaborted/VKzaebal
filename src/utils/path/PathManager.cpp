#include "PathManager.h"
#include <QStandardPaths>
#include <QDir>
#include <QRegularExpression>
#include <QCoreApplication>
#include <QSettings>

QString PathManager::s_appDataDir;
QString PathManager::s_downloadsDir;
QString PathManager::s_sessionDownloadsDir;
QString PathManager::s_lyricsDir;
QString PathManager::s_logsDir;
QString PathManager::s_tempDir;
QString PathManager::s_cachedCustomDownloadsDir;
bool PathManager::s_customDownloadsDirCached = false;
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
    s_tempDir = s_appDataDir + "/temp";

    QDir().mkpath(s_downloadsDir);
    QDir().mkpath(s_lyricsDir);
    QDir().mkpath(s_logsDir);
    QDir().mkpath(s_tempDir);

    s_initialized = true;
}

QString PathManager::GetAppDataDir() {
    if (!s_initialized) Init();
    return s_appDataDir;
}

QString PathManager::GetCustomDownloadsDir() {
    if (s_customDownloadsDirCached) {
        return s_cachedCustomDownloadsDir;
    }
    QSettings settings(GetConfigPath(), QSettings::IniFormat);
    QString path = settings.value("Downloads/Path", "").toString().trimmed();
    if (!path.isEmpty()) {
        s_cachedCustomDownloadsDir = QDir::cleanPath(path);
    } else {
        s_cachedCustomDownloadsDir.clear();
    }
    s_customDownloadsDirCached = true;
    return s_cachedCustomDownloadsDir;
}

void PathManager::InvalidateConfigCache() {
    s_customDownloadsDirCached = false;
    s_cachedCustomDownloadsDir.clear();
}

void PathManager::SetSessionDownloadsDir(const QString& dir) {
    if (!dir.isEmpty()) {
        s_sessionDownloadsDir = QDir::cleanPath(dir.trimmed());
    } else {
        s_sessionDownloadsDir.clear();
    }
}

QString PathManager::GetDownloadsDir() {
    if (!s_initialized) Init();
    QString custom = GetCustomDownloadsDir();
    if (!custom.isEmpty()) {
        QDir().mkpath(custom);
        return custom;
    }
    if (!s_sessionDownloadsDir.isEmpty()) {
        QDir().mkpath(s_sessionDownloadsDir);
        return s_sessionDownloadsDir;
    }
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

QString PathManager::GetTempDir() {
    if (!s_initialized) Init();
    return s_tempDir;
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

QString PathManager::GetDownloadFilePath(const std::string& safeFilename, const QString& ext, const QString& customDir) {
    QString extension = ext.startsWith('.') ? ext : ("." + ext);
    if (!customDir.isEmpty()) {
        return QDir::cleanPath(customDir) + "/" + QString::fromStdString(safeFilename) + extension;
    }

    QString cfgDir = GetCustomDownloadsDir();
    if (!cfgDir.isEmpty()) {
        return cfgDir + "/" + QString::fromStdString(safeFilename) + extension;
    }

    if (!s_sessionDownloadsDir.isEmpty()) {
        QString sessionPath = s_sessionDownloadsDir + "/" + QString::fromStdString(safeFilename) + extension;
        if (QFile::exists(sessionPath)) {
            return sessionPath;
        }
    }

    QString defaultPath = s_downloadsDir + "/" + QString::fromStdString(safeFilename) + extension;
    if (QFile::exists(defaultPath)) {
        return defaultPath;
    }

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

QString PathManager::GetUltimateConfigPath() {
    return GetAppDataDir() + "/ultimate.ini";
}