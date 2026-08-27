#include "SoundCloudClient.h"
#include "utils/logger/Logger.h"
#include <QJsonArray>
#include <QUrlQuery>
#include <QRegularExpression>
#include <QJsonObject>

SoundCloudClient::SoundCloudClient(QObject* parent) : BaseApiProvider(parent) {
    Logger::Log(LogLevel::INFO, "SoundCloudClient created.");
}
SoundCloudClient::~SoundCloudClient() {
    Logger::Log(LogLevel::INFO, "SoundCloudClient destroyed.");
}

bool SoundCloudClient::HandleApiError(const QJsonDocument& json, int httpStatusCode) {
    if (httpStatusCode >= 400) {
        Logger::Log(LogLevel::ERROR, "SoundCloud API Error HTTP " + std::to_string(httpStatusCode));
        return true;
    }
    return false;
}

void SoundCloudClient::InitializeWithToken() {
    FetchClientId();
}

void SoundCloudClient::FetchClientId() {
    QNetworkRequest request((QUrl("https://soundcloud.com")));
    QNetworkReply* reply = m_manager->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() == QNetworkReply::NoError) {
            QString html = reply->readAll();
            QRegularExpression re("<script crossorigin src=\"(https://a-v2\\.sndcdn\\.com/assets/[^\"]+\\.js)\"></script>");
            QRegularExpressionMatchIterator i = re.globalMatch(html);
            QString lastJsUrl;
            while (i.hasNext()) lastJsUrl = i.next().captured(1);

            if (!lastJsUrl.isEmpty()) ExtractClientIdFromJs(lastJsUrl);
            else emit ApiError("Could not find JS files on SC homepage.");
        } else emit ApiError("Failed to load SC homepage.");
        reply->deleteLater();
    });
}

void SoundCloudClient::ExtractClientIdFromJs(const QString& jsUrl) {
    QNetworkRequest request((QUrl(jsUrl)));
    QNetworkReply* reply = m_manager->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() == QNetworkReply::NoError) {
            QString js = reply->readAll();
            QRegularExpression re("client_id:\"([a-zA-Z0-9]{32})\"");
            QRegularExpressionMatch match = re.match(js);
            if (match.hasMatch()) {
                m_clientId = match.captured(1).toStdString();
                FetchMe();
            } else emit ApiError("Could not extract client_id.");
        } else emit ApiError("Failed to load JS file.");
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
        std::vector<Track> chunkTracks;
        chunkTracks.reserve(collection.size());

        for (const QJsonValue& val : collection) {
            QJsonObject item = val.toObject();
            if (!item.contains("track")) continue;
            QJsonObject trackObj = item["track"].toObject();

            Track track;
            track.id = std::to_string(trackObj["id"].toInt());
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
            chunkTracks.push_back(std::move(track));
        }

        if (!chunkTracks.empty()) emit AudioFetched(chunkTracks);
        if (json.object().contains("next_href") && !json.object()["next_href"].isNull()) {
            m_nextHref = json.object()["next_href"].toString();
            FetchAllUserAudio(offset + chunkTracks.size(), count);
        } else emit FinishedFetching();
    }, [this](const std::string&) { emit FinishedFetching(); });
}

void SoundCloudClient::FetchTrackUrl(const std::string& trackId, std::function<void(const std::string&, bool)> callback) {
    QString trackUrl = QString("https://api-v2.soundcloud.com/tracks/%1?client_id=%2").arg(QString::fromStdString(trackId), QString::fromStdString(m_clientId));
    QNetworkRequest request((QUrl(trackUrl)));

    SendJsonRequest(request, [this, callback](const QJsonDocument& json) {
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

        QUrl url(transUrl);
        QUrlQuery transQuery(url.query());
        transQuery.addQueryItem("client_id", QString::fromStdString(m_clientId));
        if (!trackAuth.isEmpty()) transQuery.addQueryItem("track_authorization", trackAuth);
        url.setQuery(transQuery);

        QNetworkRequest cdnReq(url);
        SendJsonRequest(cdnReq, [callback](const QJsonDocument& cdnJson) {
            callback(cdnJson.object()["url"].toString().toStdString(), false);
        }, [callback](const std::string&) { callback("", true); });

    }, [callback](const std::string&) { callback("", true); });
}