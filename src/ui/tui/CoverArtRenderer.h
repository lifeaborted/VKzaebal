#pragma once

#include <QObject>
#include <QImage>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <ftxui/dom/elements.hpp>
#include <string>
#include <vector>
#include <unordered_map>
#include <list>
#include <mutex>

namespace tui {

class CoverArtRenderer : public QObject {
    Q_OBJECT
public:
    explicit CoverArtRenderer(QNetworkAccessManager* netManager, QObject* parent = nullptr);
    ~CoverArtRenderer() override;

    // Requests asynchronous cover loading if not already in memory/disk cache with internal debouncing
    void RequestCover(const std::string& url, int debounceMs = 180);

    // Cancel any active network reply
    void CancelActiveRequest();

    // Renders the cover as an FTXUI Element (using half-block Unicode glyphs: ▀)
    ftxui::Element Render(int charWidth, int charHeight);

signals:
    void CoverReady(const std::string& url);

private:
    static QImage DownscaleIfNeeded(const QImage& img, int maxDim = 160);
    void PutInCache(const std::string& url, const QImage& image);
    QString GetCachePathForUrl(const std::string& url) const;
    QImage LoadCustomDefaultCover();
    void ExecuteRequestCover(const std::string& url);

    QNetworkAccessManager* m_netManager = nullptr;
    QPointer<QNetworkReply> m_activeReply = nullptr;
    QTimer* m_debounceTimer = nullptr;
    std::string m_pendingUrl;
    std::string m_currentUrl;
    QImage m_currentImage;
    bool m_hasImage = false;
    bool m_checkedDefaultCover = false;
    QImage m_customDefaultCover;

    // LRU Cache of downscaled QImage by URL (max 8 images to prevent RAM growth)
    static constexpr size_t MAX_CACHE_SIZE = 8;
    std::list<std::pair<std::string, QImage>> m_lruList;
    std::unordered_map<std::string, std::list<std::pair<std::string, QImage>>::iterator> m_imageCache;

    // Cache of rendered FTXUI Elements by dimension and URL
    std::unordered_map<std::string, ftxui::Element> m_elementCache;
    std::mutex m_mutex;
};

} // namespace tui
