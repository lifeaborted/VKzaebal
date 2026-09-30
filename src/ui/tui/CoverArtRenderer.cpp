#include "CoverArtRenderer.h"
#include "utils/path/PathManager.h"
#include "utils/logger/Logger.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QUrl>
#include <QCoreApplication>
#include <ftxui/dom/elements.hpp>

namespace tui {

CoverArtRenderer::CoverArtRenderer(QNetworkAccessManager* netManager, QObject* parent)
    : QObject(parent), m_netManager(netManager) {
    QDir().mkpath(PathManager::GetCacheDir() + "/covers");
}

CoverArtRenderer::~CoverArtRenderer() {
    CancelActiveRequest();
}

void CoverArtRenderer::CancelActiveRequest() {
    if (m_activeReply) {
        m_activeReply->disconnect();
        m_activeReply->abort();
        m_activeReply->deleteLater();
        m_activeReply = nullptr;
    }
}

QString CoverArtRenderer::GetCachePathForUrl(const std::string& url) const {
    QByteArray hash = QCryptographicHash::hash(QByteArray::fromStdString(url), QCryptographicHash::Sha1).toHex();
    return PathManager::GetCacheDir() + "/covers/" + QString::fromUtf8(hash) + ".png";
}

void CoverArtRenderer::PutInCache(const std::string& url, const QImage& img) {
    auto it = m_imageCache.find(url);
    if (it != m_imageCache.end()) {
        m_lruList.erase(it->second);
        m_imageCache.erase(it);
    } else if (m_lruList.size() >= MAX_CACHE_SIZE) {
        const std::string& oldestUrl = m_lruList.back().first;
        m_imageCache.erase(oldestUrl);
        m_lruList.pop_back();
    }
    m_lruList.push_front({url, img});
    m_imageCache[url] = m_lruList.begin();
}

void CoverArtRenderer::RequestCover(const std::string& url) {
    CancelActiveRequest();

    if (url.empty()) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_currentUrl.clear();
        m_hasImage = false;
        m_currentImage = QImage();
        m_elementCache.clear();
        emit CoverReady("");
        return;
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_currentUrl == url && m_hasImage) {
            return;
        }
        m_currentUrl = url;
        m_elementCache.clear();

        // Check memory cache
        auto it = m_imageCache.find(url);
        if (it != m_imageCache.end()) {
            m_lruList.splice(m_lruList.begin(), m_lruList, it->second);
            m_currentImage = it->second->second;
            m_hasImage = true;
            m_elementCache.clear();
            emit CoverReady(url);
            return;
        }
    }

    // Check disk cache
    QString diskCachePath = GetCachePathForUrl(url);
    if (QFile::exists(diskCachePath)) {
        QImage diskImg;
        if (diskImg.load(diskCachePath)) {
            QImage thumb = (diskImg.width() > 160 || diskImg.height() > 160)
                               ? diskImg.scaled(160, 160, Qt::KeepAspectRatio, Qt::SmoothTransformation)
                               : diskImg;
            if (diskImg.width() > 160 || diskImg.height() > 160) {
                thumb.save(diskCachePath, "PNG");
            }
            std::lock_guard<std::mutex> lock(m_mutex);
            PutInCache(url, thumb);
            if (m_currentUrl == url) {
                m_currentImage = thumb;
                m_hasImage = true;
                m_elementCache.clear();
                emit CoverReady(url);
            }
            return;
        }
    }

    // Check if it's a local file path
    QString localFile = QString::fromStdString(url);
    if (QFile::exists(localFile)) {
        QImage localImg;
        if (localImg.load(localFile)) {
            QImage thumb = (localImg.width() > 160 || localImg.height() > 160)
                               ? localImg.scaled(160, 160, Qt::KeepAspectRatio, Qt::SmoothTransformation)
                               : localImg;
            std::lock_guard<std::mutex> lock(m_mutex);
            PutInCache(url, thumb);
            if (m_currentUrl == url) {
                m_currentImage = thumb;
                m_hasImage = true;
                m_elementCache.clear();
                emit CoverReady(url);
            }
            return;
        }
    }

    if (!m_netManager) return;

    // Download from network asynchronously
    QUrl reqUrl(QString::fromStdString(url));
    if (!reqUrl.isValid()) return;

    QNetworkRequest request(reqUrl);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(5000);

    QNetworkReply* reply = m_netManager->get(request);
    m_activeReply = reply;

    connect(reply, &QNetworkReply::finished, this, [this, reply, url, diskCachePath]() {
        if (m_activeReply == reply) {
            m_activeReply = nullptr;
        }
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            return;
        }

        // Check if track was already changed while downloading
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_currentUrl != url) {
                return;
            }
        }

        QByteArray data = reply->readAll();
        QImage netImg;
        if (netImg.loadFromData(data)) {
            // Downscale to max 160x160 before saving to disk cache and memory (~5-15 KB PNG)
            QImage thumb = (netImg.width() > 160 || netImg.height() > 160)
                               ? netImg.scaled(160, 160, Qt::KeepAspectRatio, Qt::SmoothTransformation)
                               : netImg;

            // Save the compressed thumbnail to disk cache
            thumb.save(diskCachePath, "PNG");

            std::lock_guard<std::mutex> lock(m_mutex);
            PutInCache(url, thumb);
            if (m_currentUrl == url) {
                m_currentImage = thumb;
                m_hasImage = true;
                m_elementCache.clear();
                emit CoverReady(url);
            }
        }
    });
}

QImage CoverArtRenderer::LoadCustomDefaultCover() {
    if (m_checkedDefaultCover) {
        return m_customDefaultCover;
    }
    m_checkedDefaultCover = true;

    // Search paths without auto-creating any directories
    QStringList baseDirs = {
        QDir::currentPath() + "/covers",
        QCoreApplication::applicationDirPath() + "/covers",
        PathManager::GetAppDataDir() + "/covers"
    };

    QStringList formats = {"png", "jpg", "jpeg", "webp", "bmp"};

    for (const QString& dirPath : baseDirs) {
        QDir dir(dirPath);
        if (!dir.exists()) continue;

        for (const QString& fmt : formats) {
            QString filePath = dir.filePath("no_cover." + fmt);
            if (QFile::exists(filePath)) {
                QImage img;
                if (img.load(filePath)) {
                    m_customDefaultCover = (img.width() > 160 || img.height() > 160)
                        ? img.scaled(160, 160, Qt::KeepAspectRatio, Qt::SmoothTransformation)
                        : img;
                    Logger::Log(LogLevel::INFO, "CoverArtRenderer: Loaded custom default cover from " + filePath.toStdString());
                    return m_customDefaultCover;
                }
            }
        }
    }

    return m_customDefaultCover;
}

ftxui::Element CoverArtRenderer::Render(int charWidth, int charHeight) {
    std::lock_guard<std::mutex> lock(m_mutex);

    std::string cacheKey = std::to_string(charWidth) + "x" + std::to_string(charHeight) + "@" + (m_hasImage ? m_currentUrl : "NO_COVER");
    auto cachedIt = m_elementCache.find(cacheKey);
    if (cachedIt != m_elementCache.end()) {
        return cachedIt->second;
    }

    QImage activeImage = m_currentImage;
    if (!m_hasImage || activeImage.isNull()) {
        activeImage = LoadCustomDefaultCover();
    }

    if (activeImage.isNull() || charWidth <= 0 || charHeight <= 0) {
        // High quality ASCII / Unicode placeholder (vinyl record / music note)
        std::vector<ftxui::Element> placeholderRows;
        placeholderRows.push_back(ftxui::text("╔════════════════╗") | ftxui::color(ftxui::Color::RGB(88, 166, 255)));
        placeholderRows.push_back(ftxui::text("║  AUDIO PLAYER  ║") | ftxui::color(ftxui::Color::RGB(80, 255, 150)));
        placeholderRows.push_back(ftxui::text("║   ╭────────╮   ║") | ftxui::color(ftxui::Color::RGB(139, 148, 158)));
        placeholderRows.push_back(ftxui::text("║   │  (●)   │   ║") | ftxui::color(ftxui::Color::RGB(255, 184, 108)));
        placeholderRows.push_back(ftxui::text("║   ╰────────╯   ║") | ftxui::color(ftxui::Color::RGB(139, 148, 158)));
        placeholderRows.push_back(ftxui::text("║  NO COVER ART  ║") | ftxui::color(ftxui::Color::RGB(180, 180, 180)));
        placeholderRows.push_back(ftxui::text("╚════════════════╝") | ftxui::color(ftxui::Color::RGB(88, 166, 255)));

        auto elem = ftxui::vbox(std::move(placeholderRows)) | ftxui::center | ftxui::bgcolor(ftxui::Color::RGB(16, 20, 28));
        if (m_elementCache.size() >= 5) {
            m_elementCache.clear();
        }
        m_elementCache[cacheKey] = elem;
        return elem;
    }

    // Each character block cell represents 2 vertical pixels (upper pixel & lower pixel)
    int pixelWidth = charWidth;
    int pixelHeight = charHeight * 2;

    QImage scaled = activeImage.scaled(pixelWidth, pixelHeight, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

    std::vector<ftxui::Element> rows;
    rows.reserve(charHeight);

    for (int y = 0; y < charHeight; ++y) {
        std::vector<ftxui::Element> cells;
        cells.reserve(charWidth);

        int topY = y * 2;
        int botY = topY + 1;

        for (int x = 0; x < charWidth; ++x) {
            QColor topColor = scaled.pixelColor(x, topY);
            QColor botColor = (botY < pixelHeight) ? scaled.pixelColor(x, botY) : topColor;

            cells.push_back(
                ftxui::text("▀")
                | ftxui::color(ftxui::Color::RGB(topColor.red(), topColor.green(), topColor.blue()))
                | ftxui::bgcolor(ftxui::Color::RGB(botColor.red(), botColor.green(), botColor.blue()))
            );
        }
        rows.push_back(ftxui::hbox(std::move(cells)));
    }

    auto elem = ftxui::vbox(std::move(rows));
    if (m_elementCache.size() >= 5) {
        m_elementCache.clear();
    }
    m_elementCache[cacheKey] = elem;
    return elem;
}

} // namespace tui
