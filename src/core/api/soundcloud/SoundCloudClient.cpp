#include "SoundCloudClient.h"
#include "utils/logger/Logger.h"
#include "utils/path/PathManager.h"
#include <QJsonArray>
#include <QUrlQuery>
#include <QRegularExpression>
#include <QJsonObject>
#include <QSettings>
#include <QTimer>

SoundCloudClient::SoundCloudClient(QObject* parent, QNetworkAccessManager* manager) : BaseApiProvider(parent, manager) {
    QSettings settings(PathManager::GetConfigPath(), QSettings::IniFormat);
    m_clientId = settings.value("SoundCloud/ClientId", "").toString().toStdString();
    Logger::Log(LogLevel::INFO, std::string("SoundCloudClient created.") + (m_clientId.empty() ? "" : " (using cached client_id)"));
}
SoundCloudClient::~SoundCloudClient() {
    FailPendingRequests();
    Logger::Log(LogLevel::INFO, "SoundCloudClient destroyed.");
}

bool SoundCloudClient::HandleApiError(const QJsonDocument& json, int httpStatusCode) {
    if (httpStatusCode == 401) {
        Logger::Log(LogLevel::WARNING, "SoundCloud: 401 Unauthorized. Client ID may be invalid, clearing cache...");
        m_clientId.clear();
        QSettings settings(PathManager::GetConfigPath(), QSettings::IniFormat);
        settings.remove("SoundCloud/ClientId");
        if (!m_isFetchingClientId) {
            FetchClientId();
        }
        return true;
    }
    if (httpStatusCode >= 400) {
        Logger::Log(LogLevel::ERROR, "SoundCloud API Error HTTP " + std::to_string(httpStatusCode));
        return true;
    }
    return false;
}

void SoundCloudClient::InitializeWithToken() {
    if (!m_clientId.empty()) {
        FetchMe();
    } else {
        FetchClientId();
    }
}

void SoundCloudClient::FailPendingRequests() {
    auto pending = std::move(m_pendingTrackRequests);
    m_pendingTrackRequests.clear();
    for (const auto& req : pending) {
        req.second("", true);
    }
}

void SoundCloudClient::FetchClientId() {
    if (m_isFetchingClientId) return;
    m_isFetchingClientId = true;

    QNetworkRequest request((QUrl("https://soundcloud.com")));
    QNetworkReply* reply = m_manager->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() == QNetworkReply::NoError) {
            QString html = reply->readAll();
            QRegularExpression re("<script crossorigin src=\"(https://a-v2\\.sndcdn\\.com/assets/[^\"]+\\.js)\"></script>");
            QRegularExpressionMatchIterator i = re.globalMatch(html);
            QString lastJsUrl;
            while (i.hasNext()) lastJsUrl = i.next().captured(1);

            if (!lastJsUrl.isEmpty()) {
                ExtractClientIdFromJs(lastJsUrl);
            } else {
                m_isFetchingClientId = false;
                emit ApiError("Could not find JS files on SC homepage.");
                FailPendingRequests();
            }
        } else {
            m_isFetchingClientId = false;
            emit ApiError("Failed to load SC homepage.");
            FailPendingRequests();
        }
        reply->deleteLater();
    });
}

void SoundCloudClient::ExtractClientIdFromJs(const QString& jsUrl) {
    QNetworkRequest request((QUrl(jsUrl)));
    QNetworkReply* reply = m_manager->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        m_isFetchingClientId = false;
        if (reply->error() == QNetworkReply::NoError) {
            QString js = reply->readAll();
            QRegularExpression re("client_id:\"([a-zA-Z0-9]{32})\"");
            QRegularExpressionMatch match = re.match(js);
            if (match.hasMatch()) {
                m_clientId = match.captured(1).toStdString();
                QSettings settings(PathManager::GetConfigPath(), QSettings::IniFormat);
                settings.setValue("SoundCloud/ClientId", QString::fromStdString(m_clientId));
                Logger::Log(LogLevel::INFO, "SoundCloud: Client ID acquired and cached.");
                FetchMe();

                auto pending = std::move(m_pendingTrackRequests);
                m_pendingTrackRequests.clear();
                for (const auto& req : pending) {
                    FetchTrackUrl(req.first, req.second);
                }
            } else {
                emit ApiError("Could not extract client_id.");
                FailPendingRequests();
            }
        } else {
            emit ApiError("Failed to load JS file.");
            FailPendingRequests();
        }
        reply->deleteLater();
    });
}

void SoundCloudClient::FetchMe() {
    QUrl url("https://api-v2.soundcloud.com/me");
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", QByteArray("OAuth ") + QByteArray::fromStdString(m_accessToken));

    SendJsonRequest(request, [this](const QJsonDocument& json) {
        m_userId = std::to_string(json.object()["id"].toInt());
        Logger::Log(LogLevel::INFO, "SoundCloud: User ID: " + m_userId);
        FetchAllUserAudio(0, 50);
    }, [this](const std::string&) { emit ApiError("Failed to fetch profile info."); });
}

void SoundCloudClient::FetchAllUserAudio(int offset, int count) {
    QString url;
    if (offset == 0) {
        m_nextHref.clear();
        url = QString("https://api-v2.soundcloud.com/users/%1/likes?client_id=%2&limit=%3&linked_partitioning=1").arg(QString::fromStdString(m_userId), QString::fromStdString(m_clientId)).arg(count);
    } else if (!m_nextHref.isEmpty()) {
        url = m_nextHref;
        if (!url.contains("client_id=")) url += "&client_id=" + QString::fromStdString(m_clientId);
    } else {
        emit FinishedFetching(); return;
    }

    QNetworkRequest request((QUrl(url)));
    request.setRawHeader("Authorization", QByteArray("OAuth ") + QByteArray::fromStdString(m_accessToken));

    SendJsonRequest(request, [this, offset, count](const QJsonDocument& json) {
        QJsonArray collection = json.object()["collection"].toArray();
        std::vector<Track> chunkTracks = ParseSoundCloudTracks(collection);

        if (!chunkTracks.empty()) emit AudioFetched(chunkTracks);
        if (json.object().contains("next_href") && !json.object()["next_href"].isNull()) {
            m_nextHref = json.object()["next_href"].toString();
            FetchAllUserAudio(offset + chunkTracks.size(), count);
        } else emit FinishedFetching();
    }, [this](const std::string&) { emit FinishedFetching(); });
}

std::vector<Track> SoundCloudClient::ParseSoundCloudTracks(const QJsonArray& collection) {
    std::vector<Track> chunkTracks;
    chunkTracks.reserve(collection.size());

    for (const QJsonValue& val : collection) {
        QJsonObject item = val.toObject();
        QJsonObject trackObj = item.contains("track") ? item["track"].toObject() : item;

        int trackIdInt = trackObj["id"].toInt();
        if (trackIdInt == 0) continue;

        Track track;
        track.id = std::to_string(trackIdInt);
        track.source = "SoundCloud";
        track.ownerId = std::to_string(trackObj["user"].toObject()["id"].toInt());
        track.artist = trackObj["user"].toObject()["username"].toString().toStdString();
        track.title = trackObj["title"].toString().toStdString();
        track.duration = trackObj["duration"].toInt() / 1000;
        QString artwork = trackObj["artwork_url"].toString();
        if (artwork.isEmpty()) artwork = trackObj["user"].toObject()["avatar_url"].toString();
        if (!artwork.isEmpty()) {
            artwork.replace("-large.jpg", "-t500x500.jpg");
            track.coverUrl = artwork.toStdString();
        }

        // ОПТИМИЗАЦИЯ OPT-AUTH-02: Кэшируем транскодинг сразу при парсинге
        QString trackAuth = trackObj["track_authorization"].toString();
        QJsonArray transcodings = trackObj["media"].toObject()["transcodings"].toArray();
        QString transUrl;
        for (const QJsonValue& tval : transcodings) {
            QJsonObject trans = tval.toObject();
            QString protocol = trans["format"].toObject()["protocol"].toString();
            if (protocol == "progressive") { transUrl = trans["url"].toString(); break; }
            if (protocol == "hls" && transUrl.isEmpty()) transUrl = trans["url"].toString();
        }
        if (!transUrl.isEmpty()) {
            m_trackTranscodings[track.id] = {transUrl, trackAuth};
        }

        chunkTracks.push_back(std::move(track));
    }
    return chunkTracks;
}

void SoundCloudClient::SearchAudio(const std::string& query, int count, int offset,
                                   std::function<void(const std::vector<Track>& tracks, const std::string& error)> callback) {
    if (query.empty()) {
        if (callback) callback({}, "");
        return;
    }

    auto doSearch = [this, query, count, offset, callback]() {
        QUrl url("https://api-v2.soundcloud.com/search/tracks");
        QUrlQuery q;
        q.addQueryItem("q", QString::fromStdString(query));
        q.addQueryItem("client_id", QString::fromStdString(m_clientId));
        q.addQueryItem("limit", QString::number(count));
        q.addQueryItem("offset", QString::number(offset));
        url.setQuery(q);

        QNetworkRequest request(url);
        if (!m_accessToken.empty()) {
            request.setRawHeader("Authorization", QByteArray("OAuth ") + QByteArray::fromStdString(m_accessToken));
        }
        request.setTransferTimeout(8000);

        SendJsonRequest(request, [this, callback](const QJsonDocument& json) {
            QJsonArray collection = json.object()["collection"].toArray();
            std::vector<Track> tracks = ParseSoundCloudTracks(collection);
            if (callback) callback(tracks, "");
        }, [callback](const std::string& err) {
            if (callback) callback({}, err);
        });
    };

    if (m_clientId.empty()) {
        if (!m_isFetchingClientId) FetchClientId();
        QTimer::singleShot(1000, this, [doSearch]() { doSearch(); });
    } else {
        doSearch();
    }
}

void SoundCloudClient::AddTrackToFavorites(const std::string& trackId, const std::string& /*ownerId*/,
                                          std::function<void(bool success, const std::string& error)> callback) {
    if (m_accessToken.empty()) {
        if (callback) callback(false, "SoundCloud access token is empty");
        return;
    }
    if (m_userId.empty() || m_clientId.empty()) {
        if (callback) callback(false, "SoundCloud user ID or client ID not ready");
        return;
    }

    QUrl url(QString("https://api-v2.soundcloud.com/users/%1/track_likes/%2?client_id=%3")
                 .arg(QString::fromStdString(m_userId), QString::fromStdString(trackId), QString::fromStdString(m_clientId)));
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", QByteArray("OAuth ") + QByteArray::fromStdString(m_accessToken));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QNetworkReply* reply = m_manager->post(request, QByteArray("{}"));
    connect(reply, &QNetworkReply::finished, this, [reply, callback]() {
        bool ok = (reply->error() == QNetworkReply::NoError);
        std::string err = ok ? "" : reply->errorString().toStdString();
        reply->deleteLater();
        if (callback) callback(ok, err);
    });
}

void SoundCloudClient::RemoveTrackFromFavorites(const std::string& trackId, const std::string& /*ownerId*/,
                                             std::function<void(bool success, const std::string& error)> callback) {
    if (m_accessToken.empty()) {
        if (callback) callback(false, "SoundCloud access token is empty");
        return;
    }
    if (m_userId.empty() || m_clientId.empty()) {
        if (callback) callback(false, "SoundCloud user ID or client ID not ready");
        return;
    }

    QUrl url(QString("https://api-v2.soundcloud.com/users/%1/track_likes/%2?client_id=%3")
                 .arg(QString::fromStdString(m_userId), QString::fromStdString(trackId), QString::fromStdString(m_clientId)));
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", QByteArray("OAuth ") + QByteArray::fromStdString(m_accessToken));

    QNetworkReply* reply = m_manager->deleteResource(request);
    connect(reply, &QNetworkReply::finished, this, [reply, callback]() {
        bool ok = (reply->error() == QNetworkReply::NoError);
        std::string err = ok ? "" : reply->errorString().toStdString();
        reply->deleteLater();
        if (callback) callback(ok, err);
    });
}

void SoundCloudClient::RequestCdnUrl(const QString& transUrl, const QString& trackAuth, std::function<void(const std::string&, bool)> callback) {
    QUrl url(transUrl);
    QUrlQuery transQuery(url.query());
    transQuery.addQueryItem("client_id", QString::fromStdString(m_clientId));
    if (!trackAuth.isEmpty()) {
        transQuery.addQueryItem("track_authorization", trackAuth);
    }
    url.setQuery(transQuery);

    QNetworkRequest cdnReq(url);
    SendJsonRequest(cdnReq, [callback](const QJsonDocument& cdnJson) {
        callback(cdnJson.object()["url"].toString().toStdString(), false);
    }, [callback](const std::string&) { callback("", true); });
}

void SoundCloudClient::FetchTrackUrl(const std::string& trackId, std::function<void(const std::string&, bool)> callback) {
    if (m_clientId.empty()) {
        Logger::Log(LogLevel::INFO, "SoundCloud: Client ID not ready yet, queueing track URL request for " + trackId);
        m_pendingTrackRequests.push_back({trackId, callback});
        if (!m_isFetchingClientId) {
            FetchClientId();
        }
        return;
    }

    // Fast-path: если транскодинг уже известен из списка треков, сразу запрашиваем CDN URL (1 сетевой запрос вместо 2)
    auto it = m_trackTranscodings.find(trackId);
    if (it != m_trackTranscodings.end()) {
        Logger::Log(LogLevel::INFO, "SoundCloud: Fast-path for track " + trackId + " (skipping track metadata fetch)");
        RequestCdnUrl(it->second.transUrl, it->second.trackAuth, callback);
        return;
    }

    // Медленный путь: если трек запущен не из коллекции (например, прямой запуск по id)
    QString trackUrl = QString("https://api-v2.soundcloud.com/tracks/%1?client_id=%2").arg(QString::fromStdString(trackId), QString::fromStdString(m_clientId));
    QNetworkRequest request((QUrl(trackUrl)));

    SendJsonRequest(request, [this, trackId, callback](const QJsonDocument& json) {
        QJsonObject trackObj = json.object();
        QString trackAuth = trackObj["track_authorization"].toString();
        QJsonArray transcodings = trackObj["media"].toObject()["transcodings"].toArray();
        QString transUrl;

        for (const QJsonValue& val : transcodings) {
            QJsonObject trans = val.toObject();
            QString protocol = trans["format"].toObject()["protocol"].toString();
            if (protocol == "progressive") { transUrl = trans["url"].toString(); break; }
            if (protocol == "hls" && transUrl.isEmpty()) transUrl = trans["url"].toString();
        }

        if (transUrl.isEmpty()) { callback("", false); return; }

        m_trackTranscodings[trackId] = {transUrl, trackAuth};
        RequestCdnUrl(transUrl, trackAuth, callback);
    }, [callback](const std::string&) { callback("", true); });
}