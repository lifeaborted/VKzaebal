#include "YouTubeClient.h"
#include "YouTubePoTokenGenerator.h"
#include "YouTubeExtractor.h"
#include "utils/logger/Logger.h"

#include <QUrl>
#include <QUrlQuery>
#include <QNetworkRequest>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QCryptographicHash>
#include <QDateTime>
#include <QRegularExpression>
#include <QSet>

YouTubeClient::YouTubeClient(QObject* parent, QNetworkAccessManager* manager)
    : BaseApiProvider(parent, manager) {
    Logger::Log(LogLevel::INFO, "YouTubeClient: Initializing Headless Qt6 Engine (PoTokenGenerator + YouTubeExtractor)...");
    m_tokenGenerator = std::make_unique<YouTubePoTokenGenerator>(this);
    m_extractor = std::make_unique<YouTubeExtractor>(m_manager, m_tokenGenerator.get(), this);
}

YouTubeClient::~YouTubeClient() {
    Logger::Log(LogLevel::INFO, "YouTubeClient: Destroyed.");
}

bool YouTubeClient::HandleApiError(const QJsonDocument& json, int httpStatusCode) {
    if (httpStatusCode == 401 || httpStatusCode == 403) {
        Logger::Log(LogLevel::WARNING, "YouTubeClient: HTTP " + std::to_string(httpStatusCode) + " - Session expired or forbidden.");
        emit TokenExpired();
        return true;
    }

    QJsonObject root = json.object();
    if (root.contains("error")) {
        QString errMsg = root["error"].isObject()
                             ? root["error"].toObject()["message"].toString()
                             : root["error"].toString();
        Logger::Log(LogLevel::ERROR, "YouTubeClient: API Error [" + std::to_string(httpStatusCode) + "]: " + errMsg.toStdString());
        return true;
    }

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
    if (m_accessToken.empty()) {
        Logger::Log(LogLevel::WARNING, "YouTubeClient: No access token/cookies found. Prompting user for authentication...");
        emit TokenExpired();
        return;
    }

    if (offset == 0) {
        m_totalFetched = 0;
        m_continuationToken.clear();
        FetchLikedMusic(0, count, "");
    } else if (!m_continuationToken.isEmpty()) {
        FetchLikedMusic(offset, count, m_continuationToken);
    } else {
        emit FinishedFetching();
    }
}

void YouTubeClient::FetchLikedMusic(int offset, int count, const QString& continuationToken) {
    Q_UNUSED(offset);
    Logger::Log(LogLevel::INFO, "YouTubeClient: Fetching Liked Music from YouTube Music (total fetched: " + std::to_string(m_totalFetched) + ")...");

    QUrl url;
    if (continuationToken.isEmpty()) {
        url = QUrl("https://music.youtube.com/youtubei/v1/browse?prettyPrint=false");
    } else {
        url = QUrl(QString("https://music.youtube.com/youtubei/v1/browse?continuation=%1&prettyPrint=false")
                       .arg(QString::fromUtf8(QUrl::toPercentEncoding(continuationToken))));
    }

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setHeader(QNetworkRequest::UserAgentHeader, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/133.0.0.0 Safari/537.36");
    request.setRawHeader("Origin", "https://music.youtube.com");
    request.setRawHeader("Referer", "https://music.youtube.com/");
    request.setRawHeader("X-Origin", "https://music.youtube.com");
    request.setRawHeader("X-YouTube-Client-Name", "67");
    request.setRawHeader("X-YouTube-Client-Version", "1.20250101.01.00");
    request.setRawHeader("X-Goog-AuthUser", "0");
    request.setRawHeader("Accept", "*/*");

    QString cookieStr = QString::fromStdString(m_accessToken);
    request.setRawHeader("Cookie", cookieStr.toUtf8());

    QString sapisidAuth = generateSapisidHash(cookieStr, "https://music.youtube.com");
    if (!sapisidAuth.isEmpty()) {
        request.setRawHeader("Authorization", sapisidAuth.toUtf8());
    }

    QJsonObject clientObj;
    clientObj["clientName"] = "WEB_REMIX";
    clientObj["clientVersion"] = "1.20250101.01.00";
    clientObj["hl"] = "ru";
    clientObj["gl"] = "US";

    QJsonObject contextObj;
    contextObj["client"] = clientObj;

    QJsonObject requestObj;
    requestObj["context"] = contextObj;

    if (continuationToken.isEmpty()) {
        requestObj["browseId"] = "VLLM";
    } else {
        requestObj["continuation"] = continuationToken;
    }

    QByteArray payload = QJsonDocument(requestObj).toJson(QJsonDocument::Compact);

    SendJsonRequest(request, [this, count](const QJsonDocument& json) {
        QJsonObject root = json.object();

        // Проверка: вернулось ли приглашение войти (значит куки недействительны)
        QString rawJson = QString::fromUtf8(json.toJson(QJsonDocument::Compact));
        if (rawJson.contains("\"logged_in\",\"value\":\"0\"") || rawJson.contains("signInEndpoint")) {
            Logger::Log(LogLevel::WARNING, "YouTubeClient: Sign-in required! Session cookies expired or invalid.");
            emit TokenExpired();
            return;
        }

        std::vector<Track> tracks = parseTracksFromBrowseResponse(root);
        if (!tracks.empty()) {
            Logger::Log(LogLevel::INFO, "YouTubeClient: Successfully parsed " + std::to_string(tracks.size()) + " tracks from Liked Music.");
            m_totalFetched += static_cast<int>(tracks.size());
            emit AudioFetched(tracks);

            m_continuationToken = extractContinuationToken(root);
            if (!m_continuationToken.isEmpty() && m_totalFetched < count) {
                FetchLikedMusic(m_totalFetched, count, m_continuationToken);
                return;
            }
        } else {
            Logger::Log(LogLevel::INFO, "YouTubeClient: No more tracks found in Liked Music.");
        }

        emit FinishedFetching();
    }, [this](const std::string& err) {
        Logger::Log(LogLevel::ERROR, "YouTubeClient: Network error while fetching liked tracks: " + err);
        emit FinishedFetching();
    }, payload);
}

std::vector<Track> YouTubeClient::parseTracksFromBrowseResponse(const QJsonObject& root) {
    std::vector<Track> result;
    QSet<QString> seenIds;

    std::function<void(const QJsonValue&)> findRenderers = [&](const QJsonValue& val) {
        if (val.isObject()) {
            QJsonObject obj = val.toObject();

            // 1. YouTube Music item renderer: musicResponsiveListItemRenderer
            if (obj.contains("musicResponsiveListItemRenderer")) {
                QJsonObject r = obj["musicResponsiveListItemRenderer"].toObject();

                QString videoId;
                if (r.contains("playlistItemData")) {
                    videoId = r["playlistItemData"].toObject()["videoId"].toString();
                }
                if (videoId.isEmpty()) {
                    QJsonObject nav = r["navigationEndpoint"].toObject();
                    if (nav.contains("watchEndpoint")) {
                        videoId = nav["watchEndpoint"].toObject()["videoId"].toString();
                    } else if (r.contains("overlay")) {
                        QJsonObject overlay = r["overlay"].toObject();
                        QJsonObject playBtn = overlay["musicItemThumbnailOverlayRenderer"].toObject()["content"].toObject()["musicPlayButtonRenderer"].toObject();
                        videoId = playBtn["playNavigationEndpoint"].toObject()["watchEndpoint"].toObject()["videoId"].toString();
                    }
                }

                QJsonArray cols = r["flexColumns"].toArray();
                if (videoId.isEmpty() && cols.size() > 0) {
                    QJsonArray runs0 = cols[0].toObject()["musicResponsiveListItemFlexColumnRenderer"].toObject()["text"].toObject()["runs"].toArray();
                    if (!runs0.isEmpty() && runs0[0].toObject().contains("navigationEndpoint")) {
                        videoId = runs0[0].toObject()["navigationEndpoint"].toObject()["watchEndpoint"].toObject()["videoId"].toString();
                    }
                }

                if (!videoId.isEmpty() && !seenIds.contains(videoId)) {
                    seenIds.insert(videoId);
                    Track track;
                    track.id = videoId.toStdString();
                    track.source = "YouTube";

                    if (cols.size() > 0) {
                        QJsonArray runs0 = cols[0].toObject()["musicResponsiveListItemFlexColumnRenderer"].toObject()["text"].toObject()["runs"].toArray();
                        if (!runs0.isEmpty()) {
                            track.title = runs0[0].toObject()["text"].toString().toStdString();
                        }
                    }

                    for (int i = 1; i < cols.size(); ++i) {
                        QJsonArray runs = cols[i].toObject()["musicResponsiveListItemFlexColumnRenderer"].toObject()["text"].toObject()["runs"].toArray();
                        for (const QJsonValue& runVal : runs) {
                            QString t = runVal.toObject()["text"].toString().trimmed();
                            if (t == "•" || t == "-" || t == "Song" || t == "Песня" || t == "Video" || t == "Видео") {
                                continue;
                            }
                            if (t.contains(':')) {
                                QStringList parts = t.split(':');
                                if (parts.size() == 2) {
                                    track.duration = parts[0].toInt() * 60 + parts[1].toInt();
                                } else if (parts.size() == 3) {
                                    track.duration = parts[0].toInt() * 3600 + parts[1].toInt() * 60 + parts[2].toInt();
                                }
                            } else if (track.artist.empty() && !t.isEmpty()) {
                                track.artist = t.toStdString();
                            }
                        }
                    }

                    if (track.duration == 0 && r.contains("fixedColumns")) {
                        QJsonArray fixedCols = r["fixedColumns"].toArray();
                        for (const QJsonValue& fixedVal : fixedCols) {
                            QJsonArray fRuns = fixedVal.toObject()["musicResponsiveListItemFixedColumnRenderer"].toObject()["text"].toObject()["runs"].toArray();
                            for (const QJsonValue& fRun : fRuns) {
                                QString t = fRun.toObject()["text"].toString().trimmed();
                                if (t.contains(':')) {
                                    QStringList parts = t.split(':');
                                    if (parts.size() == 2) {
                                        track.duration = parts[0].toInt() * 60 + parts[1].toInt();
                                    } else if (parts.size() == 3) {
                                        track.duration = parts[0].toInt() * 3600 + parts[1].toInt() * 60 + parts[2].toInt();
                                    }
                                }
                            }
                        }
                    }

                    QJsonArray thumbs = r["thumbnail"].toObject()["musicThumbnailRenderer"].toObject()["thumbnail"].toObject()["thumbnails"].toArray();
                    if (!thumbs.isEmpty()) {
                        track.coverUrl = thumbs.last().toObject()["url"].toString().toStdString();
                    }

                    if (track.artist.empty()) track.artist = "YouTube Music";
                    if (track.title.empty()) track.title = "Untitled Track";

                    result.push_back(track);
                }
            }
            // 2. Резервный формат: стандартный YouTube playlistVideoRenderer
            else if (obj.contains("playlistVideoRenderer")) {
                QJsonObject r = obj["playlistVideoRenderer"].toObject();
                QString videoId = r["videoId"].toString();

                if (!videoId.isEmpty() && !seenIds.contains(videoId)) {
                    seenIds.insert(videoId);
                    Track track;
                    track.id = videoId.toStdString();
                    track.source = "YouTube";

                    QJsonArray titleRuns = r["title"].toObject()["runs"].toArray();
                    if (!titleRuns.isEmpty()) {
                        track.title = titleRuns[0].toObject()["text"].toString().toStdString();
                    }

                    QJsonArray bylineRuns = r["shortBylineText"].toObject()["runs"].toArray();
                    if (!bylineRuns.isEmpty()) {
                        track.artist = bylineRuns[0].toObject()["text"].toString().toStdString();
                    }

                    track.duration = r["lengthSeconds"].toString().toInt();

                    QJsonArray thumbs = r["thumbnail"].toObject()["thumbnails"].toArray();
                    if (!thumbs.isEmpty()) {
                        track.coverUrl = thumbs.last().toObject()["url"].toString().toStdString();
                    }

                    if (track.artist.empty()) track.artist = "YouTube";
                    if (track.title.empty()) track.title = "Untitled Track";

                    result.push_back(track);
                }
            }

            for (auto it = obj.begin(); it != obj.end(); ++it) {
                findRenderers(it.value());
            }
        } else if (val.isArray()) {
            for (const QJsonValue& elem : val.toArray()) {
                findRenderers(elem);
            }
        }
    };

    findRenderers(root);
    return result;
}

QString YouTubeClient::extractContinuationToken(const QJsonObject& root) {
    std::function<QString(const QJsonValue&)> findToken = [&](const QJsonValue& val) -> QString {
        if (val.isObject()) {
            QJsonObject obj = val.toObject();
            if (obj.contains("nextContinuationData")) {
                return obj["nextContinuationData"].toObject()["continuation"].toString();
            }
            if (obj.contains("continuationCommand")) {
                return obj["continuationCommand"].toObject()["token"].toString();
            }
            for (auto it = obj.begin(); it != obj.end(); ++it) {
                QString res = findToken(it.value());
                if (!res.isEmpty()) return res;
            }
        } else if (val.isArray()) {
            for (const QJsonValue& elem : val.toArray()) {
                QString res = findToken(elem);
                if (!res.isEmpty()) return res;
            }
        }
        return "";
    };

    return findToken(root);
}

QString YouTubeClient::extractCookieValue(const QString& cookies, const QString& key) {
    QRegularExpression re("(?:^|;\\s*)" + QRegularExpression::escape(key) + "=([^;]+)");
    auto match = re.match(cookies);
    if (match.hasMatch()) {
        return match.captured(1).trimmed();
    }
    return "";
}

QString YouTubeClient::generateSapisidHash(const QString& cookies, const QString& origin) {
    QString sapisid = extractCookieValue(cookies, "SAPISID");
    if (sapisid.isEmpty()) {
        sapisid = extractCookieValue(cookies, "__Secure-1PAPISID");
    }
    if (sapisid.isEmpty()) {
        sapisid = extractCookieValue(cookies, "__Secure-3PAPISID");
    }
    if (sapisid.isEmpty()) {
        return "";
    }

    qint64 timestamp = QDateTime::currentSecsSinceEpoch();
    QString toHash = QString("%1 %2 %3").arg(timestamp).arg(sapisid, origin);
    QByteArray hash = QCryptographicHash::hash(toHash.toUtf8(), QCryptographicHash::Sha1).toHex();
    return QString("SAPISIDHASH %1_%2").arg(timestamp).arg(QString::fromUtf8(hash));
}