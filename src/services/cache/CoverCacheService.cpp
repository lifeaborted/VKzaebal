#include "CoverCacheService.h"
#include "utils/path/PathManager.h"
#include "utils/logger/Logger.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QCryptographicHash>
#include <QImage>
#include <QFile>
#include <QFileInfo>
#include <QThreadPool>
#include <QPointer>
#include <atomic>

namespace services::cache {

CoverCacheService& CoverCacheService::Instance() {
    static CoverCacheService s_instance;
    return s_instance;
}

CoverCacheService::CoverCacheService(QObject* parent)
    : QObject(parent)
    , m_netManager(new QNetworkAccessManager(this)) {
}

CoverCacheService::~CoverCacheService() = default;

QString CoverCacheService::GetDiskPath(const QString& remoteUrl, bool isThumbnail) const {
    QByteArray hash = QCryptographicHash::hash(remoteUrl.toUtf8(), QCryptographicHash::Md5).toHex();
    QString fileName = QString::fromUtf8(hash) + (isThumbnail ? "_thumb.covers" : ".covers");
    return PathManager::GetCoversDir() + "/" + fileName;
}

QString CoverCacheService::GetCoverUrl(const QString& remoteUrl, bool isThumbnail) {
    if (remoteUrl.isEmpty()) {
        return QString();
    }
    if (remoteUrl.startsWith("file:") || remoteUrl.startsWith("qrc:")) {
        return remoteUrl;
    }

    QString dedupeKey = remoteUrl + (isThumbnail ? "_t" : "_f");
    auto it = m_verifiedLocalUrls.find(dedupeKey);
    if (it != m_verifiedLocalUrls.end()) {
        return it.value();
    }

    QString diskPath = GetDiskPath(remoteUrl, isThumbnail);
    if (QFile::exists(diskPath)) {
        QString localUrl = QUrl::fromLocalFile(diskPath).toString();
        m_verifiedLocalUrls.insert(dedupeKey, localUrl);
        return localUrl;
    }

    if (!m_activeAndPendingUrls.contains(dedupeKey)) {
        m_activeAndPendingUrls.insert(dedupeKey);
        m_downloadQueue.enqueue({remoteUrl, diskPath, isThumbnail});
        ProcessDownloadQueue();
    }

    return QString();
}

void CoverCacheService::ProcessDownloadQueue() {
    while (m_activeDownloads < MAX_CONCURRENT_DOWNLOADS && !m_downloadQueue.isEmpty()) {
        DownloadTask task = m_downloadQueue.dequeue();
        StartDownload(task);
    }
}

void CoverCacheService::StartDownload(const DownloadTask& task) {
    m_activeDownloads++;

    QNetworkRequest request(QUrl(task.url));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply* reply = m_netManager->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply, task]() {
        reply->deleteLater();
        m_activeDownloads--;

        if (reply->error() == QNetworkReply::NoError) {
            QByteArray data = reply->readAll();
            if (!data.isEmpty()) {
                QPointer<CoverCacheService> safeThis(this);
                QThreadPool::globalInstance()->start([safeThis, task, data = std::move(data)]() {
                    QImage img;
                    if (img.loadFromData(data)) {
                        if (task.isThumbnail) {
                            img = img.scaled(140, 140, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                        }
                        if (img.save(task.localPath, "JPG", 85)) {
                            static std::atomic<int> s_enforceCounter{0};
                            if (++s_enforceCounter % 25 == 0) {
                                PathManager::EnforceCacheLimit(PathManager::GetCoversDir(), 50); // 50 MB max
                            }

                            if (safeThis) {
                                QMetaObject::invokeMethod(safeThis, [safeThis, task]() {
                                    if (!safeThis) return;
                                    QString localUrl = QUrl::fromLocalFile(task.localPath).toString();
                                    QString dedupeKey = task.url + (task.isThumbnail ? "_t" : "_f");
                                    safeThis->m_verifiedLocalUrls.insert(dedupeKey, localUrl);
                                    emit safeThis->CoverCached(task.url, localUrl);
                                }, Qt::QueuedConnection);
                            }
                        }
                    }
                });
            }
        }

        ProcessDownloadQueue();
    });
}

} // namespace services::cache
