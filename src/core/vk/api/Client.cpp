#include "Client.h"
#include "utils/logger/Logger.h"

#include <QUrl>
#include <QUrlQuery>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
#include <iostream>
#include <QEventLoop>

Client::Client(QObject* parent)
    : QObject(parent), m_manager(new QNetworkAccessManager(this)) {
    Logger::Log(LogLevel::INFO, "api created.");
}

Client::~Client() {
    Logger::Log(LogLevel::INFO, "api destroyed.");
}

void Client::SetAccessToken(const std::string& token) {
    m_accessToken = token;
}

void Client::ValidateToken(std::function<void(bool)> callback) {
    if (m_accessToken.empty()) {
        callback(false);
        return;
    }

    QUrl url("https://api.vk.com/method/users.get");
    QUrlQuery query;
    query.addQueryItem("v", QString::fromStdString(m_apiVersion));
    query.addQueryItem("access_token", QString::fromStdString(m_accessToken));
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply* reply = m_manager->get(request);

    connect(reply, &QNetworkReply::finished, this, [reply, callback]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            callback(false);
            return;
        }

        QByteArray response_data = reply->readAll();
        QJsonDocument json = QJsonDocument::fromJson(response_data);

        // Если ВК вернул ошибку, значит токен невалиден
        if (json.object().contains("error")) {
            Logger::Log(LogLevel::WARNING, "api: Token validation failed (API error).");
            callback(false);
        } else {
            Logger::Log(LogLevel::INFO, "api: Token is valid.");
            callback(true);
        }
    });
}

void Client::FetchUserAudio(long long ownerId, int count) {
    if (m_accessToken.empty()) {
        emit ApiError("Access token is missing!");
        return;
    }

    QUrl url("https://api.vk.com/method/audio.get");
    QUrlQuery query;
    if (ownerId != 0) {
        query.addQueryItem("owner_id", QString::number(ownerId));
    }
    query.addQueryItem("count", QString::number(count));
    query.addQueryItem("v", QString::fromStdString(m_apiVersion));
    query.addQueryItem("access_token", QString::fromStdString(m_accessToken));

    url.setQuery(query);
    QNetworkRequest request(url);

    request.setHeader(QNetworkRequest::UserAgentHeader, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");

    Logger::Log(LogLevel::INFO, "api: Fetching audio...");

    QNetworkReply* reply = m_manager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() { OnReplyFinished(reply); });
}

void Client::FetchTrackUrl(const std::string& trackId, std::function<void(const std::string&, bool)> callback) {
    QUrl url("https://api.vk.com/method/audio.getById");
    QUrlQuery query;
    query.addQueryItem("audios", QString::fromStdString(trackId));
    query.addQueryItem("access_token", QString::fromStdString(m_accessToken));
    query.addQueryItem("v", QString::fromStdString(m_apiVersion));
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "VKAndroidApp/5.56.1-12345 (Android 11; SDK 30; x86_64; en; 2274003)");
    request.setTransferTimeout(5000);

    QNetworkReply* reply = m_manager->get(request);

    QObject::connect(reply, &QNetworkReply::finished, this, [this, reply, callback]() {
        std::string freshUrl = "";
        bool isNetworkError = false;

        if (reply->error() == QNetworkReply::NoError) {
            QByteArray response_data = reply->readAll();
            QJsonDocument json = QJsonDocument::fromJson(response_data);
            QJsonObject root = json.object();

            if (root.contains("error")) {
                QJsonObject errObj = root["error"].toObject();
                std::string errMsg = errObj["error_msg"].toString().toStdString();
                int errCode = errObj["error_code"].toInt();
                Logger::Log(LogLevel::ERROR, "VK API Error [" + std::to_string(errCode) + "]: " + errMsg);

                if (errCode == 5) {
                    emit TokenExpired();
                }
            } else {
                QJsonArray responseArray = root["response"].toArray();
                if (!responseArray.isEmpty()) {
                    QJsonObject trackObj = responseArray[0].toObject();
                    freshUrl = trackObj["url"].toString().toStdString();
                }
            }
        } else {
            Logger::Log(LogLevel::ERROR, "Network error while fetching track URL: " + reply->errorString().toStdString());
            isNetworkError = true;
        }

        callback(freshUrl, isNetworkError);
        reply->deleteLater();
    });
}

void Client::OnReplyFinished(QNetworkReply* reply) {
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        std::string err = reply->errorString().toStdString();
        Logger::Log(LogLevel::ERROR, "api Network Error: " + err);
        emit ApiError(err);
        return;
    }

    QByteArray responseData = reply->readAll();
    QJsonDocument jsonDoc = QJsonDocument::fromJson(responseData);

    if (!jsonDoc.isObject()) {
        emit ApiError("Invalid JSON response");
        return;
    }

    QJsonObject rootObj = jsonDoc.object();

    if (rootObj.contains("error")) {
        QJsonObject errObj = rootObj["error"].toObject();
        std::string errMsg = errObj["error_msg"].toString().toStdString();
        Logger::Log(LogLevel::ERROR, "VK API Error: " + errMsg);
        emit ApiError(errMsg);
        return;
    }

    QJsonObject responseObj = rootObj["response"].toObject();
    QJsonArray itemsArray = responseObj["items"].toArray();

    std::vector<Track> tracks;
    for (int i = 0; i < itemsArray.size(); ++i) {
        QJsonObject trackObj = itemsArray[i].toObject();

        Track t;
        int audioId = trackObj["id"].toInt();
        int ownerId = trackObj["owner_id"].toInt();

        // СТРОГОЕ СООТВЕТСТВИЕ СТРУКТУРЕ Track ИЗ РЕПОЗИТОРИЯ
        t.id = std::to_string(ownerId) + "_" + std::to_string(audioId);
        t.ownerId = std::to_string(ownerId);
        t.artist = trackObj["artist"].toString().toStdString();
        t.title = trackObj["title"].toString().toStdString();
        t.url = trackObj["url"].toString().toStdString();
        t.duration = trackObj["duration"].toInt();
        t.coverUrl = "";
        t.lyrics_id = trackObj.contains("lyrics_id") ? std::to_string(trackObj["lyrics_id"].toInt()) : "";
        t.lyrics = "";

        if (trackObj.contains("album") && trackObj["album"].isObject()) {
            QJsonObject album = trackObj["album"].toObject();
            if (album.contains("thumb") && album["thumb"].isObject()) {
                QJsonObject thumb = album["thumb"].toObject();
                QStringList qualityKeys = {
                    "photo_1200", "photo_600", "photo_300",
                    "photo_270", "photo_135", "photo_68", "photo_34"
                };

                for (const QString& key : qualityKeys) {
                    if (thumb.contains(key)) {
                        t.coverUrl = thumb[key].toString().toStdString();
                        break;
                    }
                }
            }
        }

        if (audioId != 0) {
            tracks.push_back(t);
        }
    }

    Logger::Log(LogLevel::INFO, "api: Successfully parsed " + std::to_string(tracks.size()) + " tracks.");
    emit AudioFetched(tracks);
}

void Client::FetchAllUserAudio(int offset, int count) {
    QUrl url("https://api.vk.com/method/audio.get");
    QUrlQuery query;
    query.addQueryItem("access_token", QString::fromStdString(m_accessToken));
    query.addQueryItem("v", QString::fromStdString(m_apiVersion));
    query.addQueryItem("offset", QString::number(offset));
    query.addQueryItem("count", QString::number(count));
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");

    QNetworkReply* reply = m_manager->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply, offset, count]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            Logger::Log(LogLevel::ERROR, "Network error during audio.get: " + reply->errorString().toStdString());
            return;
        }

        QByteArray response_data = reply->readAll();
        QJsonDocument json = QJsonDocument::fromJson(response_data);
        QJsonObject root = json.object();

        if (root.contains("error")) {
            QJsonObject errObj = root["error"].toObject();
            int errCode = errObj["error_code"].toInt();
            std::string errMsg = errObj["error_msg"].toString().toStdString();

            Logger::Log(LogLevel::ERROR, "VK API Error [" + std::to_string(errCode) + "]: " + errMsg);

            if (errCode == 5) {
                emit TokenExpired();
            }
            return;
        }

        std::vector<Track> chunkTracks;
        QJsonObject responseObj = root["response"].toObject();
        QJsonArray items = responseObj["items"].toArray();

        for (const QJsonValue& val : items) {
            QJsonObject trackJson = val.toObject();
            Track track;

            int owner_id = trackJson["owner_id"].toInt();
            int audio_id = trackJson["id"].toInt();

            // СТРОГОЕ СООТВЕТСТВИЕ СТРУКТУРЕ Track ИЗ РЕПОЗИТОРИЯ
            track.id = std::to_string(owner_id) + "_" + std::to_string(audio_id);
            track.ownerId = std::to_string(owner_id);
            track.artist = trackJson["artist"].toString().toStdString();
            track.title = trackJson["title"].toString().toStdString();
            track.url = trackJson["url"].toString().toStdString();
            track.duration = trackJson["duration"].toInt();
            track.coverUrl = "";
            track.lyrics_id = trackJson.contains("lyrics_id") ? std::to_string(trackJson["lyrics_id"].toInt()) : "";
            track.lyrics = "";

            // Парсинг обложек
            if (trackJson.contains("album") && trackJson["album"].isObject()) {
                QJsonObject album = trackJson["album"].toObject();
                if (album.contains("thumb") && album["thumb"].isObject()) {
                    QJsonObject thumb = album["thumb"].toObject();
                    QStringList qualityKeys = {
                        "photo_1200", "photo_600", "photo_300",
                        "photo_270", "photo_135", "photo_68", "photo_34"
                    };

                    for (const QString& key : qualityKeys) {
                        if (thumb.contains(key)) {
                            track.coverUrl = thumb[key].toString().toStdString();
                            break;
                        }
                    }
                }
            }

            if (audio_id != 0) {
                chunkTracks.push_back(track);
            }
        }

        if (!chunkTracks.empty()) {
            emit AudioFetched(chunkTracks);
        }

        if (items.size() == count) {
            FetchAllUserAudio(offset + count, count);
        } else {
            emit FinishedFetching();
        }
    });
}