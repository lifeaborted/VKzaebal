#include "YouTubeClient.h"
#include "YouTubePoTokenGenerator.h"
#include "YouTubeExtractor.h"
#include "utils/logger/Logger.h"
#include <QTimer>

YouTubeClient::YouTubeClient(QObject* parent)
    : BaseApiProvider(parent) {
    Logger::Log(LogLevel::INFO, "YouTubeClient: Initializing Headless Qt6 Engine (PoTokenGenerator + YouTubeExtractor)...");
    m_tokenGenerator = std::make_unique<YouTubePoTokenGenerator>(this);
    m_extractor = std::make_unique<YouTubeExtractor>(m_manager, m_tokenGenerator.get(), this);
}

YouTubeClient::~YouTubeClient() {
    Logger::Log(LogLevel::INFO, "YouTubeClient: Destroyed.");
}

bool YouTubeClient::HandleApiError(const QJsonDocument& json, int httpStatusCode) {
    Q_UNUSED(json);
    Q_UNUSED(httpStatusCode);
    return false;
}

void YouTubeClient::FetchTrackUrl(const std::string& trackId, std::function<void(const std::string&, bool)> callback) {
    Logger::Log(LogLevel::INFO, "YouTubeClient: FetchTrackUrl called for trackId=" + trackId);
    QString qTrackId = QString::fromStdString(trackId);

    m_extractor->extractAudioUrl(qTrackId, [callback, trackId](const QString& streamUrl, bool isError) {
        if (isError || streamUrl.isEmpty()) {
            Logger::Log(LogLevel::ERROR, "YouTubeClient: Extraction failed for track: " + trackId);
            callback("", true);
        } else {
            Logger::Log(LogLevel::INFO, "YouTubeClient: Extraction succeeded for track: " + trackId);
            callback(streamUrl.toStdString(), false);
        }
    });
}

void YouTubeClient::FetchAllUserAudio(int offset, int count) {
    Q_UNUSED(count);
    if (offset > 0) {
        emit FinishedFetching();
        return;
    }

    std::vector<Track> chunkTracks;
    Track t;
    t.id = "rm78jQsP1Og";
    t.source = "YouTube";
    t.artist = "RSAC";
    t.title = "ЩЕНКИ - ГРЯЗЬ";
    t.duration = 192;
    chunkTracks.push_back(t);

    QTimer::singleShot(100, this, [this, chunkTracks]() {
        emit AudioFetched(chunkTracks);
        emit FinishedFetching();
    });
}