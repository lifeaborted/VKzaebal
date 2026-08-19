#include "SoundCloudClient.h"
#include "utils/logger/Logger.h"

#include <QJsonArray>
#include <QUrlQuery>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QJsonObject>

SoundCloudClient::SoundCloudClient(QObject* parent)
    : IAudioProvider(parent), m_manager(new QNetworkAccessManager(this)) {
    Logger::Log(LogLevel::INFO, "SoundCloudClient created.");
}

SoundCloudClient::~SoundCloudClient() {
    Logger::Log(LogLevel::INFO, "SoundCloudClient destroyed.");
}

void SoundCloudClient::InitializeWithProfile(const QString& profileUrl) {
    m_profileUrl = profileUrl;
    Logger::Log(LogLevel::INFO, "SoundCloud: Starting initialization for profile " + profileUrl.toStdString());
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
            while (i.hasNext()) {
                QRegularExpressionMatch match = i.next();
                lastJsUrl = match.captured(1);
            }

            if (!lastJsUrl.isEmpty()) {
                Logger::Log(LogLevel::INFO, "SoundCloud: Found JS file: " + lastJsUrl.toStdString());
                ExtractClientIdFromJs(lastJsUrl);
            } else {
                emit ApiError("Could not find JS files on SoundCloud homepage.");
            }
        } else {
            emit ApiError("Failed to load SoundCloud homepage.");
        }
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
                Logger::Log(LogLevel::INFO, "SoundCloud: Successfully extracted client_id: " + m_clientId);

                ResolveProfileUrl();
            } else {
                emit ApiError("Could not extract client_id from JS file.");
            }
        } else {
            emit ApiError("Failed to load JS file.");
        }
        reply->deleteLater();
    });
}

void SoundCloudClient::ResolveProfileUrl() {
    QString resolveUrl = QString("https://api-v2.soundcloud.com/resolve?url=%1&client_id=%2")
                             .arg(QString::fromUtf8(QUrl::toPercentEncoding(m_profileUrl)),
                                  QString::fromStdString(m_clientId));

    QNetworkRequest request((QUrl(resolveUrl)));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply* reply = m_manager->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() == QNetworkReply::NoError) {
            QJsonDocument json = QJsonDocument::fromJson(reply->readAll());
            QJsonObject obj = json.object();

            if (obj.contains("id")) {
                m_userId = std::to_string(obj["id"].toInt());
                Logger::Log(LogLevel::INFO, "SoundCloud: Resolved user ID: " + m_userId);

                FetchAllUserAudio(0, 50);
            } else {
                emit ApiError("Could not find user ID in resolve response.");
            }
        } else {
            QString errStr = reply->errorString();
            QByteArray errBody = reply->readAll();
            Logger::Log(LogLevel::ERROR, "SC Resolve Error: " + errStr.toStdString());
            Logger::Log(LogLevel::ERROR, "SC Resolve Body: " + errBody.toStdString());

            emit ApiError("Failed to resolve profile: " + errStr.toStdString());
        }
        reply->deleteLater();
    });
}

void SoundCloudClient::SetAccessToken(const std::string&) {}

void SoundCloudClient::FetchAllUserAudio(int offset, int count) {
    QString url;

    if (offset == 0) {
        m_nextHref.clear();
        url = QString("https://api-v2.soundcloud.com/users/%1/likes?client_id=%2&limit=%3&linked_partitioning=1")
                  .arg(QString::fromStdString(m_userId), QString::fromStdString(m_clientId))
                  .arg(count);
    } else if (!m_nextHref.isEmpty()) {
        url = m_nextHref;
        if (!url.contains("client_id=")) {
            url += "&client_id=" + QString::fromStdString(m_clientId);
        }
    } else {
        emit FinishedFetching();
        return;
    }

    QNetworkRequest request((QUrl(url)));
    QNetworkReply* reply = m_manager->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply, offset, count]() {
        if (reply->error() != QNetworkReply::NoError) {
            Logger::Log(LogLevel::ERROR, "SoundCloud API Error: " + reply->errorString().toStdString());
            emit FinishedFetching();
            reply->deleteLater();
            return;
        }

        QJsonDocument json = QJsonDocument::fromJson(reply->readAll());
        QJsonObject root = json.object();
        QJsonArray collection = root["collection"].toArray();

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
            track.url = "";

            QString artwork = trackObj["artwork_url"].toString();
            if (artwork.isEmpty()) {
                artwork = trackObj["user"].toObject()["avatar_url"].toString();
            }
            if (!artwork.isEmpty()) {
                artwork.replace("-large.jpg", "-t500x500.jpg");
                track.coverUrl = artwork.toStdString();
            }

            chunkTracks.push_back(std::move(track));
        }

        if (!chunkTracks.empty()) {
            emit AudioFetched(chunkTracks);
            Logger::Log(LogLevel::INFO, "SoundCloud: Fetched " + std::to_string(chunkTracks.size()) + " tracks. Total loaded: " + std::to_string(offset + chunkTracks.size()));
        }
        // Если SC прислал ссылку на следующую страницу — сохраняем её и идем дальше
        if (root.contains("next_href") && !root["next_href"].isNull()) {
            m_nextHref = root["next_href"].toString();
            FetchAllUserAudio(offset + chunkTracks.size(), count);
        } else {
            emit FinishedFetching();
        }

        reply->deleteLater();
    });
}

void SoundCloudClient::FetchTrackUrl(const std::string& trackId, std::function<void(const std::string&, bool)> callback) {
    QString trackUrl = QString("https://api-v2.soundcloud.com/tracks/%1?client_id=%2")
                           .arg(QString::fromStdString(trackId), QString::fromStdString(m_clientId));

    QNetworkRequest request((QUrl(trackUrl)));
    QNetworkReply* reply = m_manager->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply, callback]() {
        if (reply->error() != QNetworkReply::NoError) {
            Logger::Log(LogLevel::ERROR, "SC FetchTrackUrl Error: " + reply->errorString().toStdString());
            callback("", true);
            reply->deleteLater();
            return;
        }

        QJsonDocument json = QJsonDocument::fromJson(reply->readAll());
        QJsonObject trackObj = json.object();
        QJsonArray transcodings = trackObj["media"].toObject()["transcodings"].toArray();

        QString trackAuth = trackObj["track_authorization"].toString();

        QString transUrl;

        for (const QJsonValue& val : transcodings) {
            QJsonObject trans = val.toObject();
            QString protocol = trans["format"].toObject()["protocol"].toString();
            if (protocol == "progressive") {
                transUrl = trans["url"].toString();
                break;
            }
            if (protocol == "hls" && transUrl.isEmpty()) {
                transUrl = trans["url"].toString();
            }
        }

        if (transUrl.isEmpty()) {
            Logger::Log(LogLevel::ERROR, "SoundCloud: No playable stream formats found for this track.");
            callback("", false);
            reply->deleteLater();
            return;
        }

        QUrl url(transUrl);
        QUrlQuery transQuery(url.query());
        transQuery.addQueryItem("client_id", QString::fromStdString(m_clientId));
        if (!trackAuth.isEmpty()) {
            transQuery.addQueryItem("track_authorization", trackAuth);
        }
        url.setQuery(transQuery);

        QNetworkRequest cdnReq(url);
        QNetworkReply* cdnReply = m_manager->get(cdnReq);

        connect(cdnReply, &QNetworkReply::finished, this, [cdnReply, callback]() {
            if (cdnReply->error() != QNetworkReply::NoError) {
                Logger::Log(LogLevel::ERROR, "SC CDN URL Error: " + cdnReply->errorString().toStdString());
                callback("", true);
                cdnReply->deleteLater();
                return;
            }

            QJsonDocument cdnJson = QJsonDocument::fromJson(cdnReply->readAll());
            std::string finalUrl = cdnJson.object()["url"].toString().toStdString();

            callback(finalUrl, false);
            cdnReply->deleteLater();
        });

        reply->deleteLater();
    });
}