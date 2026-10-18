#pragma once

#include <QObject>
#include <QString>
#include <QSet>
#include <QQueue>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;

namespace services::cache {

class CoverCacheService : public QObject {
    Q_OBJECT
public:
    static CoverCacheService& Instance();

    explicit CoverCacheService(QObject* parent = nullptr);
    ~CoverCacheService() override;

    /// Возвращает локальный URL (file://...) если обложка уже сохранена в кэш,
    /// либо возвращает исходный remoteUrl и асинхронно ставит загрузку в очередь.
    QString GetCoverUrl(const QString& remoteUrl, bool isThumbnail = false);

signals:
    void CoverCached(const QString& remoteUrl, const QString& localFileUrl);

private slots:
    void ProcessDownloadQueue();

private:
    struct DownloadTask {
        QString url;
        QString localPath;
        bool isThumbnail;
    };

    QString GetDiskPath(const QString& remoteUrl, bool isThumbnail) const;
    void StartDownload(const DownloadTask& task);

    QNetworkAccessManager* m_netManager{nullptr};
    QSet<QString> m_activeAndPendingUrls;
    QHash<QString, QString> m_verifiedLocalUrls;
    QQueue<DownloadTask> m_downloadQueue;
    int m_activeDownloads{0};
    static constexpr int MAX_CONCURRENT_DOWNLOADS = 4;
};

} // namespace services::cache
