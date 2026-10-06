This file is a merged representation of a subset of the codebase, containing files not matching ignore patterns, combined into a single document by Repomix.

# File Summary

## Purpose
This file contains a packed representation of a subset of the repository's contents that is considered the most important context.
It is designed to be easily consumable by AI systems for analysis, code review,
or other automated processes.

## File Format
The content is organized as follows:
1. This summary section
2. Repository information
3. Directory structure
4. Repository files (if enabled)
5. Multiple file entries, each consisting of:
  a. A header with the file path (## File: path/to/file)
  b. The full contents of the file in a code block

## Usage Guidelines
- This file should be treated as read-only. Any changes should be made to the
  original repository files, not this packed version.
- When processing this file, use the file path to distinguish
  between different files in the repository.
- Be aware that this file may contain sensitive information. Handle it with
  the same level of security as you would the original repository.

## Notes
- Some files may have been excluded based on .gitignore rules and Repomix's configuration
- Binary files are not included in this packed representation. Please refer to the Repository Structure section for a complete list of file paths, including binary files
- Files matching these patterns are excluded: libs/*/**, **/miniaudio.h, **/*.md, .env
- Files matching patterns in .gitignore are excluded
- Files matching default ignore patterns are excluded
- Files are sorted by Git change count (files with more changes are at the bottom)

# Directory Structure
```
core/
  api/
    soundcloud/
      SoundCloudClient.cpp
      SoundCloudClient.h
    spotify/
      SpotifyClient.cpp
      SpotifyClient.h
    vk/
      VkClient.cpp
      VkClient.h
    yandex/
      YandexClient.cpp
      YandexClient.h
    IAudioProvider.h
  audio/
    bass/
      BassEngine.cpp
      BassEngine.h
    miniaudio/
      miniaudio_impl.cpp
      MiniaudioEngine.cpp
      MiniaudioEngine.h
    playback/
      PlaybackController.cpp
      PlaybackController.h
    IAudioEngine.h
  auth/
    oauth/
      OAuthManager.cpp
      OAuthManager.h
    router/
      SourceRouter.cpp
      SourceRouter.h
    auth.qml
  lyrics/
    LyricsFetcher.cpp
    LyricsFetcher.h
  playlist/
    PlaylistManager.cpp
    PlaylistManager.h
  shazam/
    ShazamSignatureBuilder.cpp
    ShazamSignatureBuilder.h
    signature.proto
models/
  Track.h
services/
  database/
    DatabaseManager.cpp
    DatabaseManager.h
  downloader/
    TrackDownloader.cpp
    TrackDownloader.h
  network/
    NetworkStreamer.cpp
    NetworkStreamer.h
ui/
  console/
    commands/
      CommandDispatcher.cpp
      CommandDispatcher.h
    core/
      ConsoleController.cpp
      ConsoleController.h
    view/
      ConsoleRenderer.cpp
      ConsoleRenderer.h
utils/
  buffer/
    RingBuffer.cpp
    RingBuffer.h
  env/
    EnvParser.cpp
    EnvParser.h
  logger/
    Logger.cpp
    Logger.h
  parser/
    MpegTsDemuxer.cpp
    MpegTsDemuxer.h
  path/
    PathManager.cpp
    PathManager.h
main.cpp
resources.qrc
```

# Files

## File: core/api/soundcloud/SoundCloudClient.cpp
```cpp
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

void SoundCloudClient::InitializeWithToken() {
    Logger::Log(LogLevel::INFO, "SoundCloud: Starting initialization with OAuth token.");
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

                FetchMe();
            } else {
                emit ApiError("Could not extract client_id from JS file.");
            }
        } else {
            emit ApiError("Failed to load JS file.");
        }
        reply->deleteLater();
    });
}

void SoundCloudClient::SetAccessToken(const std::string& token) {
    m_accessToken = token;
}

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
    request.setRawHeader("Authorization", QByteArray("OAuth ") + QByteArray::fromStdString(m_accessToken));
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

void SoundCloudClient::FetchMe() {
    QNetworkRequest request((QUrl("https://api-v2.soundcloud.com/me")));
    request.setRawHeader("Authorization", QByteArray("OAuth ") + QByteArray::fromStdString(m_accessToken));

    QNetworkReply* reply = m_manager->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() == QNetworkReply::NoError) {
            QJsonDocument json = QJsonDocument::fromJson(reply->readAll());
            m_userId = std::to_string(json.object()["id"].toInt());
            Logger::Log(LogLevel::INFO, "SoundCloud: Welcome back! User ID: " + m_userId);

            FetchAllUserAudio(0, 50);
        } else {
            emit ApiError("Failed to fetch profile info. Token might be invalid.");
        }
        reply->deleteLater();
    });
}
```

## File: core/api/soundcloud/SoundCloudClient.h
```c
#pragma once
#include "core/api/IAudioProvider.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QString>
#include <string>

class SoundCloudClient : public IAudioProvider {
    Q_OBJECT
public:
    explicit SoundCloudClient(QObject* parent = nullptr);
    ~SoundCloudClient() override;

    void InitializeWithToken();

    void SetAccessToken(const std::string& token) override;
    void FetchTrackUrl(const std::string& trackId, std::function<void(const std::string&, bool)> callback) override;
    void FetchAllUserAudio(int offset = 0, int count = 200) override;

private:
    void FetchClientId();
    void ExtractClientIdFromJs(const QString& jsUrl);
    void FetchMe();

    QNetworkAccessManager* m_manager;
    std::string m_clientId;
    std::string m_accessToken;
    std::string m_userId;
    QString m_nextHref;
};
```

## File: core/api/spotify/SpotifyClient.cpp
```cpp
#include "SpotifyClient.h"
#include "utils/logger/Logger.h"
#include <QUrl>
#include <QUrlQuery>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QCryptographicHash>
#include <QRandomGenerator>

SpotifyClient::SpotifyClient(QObject* parent) : IAudioProvider(parent), m_manager(new QNetworkAccessManager(this)) {
    Logger::Log(LogLevel::INFO, "SpotifyClient created.");
}

SpotifyClient::~SpotifyClient() {
    Logger::Log(LogLevel::INFO, "SpotifyClient destroyed.");
}

void SpotifyClient::ValidateToken(std::function<void(bool)> callback) {
    if (m_accessToken.empty()) {
        callback(false);
        return;
    }

    QUrl url("https://api.spotify.com/v1/me");
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", QByteArray("Bearer ") + QString::fromStdString(m_accessToken).toUtf8());

    QNetworkReply* reply = m_manager->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply, callback]() {
        if (reply->error() == QNetworkReply::NoError) {
            Logger::Log(LogLevel::INFO, "Spotify: Token is valid.");
            callback(true);
        } else {
            Logger::Log(LogLevel::WARNING, "Spotify: Token validation failed.");
            callback(false);
        }
        reply->deleteLater();
    });
}

void SpotifyClient::SetAccessToken(const std::string& token) {
    m_accessToken = token;
}

void SpotifyClient::FetchTrackUrl(const std::string& trackId, std::function<void(const std::string&, bool)> callback) {
    // ЗАГЛУШКА
    callback("", false);
}

void SpotifyClient::FetchAllUserAudio(int offset, int count) {
    QUrl url(QString("https://api.spotify.com/v1/me/tracks?limit=50&offset=%1").arg(offset));

    QNetworkRequest request(url);
    request.setRawHeader("Authorization", QByteArray("Bearer ") + QByteArray::fromStdString(m_accessToken));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    request.setHeader(QNetworkRequest::UserAgentHeader, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
    request.setRawHeader("Accept", "*/*");
    request.setRawHeader("App-Platform", "WebPlayer");

    QNetworkReply* reply = m_manager->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply, offset]() {
        if (reply->error() != QNetworkReply::NoError) {
            QByteArray errBody = reply->readAll();
            Logger::Log(LogLevel::ERROR, "Spotify API Error: " + reply->errorString().toStdString());
            Logger::Log(LogLevel::ERROR, "Spotify Error Body: " + errBody.toStdString());

            if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 401) {
                emit AuthError("Token expired (401)");
            }
            reply->deleteLater();
            return;
        }

        QJsonDocument json = QJsonDocument::fromJson(reply->readAll());
        if (json.isNull() || !json.isObject()) {
            Logger::Log(LogLevel::ERROR, "Spotify API: Received invalid JSON");
            emit FinishedFetching();
            reply->deleteLater();
            return;
        }

        QJsonObject root = json.object();

        if (!root.contains("items") || !root["items"].isArray()) {
            Logger::Log(LogLevel::WARNING, "Spotify API: 'items' array missing or empty");
            emit FinishedFetching();
            reply->deleteLater();
            return;
        }

        QJsonArray items = root["items"].toArray();
        std::vector<Track> chunkTracks;
        chunkTracks.reserve(items.size());

        for (const QJsonValue& val : items) {
            if (!val.isObject()) continue;
            QJsonObject itemObj = val.toObject();

            if (!itemObj.contains("track") || !itemObj["track"].isObject()) continue;

            QJsonObject trackObj = itemObj["track"].toObject();
            Track track;

            // Извлекаем мета данные
            track.id = trackObj["id"].toString().toStdString();
            track.source = "Spotify";
            track.title = trackObj.value("name").toString().toStdString();
            track.duration = trackObj.value("duration_ms").toInt(0) / 1000;
            track.ownerId = "spotify";
            track.url = "";

            // парсинг артистов
            if (trackObj.contains("artists") && trackObj["artists"].isArray()) {
                QJsonArray artistsArray = trackObj["artists"].toArray();
                QString artistName;
                for (int i = 0; i < artistsArray.size(); ++i) {
                    if (i > 0) artistName += ", ";
                    artistName += artistsArray[i].toObject().value("name").toString();
                }
                track.artist = artistName.toStdString();
            }

            // парсинг обложки
            if (trackObj.contains("album") && trackObj["album"].isObject()) {
                QJsonObject albumObj = trackObj["album"].toObject();
                if (albumObj.contains("images") && albumObj["images"].isArray()) {
                    QJsonArray imagesArray = albumObj["images"].toArray();
                    if (!imagesArray.isEmpty()) {
                        track.coverUrl = imagesArray[0].toObject().value("url").toString().toStdString();
                    }
                }
            }

            if (!track.id.empty() && !track.title.empty()) {
                chunkTracks.push_back(std::move(track));
            }
        }

        if (!chunkTracks.empty()) {
            emit AudioFetched(chunkTracks);
        }

        int total = root.value("total").toInt(0);
        Logger::Log(LogLevel::INFO, "Spotify: Fetched chunk offset " + std::to_string(offset) + ", items: " + std::to_string(items.size()));

        if (offset + items.size() < total && !items.isEmpty()) {
            FetchAllUserAudio(offset + items.size(), 50);
        } else {
            emit FinishedFetching();
        }

        reply->deleteLater();
    });
}

std::string SpotifyClient::StartAuthPkce(const QString& clientId) {
    m_clientId = clientId;

    // 1. Генерируем Code Verifier
    const QString possibleChars("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~");
    m_codeVerifier.clear();
    for (int i = 0; i < 64; ++i) {
        int index = QRandomGenerator::global()->generate() % possibleChars.length();
        m_codeVerifier.append(possibleChars.at(index));
    }

    // 2. Генерируем Code Challenge
    QByteArray hash = QCryptographicHash::hash(m_codeVerifier.toUtf8(), QCryptographicHash::Sha256);
    QString codeChallenge = hash.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);

    // 3. Собираем URL для окна авторизации
    QUrl url("https://accounts.spotify.com/authorize");
    QUrlQuery query;
    query.addQueryItem("client_id", m_clientId);
    query.addQueryItem("response_type", "code");
    query.addQueryItem("redirect_uri", "http://127.0.0.1:8080/callback");
    query.addQueryItem("code_challenge_method", "S256");
    query.addQueryItem("code_challenge", codeChallenge);
    query.addQueryItem("scope", "user-library-read");
    url.setQuery(query);

    return url.toString().toStdString();
}

void SpotifyClient::ExchangeCodeForToken(const std::string& code) {
    QUrl url("https://accounts.spotify.com/api/token");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    QByteArray postData;
    postData.append("client_id=" + QUrl::toPercentEncoding(m_clientId));
    postData.append("&grant_type=authorization_code");
    postData.append("&code=" + QUrl::toPercentEncoding(QString::fromStdString(code)));
    postData.append("&redirect_uri=" + QUrl::toPercentEncoding("http://127.0.0.1:8080/callback"));
    postData.append("&code_verifier=" + QUrl::toPercentEncoding(m_codeVerifier));

    QNetworkReply* reply = m_manager->post(request, postData);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() == QNetworkReply::NoError) {
            QJsonDocument json = QJsonDocument::fromJson(reply->readAll());
            std::string token = json.object()["access_token"].toString().toStdString();

            if (!token.empty()) {
                Logger::Log(LogLevel::INFO, "Spotify: Access Token successfully obtained via PKCE!");
                emit TokenReceived(token);
            } else {
                emit AuthError("Access token not found in response.");
            }
        } else {
            std::string err = reply->errorString().toStdString();
            std::string errBody = reply->readAll().toStdString();
            Logger::Log(LogLevel::ERROR, "Spotify PKCE Token Exchange failed: " + err);
            Logger::Log(LogLevel::ERROR, "Spotify Exchange Body: " + errBody);
            emit AuthError(err);
        }
        reply->deleteLater();
    });
}

void SpotifyClient::AuthWithSpDc(const QString& spDcCookie) {
    QUrl url("https://open.spotify.com/get_access_token?reason=transport&productType=web_player");
    QNetworkRequest request(url);

    request.setRawHeader("Cookie", QByteArray("sp_dc=") + spDcCookie.toUtf8());
    request.setHeader(QNetworkRequest::UserAgentHeader, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("App-Platform", "WebPlayer");
    request.setRawHeader("Origin", "https://open.spotify.com");
    request.setRawHeader("Referer", "https://open.spotify.com/");
    request.setRawHeader("Sec-Fetch-Dest", "empty");
    request.setRawHeader("Sec-Fetch-Mode", "cors");
    request.setRawHeader("Sec-Fetch-Site", "same-origin");

    QNetworkReply* reply = m_manager->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() == QNetworkReply::NoError) {
            QJsonDocument json = QJsonDocument::fromJson(reply->readAll());

            if (json.isNull() || !json.isObject()) {
                emit AuthError("Invalid JSON response from Spotify.");
                return;
            }

            std::string token = json.object()["accessToken"].toString().toStdString();

            if (!token.empty()) {
                Logger::Log(LogLevel::INFO, "Spotify: Web Access Token successfully obtained via sp_dc!");
                emit TokenReceived(token);
            } else {
                emit AuthError("Token not found in response.");
            }
        } else {
            std::string err = reply->errorString().toStdString();
            std::string errBody = reply->readAll().toStdString();
            Logger::Log(LogLevel::ERROR, "Spotify Web Auth failed: " + err);
            Logger::Log(LogLevel::ERROR, "Spotify Web Auth Body: " + errBody);
            emit AuthError(err);
        }
        reply->deleteLater();
    });
}
```

## File: core/api/spotify/SpotifyClient.h
```c
#pragma once
#include "core/api/IAudioProvider.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QString>

class SpotifyClient : public IAudioProvider {
    Q_OBJECT
public:
    explicit SpotifyClient(QObject* parent = nullptr);
    ~SpotifyClient() override;
    void AuthWithSpDc(const QString& spDcCookie);
    std::string StartAuthPkce(const QString& clientId);
    void ExchangeCodeForToken(const std::string& code);

    void ValidateToken(std::function<void(bool)> callback);
    void SetAccessToken(const std::string& token) override;
    void FetchTrackUrl(const std::string& trackId, std::function<void(const std::string&, bool)> callback) override;
    void FetchAllUserAudio(int offset = 0, int count = 200) override;

    signals:
        void TokenReceived(const std::string& token);
        void AuthError(const std::string& errorString);

private:
    QNetworkAccessManager* m_manager;
    std::string m_accessToken;

    QString m_clientId;
    QString m_codeVerifier;
};
```

## File: core/api/vk/VkClient.cpp
```cpp
#include "VkClient.h"
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

VkClient::VkClient(QObject* parent) : IAudioProvider(parent), m_manager(new QNetworkAccessManager(this)) {
    Logger::Log(LogLevel::INFO, "api created.");
}

VkClient::~VkClient() {
    Logger::Log(LogLevel::INFO, "api destroyed.");
}

void VkClient::SetAccessToken(const std::string& token) {
    m_accessToken = token;
}

void VkClient::ValidateToken(std::function<void(bool)> callback) {
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

        if (json.isNull() || !json.isObject()) {
            Logger::Log(LogLevel::ERROR, "VK API: Received invalid JSON in ValidateToken");
            callback(false);
            return;
        }

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

void VkClient::FetchTrackUrl(const std::string& trackId, std::function<void(const std::string&, bool)> callback) {
    QUrl url("https://api.vk.com/method/audio.getById");
    QUrlQuery query;
    query.addQueryItem("audios", QString::fromStdString(trackId));
    query.addQueryItem("access_token", QString::fromStdString(m_accessToken));
    query.addQueryItem("v", QString::fromStdString(m_apiVersion));
    url.setQuery(query);

    Logger::Log(LogLevel::INFO, "http запрос: " + url.toString().toStdString());

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

            if (json.isNull() || !json.isObject()) {
                Logger::Log(LogLevel::ERROR, "VK API: Received invalid JSON in FetchTrackUrl");
                callback("", true);
                reply->deleteLater();
                return;
            }

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

void VkClient::OnReplyFinished(QNetworkReply* reply) {
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
    tracks.reserve(itemsArray.size());
    for (int i = 0; i < itemsArray.size(); ++i) {
        QJsonObject trackObj = itemsArray[i].toObject();

        Track t;
        int audioId = trackObj["id"].toInt();
        int ownerId = trackObj["owner_id"].toInt();

        t.id = std::to_string(ownerId) + "_" + std::to_string(audioId);
        t.source = "VK";
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

void VkClient::FetchAllUserAudio(int offset, int count) {
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

        if (json.isNull() || !json.isObject()) {
            Logger::Log(LogLevel::ERROR, "VK API: Received invalid JSON in FetchAllUserAudio");
            emit FinishedFetching();
            return;
        }

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
        chunkTracks.reserve(items.size());

        for (const QJsonValue& val : items) {
            QJsonObject trackJson = val.toObject();
            Track track;

            int owner_id = trackJson["owner_id"].toInt();
            int audio_id = trackJson["id"].toInt();

            track.id = std::to_string(owner_id) + "_" + std::to_string(audio_id);
            track.source = "VK";
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
```

## File: core/api/vk/VkClient.h
```c
#pragma once
#include "core/api/IAudioProvider.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>

class VkClient : public IAudioProvider {
    Q_OBJECT
public:
    explicit VkClient(QObject* parent = nullptr);
    ~VkClient() override;

    void SetAccessToken(const std::string& token) override;
    void ValidateToken(std::function<void(bool isValid)> callback);

    void FetchTrackUrl(const std::string& trackId, std::function<void(const std::string&, bool isNetworkError)> callback) override;
    void FetchAllUserAudio(int offset = 0, int count = 200) override;

private slots:
    void OnReplyFinished(QNetworkReply* reply);

private:
    QNetworkAccessManager* m_manager;
    std::string m_accessToken;
    std::string m_apiVersion = "5.131";
};
```

## File: core/api/yandex/YandexClient.cpp
```cpp
#include "YandexClient.h"
#include "utils/logger/Logger.h"

#include <QUrl>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QCryptographicHash>
#include <QXmlStreamReader>

YandexClient::YandexClient(QObject* parent) 
    : IAudioProvider(parent), m_manager(new QNetworkAccessManager(this)) {
    Logger::Log(LogLevel::INFO, "YandexClient created.");
}

YandexClient::~YandexClient() {
    Logger::Log(LogLevel::INFO, "YandexClient destroyed.");
}

void YandexClient::SetAccessToken(const std::string& token) {
    m_accessToken = token;
}

void YandexClient::FetchAllUserAudio(int offset, int count) {
    if (m_accessToken.empty()) {
        Logger::Log(LogLevel::ERROR, "Yandex: Token is empty!");
        emit FinishedFetching();
        return;
    }

    if (m_userId.empty()) {
        FetchUserId();
    } else {
        FetchLikesIds(offset, count);
    }
}

void YandexClient::FetchUserId() {
    QUrl url("https://api.music.yandex.net/account/status");
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", QByteArray("OAuth ") + QByteArray::fromStdString(m_accessToken));

    QNetworkReply* reply = m_manager->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() == QNetworkReply::NoError) {
            QJsonDocument json = QJsonDocument::fromJson(reply->readAll());
            m_userId = std::to_string(json.object()["result"].toObject()["account"].toObject()["uid"].toInt());
            Logger::Log(LogLevel::INFO, "Yandex: Successfully got User ID: " + m_userId);

            FetchLikesIds(0, 200);
        } else {
            Logger::Log(LogLevel::ERROR, "Yandex API Error: Failed to fetch user status.");
            emit ApiError("Failed to fetch Yandex status");
        }
        reply->deleteLater();
    });
}

void YandexClient::FetchLikesIds(int offset, int count) {
    QUrl url(QString("https://api.music.yandex.net/users/%1/likes/tracks").arg(QString::fromStdString(m_userId)));
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", QByteArray("OAuth ") + QByteArray::fromStdString(m_accessToken));
    request.setHeader(QNetworkRequest::UserAgentHeader, "Yandex-Music-API");

    QNetworkReply* reply = m_manager->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply, offset, count]() {
        if (reply->error() == QNetworkReply::NoError) {
            QByteArray responseData = reply->readAll();
            QJsonDocument json = QJsonDocument::fromJson(responseData);
            QJsonArray tracksArray = json.object()["result"].toObject()["library"].toObject()["tracks"].toArray();

            Logger::Log(LogLevel::INFO, "Yandex: В лайках найдено треков: " + std::to_string(tracksArray.size()));

            QStringList chunkIds;
            for (int i = offset; i < offset + count && i < tracksArray.size(); ++i) {
                // Универсальный парсинг ID (Яндекс может отдавать их и строками, и числами)
                QJsonValue idVal = tracksArray[i].toObject()["id"];
                QString trackId = idVal.isString() ? idVal.toString() : QString::number(idVal.toInt());
                chunkIds.append(trackId);
            }

            if (!chunkIds.isEmpty()) {
                FetchTracksMetadata(chunkIds);

                if (offset + count < tracksArray.size()) {
                    FetchLikesIds(offset + count, count);
                }
            } else {
                Logger::Log(LogLevel::WARNING, "Yandex: Очередь ID пуста. Сырой ответ: " + responseData.left(200).toStdString());
                emit FinishedFetching();
            }
        } else {
            Logger::Log(LogLevel::ERROR, "Yandex API Error (FetchLikes): " + reply->errorString().toStdString());
            Logger::Log(LogLevel::ERROR, "Yandex API Body: " + reply->readAll().toStdString());
            emit FinishedFetching();
        }
        reply->deleteLater();
    });
}

void YandexClient::FetchTracksMetadata(const QStringList& trackIds) {
    QUrl url("https://api.music.yandex.net/tracks");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    request.setRawHeader("Authorization", QByteArray("OAuth ") + QByteArray::fromStdString(m_accessToken));
    request.setHeader(QNetworkRequest::UserAgentHeader, "Yandex-Music-API");

    QByteArray postData = "track-ids=" + trackIds.join(",").toUtf8();
    QNetworkReply* reply = m_manager->post(request, postData);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() == QNetworkReply::NoError) {
            QJsonDocument json = QJsonDocument::fromJson(reply->readAll());
            QJsonArray items = json.object()["result"].toArray();

            std::vector<Track> chunkTracks;
            chunkTracks.reserve(items.size());

            for (const QJsonValue& val : items) {
                QJsonObject trackObj = val.toObject();
                Track track;

                QJsonValue idVal = trackObj["id"];
                track.id = idVal.isString() ? idVal.toString().toStdString() : std::to_string(idVal.toInt());

                track.source = "Yandex";
                track.title = trackObj["title"].toString().toStdString();
                track.duration = trackObj["durationMs"].toInt() / 1000;
                track.url = "";

                if (trackObj.contains("artists") && trackObj["artists"].isArray()) {
                    QJsonArray artistsArray = trackObj["artists"].toArray();
                    QString artistName;
                    for (int i = 0; i < artistsArray.size(); ++i) {
                        if (i > 0) artistName += ", ";
                        artistName += artistsArray[i].toObject()["name"].toString();
                    }
                    track.artist = artistName.toStdString();
                }

                QString coverUri = trackObj["coverUri"].toString();
                if (!coverUri.isEmpty()) {
                    coverUri.replace("%%", "200x200");
                    track.coverUrl = "https://" + coverUri.toStdString();
                }

                if (!track.id.empty() && !track.title.empty()) {
                    chunkTracks.push_back(std::move(track));
                }
            }

            if (!chunkTracks.empty()) {
                Logger::Log(LogLevel::INFO, "Yandex: Fetched metadata for " + std::to_string(chunkTracks.size()) + " tracks.");
                emit AudioFetched(chunkTracks);
            } else {
                Logger::Log(LogLevel::WARNING, "Yandex: Metadata array is empty!");
            }
        } else {
            Logger::Log(LogLevel::ERROR, "Yandex API Error (FetchMeta): " + reply->errorString().toStdString());
            Logger::Log(LogLevel::ERROR, "Yandex API Body: " + reply->readAll().toStdString());
        }
        reply->deleteLater();
    });
}

void YandexClient::FetchTrackUrl(const std::string& trackId, std::function<void(const std::string&, bool)> callback) {
    QUrl url(QString("https://api.music.yandex.net/tracks/%1/download-info").arg(QString::fromStdString(trackId)));
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", QByteArray("OAuth ") + QByteArray::fromStdString(m_accessToken));

    QNetworkReply* reply = m_manager->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply, callback]() {
        if (reply->error() != QNetworkReply::NoError) {
            Logger::Log(LogLevel::ERROR, "Yandex API Error: Failed to fetch download info");
            callback("", true);
            reply->deleteLater();
            return;
        }

        QJsonDocument json = QJsonDocument::fromJson(reply->readAll());
        QJsonArray result = json.object()["result"].toArray();

        QString downloadInfoUrl;
        for (const QJsonValue& val : result) {
            QJsonObject obj = val.toObject();
            if (obj["codec"].toString() == "mp3") {
                downloadInfoUrl = obj["downloadInfoUrl"].toString();
                break;
            }
        }

        if (downloadInfoUrl.isEmpty()) {
            Logger::Log(LogLevel::ERROR, "Yandex: No mp3 codec found for track");
            callback("", false);
            reply->deleteLater();
            return;
        }

        // Получаем XML-файл с нодами сервера и солью
        QNetworkRequest xmlRequest((QUrl(downloadInfoUrl)));
        xmlRequest.setRawHeader("Authorization", QByteArray("OAuth ") + QByteArray::fromStdString(m_accessToken));
        QNetworkReply* xmlReply = m_manager->get(xmlRequest);

        connect(xmlReply, &QNetworkReply::finished, this, [xmlReply, callback]() {
            if (xmlReply->error() != QNetworkReply::NoError) {
                Logger::Log(LogLevel::ERROR, "Yandex API Error: Failed to fetch XML node info");
                callback("", true);
                xmlReply->deleteLater();
                return;
            }

            QString host, path, ts, s;
            QXmlStreamReader xml(xmlReply->readAll());
            while (!xml.atEnd() && !xml.hasError()) {
                QXmlStreamReader::TokenType token = xml.readNext();
                if (token == QXmlStreamReader::StartElement) {
                    if (xml.name() == QString("host")) host = xml.readElementText();
                    else if (xml.name() == QString("path")) path = xml.readElementText();
                    else if (xml.name() == QString("ts")) ts = xml.readElementText();
                    else if (xml.name() == QString("s")) s = xml.readElementText();
                }
            }

            if (host.isEmpty() || path.isEmpty() || ts.isEmpty() || s.isEmpty()) {
                Logger::Log(LogLevel::ERROR, "Yandex: Failed to parse XML node data");
                callback("", false);
                xmlReply->deleteLater();
                return;
            }

            // Формируем MD5 подпись
            QString magicStr = "XGRlBW9FXlekgbPrRHuAle";
            QString signData = magicStr + path.mid(1) + s;

            QByteArray hash = QCryptographicHash::hash(signData.toUtf8(), QCryptographicHash::Md5);
            QString sign = hash.toHex();
            QString finalUrl = "https://" + host + "/get-mp3/" + sign + "/" + ts + path;

            Logger::Log(LogLevel::INFO, "Yandex: Successfully generated track URL");
            callback(finalUrl.toStdString(), false);

            xmlReply->deleteLater();
        });

        reply->deleteLater();
    });
}
```

## File: core/api/yandex/YandexClient.h
```c
#pragma once
#include "core/api/IAudioProvider.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QString>
#include <string>

class YandexClient : public IAudioProvider {
    Q_OBJECT
public:
    explicit YandexClient(QObject* parent = nullptr);
    ~YandexClient() override;

    void SetAccessToken(const std::string& token) override;

    void FetchTrackUrl(const std::string& trackId, std::function<void(const std::string&, bool)> callback) override;
    void FetchAllUserAudio(int offset = 0, int count = 200) override;

private:
    void FetchUserId();
    void FetchLikesIds(int offset, int count);
    void FetchTracksMetadata(const QStringList& trackIds);

    QNetworkAccessManager* m_manager;
    std::string m_accessToken;
    std::string m_userId;
};
```

## File: core/api/IAudioProvider.h
```c
#pragma once
#include <QObject>
#include <string>
#include <vector>
#include <functional>
#include "models/Track.h"

class IAudioProvider : public QObject {
    Q_OBJECT
public:
    explicit IAudioProvider(QObject* parent = nullptr) : QObject(parent) {}
    virtual ~IAudioProvider() = default;

    virtual void SetAccessToken(const std::string& token) = 0;
    virtual void FetchTrackUrl(const std::string& trackId, std::function<void(const std::string& url, bool isNetworkError)> callback) = 0;
    virtual void FetchAllUserAudio(int offset = 0, int count = 200) = 0;

    signals:
        // Общие сигналы для всех сервисов
        void AudioFetched(const std::vector<Track>& tracks);
    void ApiError(const std::string& errorMessage);
    void TokenExpired();
    void FinishedFetching();
};
```

## File: core/audio/bass/BassEngine.cpp
```cpp
#include "BassEngine.h"
#include "utils/logger/Logger.h"
#include "utils/path/PathManager.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QSettings>
#include <QFile>
#include <QString>
#include <QFileInfo>
#include <QUrl>

BassEngine::BassEngine() {
}

BassEngine::~BassEngine() {
    if (m_activeStream != 0) {
        if (m_syncEnd != 0) BASS_ChannelRemoveSync(m_activeStream, m_syncEnd);
        BASS_StreamFree(m_activeStream);
    }
    if (m_fadingStream != 0) {
        BASS_StreamFree(m_fadingStream);
    }
    if (m_hlsPlugin != 0) {
        BASS_PluginFree(m_hlsPlugin);
    }
    BASS_Free();
    Logger::Log(LogLevel::INFO, "bass: BASS freed.");
}

bool BassEngine::Init() {
    QSettings settings("config.ini", QSettings::IniFormat);
    int netTimeout = settings.value("Audio/NetTimeout", 5000).toInt();
    int netReadTimeout = settings.value("Audio/NetReadTimeout", 5000).toInt();
    m_crossfadeDurationMs = settings.value("Audio/CrossfadeDurationMs", 3000).toInt();

    BASS_SetConfig(BASS_CONFIG_NET_TIMEOUT, netTimeout);
    BASS_SetConfig(BASS_CONFIG_NET_READTIMEOUT, netReadTimeout);
    BASS_SetConfig(BASS_CONFIG_NET_PLAYLIST, 0);
    BASS_SetConfig(BASS_CONFIG_NET_BUFFER, 20000);
    BASS_SetConfig(BASS_CONFIG_NET_PREBUF, 50);
    BASS_SetConfigPtr(BASS_CONFIG_NET_AGENT, "VKAndroidApp/5.56.1-12345 (Android 11; SDK 30; x86_64; en; 2274003)");

    if (!BASS_Init(-1, 44100, 0, 0, nullptr)) {
        Logger::Log(LogLevel::ERROR, "bass: Failed to initialize BASS!");
        return false;
    }

    m_hlsPlugin = BASS_PluginLoad("basshls.dll", 0);
    if (!m_hlsPlugin) {
        Logger::Log(LogLevel::WARNING, "bass: Could not load basshls.dll plugin! Local HLS might fail.");
    }

    return true;
}

void CALLBACK BassEngine::BassTrackNearEndCallback(HSYNC handle, DWORD channel, DWORD data, void* user) {
    BassEngine* engine = static_cast<BassEngine*>(user);
    if (engine->OnTrackNearEnd) {
        QMetaObject::invokeMethod(QCoreApplication::instance(), [engine]() {
            engine->OnTrackNearEnd();
        }, Qt::QueuedConnection);
    }
}

void CALLBACK BassEngine::BassTrackEndCallback(HSYNC handle, DWORD channel, DWORD data, void* user) {
    Logger::Log(LogLevel::INFO, "bass: --- TRACK COMPLETION EVENT ---");
    BassEngine* engine = static_cast<BassEngine*>(user);
    if (engine->OnTrackFinished) {
        QMetaObject::invokeMethod(QCoreApplication::instance(), [engine]() {
            engine->OnTrackFinished();
        }, Qt::QueuedConnection);
    }
}

bool BassEngine::PlayStream(const std::string& url, int durationSec, bool crossfade, const std::string& trackId) {
    if (m_activeStream != 0) {
        if (m_syncEnd != 0) BASS_ChannelRemoveSync(m_activeStream, m_syncEnd);
        if (m_syncNearEnd != 0) BASS_ChannelRemoveSync(m_activeStream, m_syncNearEnd);
        if (m_syncCrossfade != 0) BASS_ChannelRemoveSync(m_activeStream, m_syncCrossfade);

        if (crossfade) {
            // Если предыдущий трек еще затухает, убиваем его
            if (m_fadingStream != 0) BASS_StreamFree(m_fadingStream);

            m_fadingStream = m_activeStream;

            // Плавно глушим старый трек за m_crossfadeDurationMs мс
            BASS_ChannelSlideAttribute(m_fadingStream, BASS_ATTRIB_VOL, 0.0f, m_crossfadeDurationMs);

            // Ставим хук: как только громкость упадет до 0, вычищаем трек из памяти
            BASS_ChannelSetSync(m_fadingStream, BASS_SYNC_SLIDE | BASS_SYNC_ONETIME, 0,
                [](HSYNC handle, DWORD channel, DWORD data, void* user) {
                    BASS_StreamFree(channel);
                    Logger::Log(LogLevel::INFO, "bass: Faded track properly freed.");
                }, nullptr);
        } else {
            BASS_StreamFree(m_activeStream);
        }

        m_activeStream = 0;
        m_syncEnd = 0;
        m_syncNearEnd = 0;
    }

    // --- ЛОГИКА ОФФЛАЙН ВОСПРОИЗВЕДЕНИЯ ---
    QString localPath = PathManager::GetDownloadFilePath(trackId, "mp3");
    if (!QFile::exists(localPath)) {
        localPath = PathManager::GetDownloadFilePath(trackId, "aac");
    }

    // играем с диска
    if (!trackId.empty() && QFile::exists(localPath)) {
        Logger::Log(LogLevel::INFO, "bass: Playing from local downloads -> " + localPath.toStdString());
        m_activeStream = BASS_StreamCreateFile(FALSE, localPath.toStdString().c_str(), 0, 0, 0);
    }
    // Иначе из интернета
    else {
        m_activeStream = BASS_StreamCreateURL(url.c_str(), 0, 0, nullptr, nullptr);
    }

    if (m_activeStream != 0) {
        if (crossfade) {
            // Новый трек стартует с 0 громкости и нарастает
            BASS_ChannelSetAttribute(m_activeStream, BASS_ATTRIB_VOL, 0.0f);
            BASS_ChannelSlideAttribute(m_activeStream, BASS_ATTRIB_VOL, m_volume, m_crossfadeDurationMs);
        } else {
            BASS_ChannelSetAttribute(m_activeStream, BASS_ATTRIB_VOL, m_volume);
        }

        m_syncEnd = BASS_ChannelSetSync(m_activeStream, BASS_SYNC_END, 0, &BassEngine::BassTrackEndCallback, this);

        QWORD length = BASS_ChannelGetLength(m_activeStream, BASS_POS_BYTE);
        double actualDuration = (length != (QWORD)-1) ? BASS_ChannelBytes2Seconds(m_activeStream, length) : static_cast<double>(durationSec);

        double crossfadeSec = m_crossfadeDurationMs / 1000.0;

        if (crossfade && actualDuration > crossfadeSec) {
            QWORD crossfadePos = BASS_ChannelSeconds2Bytes(m_activeStream, actualDuration - crossfadeSec);
            m_syncCrossfade = BASS_ChannelSetSync(m_activeStream, BASS_SYNC_POS | BASS_SYNC_ONETIME, crossfadePos, &BassEngine::BassTrackEndCallback, this);
        }

        // Хук предзагрузки за 10 сек до конца
        if (actualDuration > 10.0) {
            QWORD prefetchPos = BASS_ChannelSeconds2Bytes(m_activeStream, actualDuration - 10.0);
            m_syncNearEnd = BASS_ChannelSetSync(m_activeStream, BASS_SYNC_POS, prefetchPos, &BassEngine::BassTrackNearEndCallback, this);
        }

        BASS_ChannelPlay(m_activeStream, FALSE);
        return true;
    }

    Logger::Log(LogLevel::ERROR, "bass: Failed to create stream! Error: " + std::to_string(BASS_ErrorGetCode()));
    return false;
}

void BassEngine::SetPositionSeconds(double pos) {
    if (m_activeStream != 0) {
        QWORD bytePos = BASS_ChannelSeconds2Bytes(m_activeStream, pos);
        BASS_ChannelSetPosition(m_activeStream, bytePos, BASS_POS_BYTE);

        QWORD length = BASS_ChannelGetLength(m_activeStream, BASS_POS_BYTE);
        if (length != (QWORD)-1) {
            QWORD prefetchPos = length - BASS_ChannelSeconds2Bytes(m_activeStream, 10.0);
            if (bytePos >= prefetchPos && OnTrackNearEnd) {
                QMetaObject::invokeMethod(QCoreApplication::instance(), [this]() {
                    this->OnTrackNearEnd();
                }, Qt::QueuedConnection);
            }
        }
    }
}

void BassEngine::Pause() {
    if (m_activeStream != 0) BASS_ChannelPause(m_activeStream);
}

void BassEngine::Resume() {
    if (m_activeStream != 0) BASS_ChannelPlay(m_activeStream, FALSE);
}

void BassEngine::SetVolume(float volume) {
    m_volume = volume;
    if (m_volume < 0.0f) m_volume = 0.0f;
    if (m_volume > 1.0f) m_volume = 1.0f;
    if (m_activeStream != 0) BASS_ChannelSetAttribute(m_activeStream, BASS_ATTRIB_VOL, m_volume);
}

float BassEngine::GetVolume() const { return m_volume; }

bool BassEngine::IsPlaying() const {
    return m_activeStream != 0 && BASS_ChannelIsActive(m_activeStream) == BASS_ACTIVE_PLAYING;
}

double BassEngine::GetPositionSeconds() const {
    if (m_activeStream == 0) return 0.0;
    QWORD pos = BASS_ChannelGetPosition(m_activeStream, BASS_POS_BYTE);
    return (pos == (QWORD)-1) ? 0.0 : BASS_ChannelBytes2Seconds(m_activeStream, pos);
}

double BassEngine::GetLengthSeconds() const {
    if (m_activeStream == 0) return 0.0;
    QWORD len = BASS_ChannelGetLength(m_activeStream, BASS_POS_BYTE);
    return (len == (QWORD)-1) ? 0.0 : BASS_ChannelBytes2Seconds(m_activeStream, len);
}

std::vector<float> BassEngine::GetSpectrumData() const {
    std::vector<float> fft(128, 0.0f);
    if (m_activeStream != 0 && BASS_ChannelIsActive(m_activeStream) == BASS_ACTIVE_PLAYING) {
        BASS_ChannelGetData(m_activeStream, fft.data(), BASS_DATA_FFT256);
    }
    return fft;
}
```

## File: core/audio/bass/BassEngine.h
```c
#pragma once
#include "../IAudioEngine.h"

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif
#include "bass.h"

#ifdef _WIN32
#undef ERROR
#endif

class BassEngine : public IAudioEngine {
public:
    BassEngine();
    ~BassEngine() override;

    bool Init() override;
    bool PlayStream(const std::string& url, int durationSec, bool crossfade, const std::string& trackId) override;
    void Pause() override;
    void Resume() override;
    void SetVolume(float volume) override;
    void SetPositionSeconds(double pos) override;
    float GetVolume() const override;
    bool IsPlaying() const override;
    double GetPositionSeconds() const override;
    double GetLengthSeconds() const override;
    std::vector<float> GetSpectrumData() const override;
    // Заглушка
    void ClearBuffers(bool crossfade = false, int nextDurationSec = 0) override {}

private:
    HSTREAM m_activeStream = 0;
    HSTREAM m_fadingStream = 0;
    HSYNC m_syncCrossfade = 0;

    HPLUGIN m_hlsPlugin = 0;
    HSYNC m_syncEnd = 0;
    HSYNC m_syncNearEnd = 0;
    float m_volume = 1.0f;

    int m_crossfadeDurationMs = 3000;

    static void CALLBACK BassTrackEndCallback(HSYNC handle, DWORD channel, DWORD data, void* user);
    static void CALLBACK BassTrackNearEndCallback(HSYNC handle, DWORD channel, DWORD data, void* user);
};
```

## File: core/audio/miniaudio/miniaudio_impl.cpp
```cpp
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"
```

## File: core/audio/miniaudio/MiniaudioEngine.cpp
```cpp
#include "MiniaudioEngine.h"
#include "utils/logger/Logger.h"
#include "minimp3.h"
#include "utils/path/PathManager.h"

#include <QFile>
#include <QString>
#include <algorithm>
#include <chrono>
#include <QCoreApplication>
#include <QSettings>
#include <complex>
#include <cmath>

const double PI = 3.14159265358979323846;
typedef std::complex<double> Complex;

// Быстрое преобразование Фурье (FFT) Кули-Тьюки
static void SimpleFFT(std::vector<Complex>& a) {
    size_t n = a.size();
    if (n <= 1) return;
    std::vector<Complex> a0(n / 2), a1(n / 2);
    for (size_t i = 0; i < n / 2; i++) {
        a0[i] = a[i * 2];
        a1[i] = a[i * 2 + 1];
    }
    SimpleFFT(a0);
    SimpleFFT(a1);
    double ang = 2 * PI / n;
    Complex w(1), wn(cos(ang), sin(ang));
    for (size_t i = 0; i < n / 2; i++) {
        a[i] = a0[i] + w * a1[i];
        a[i + n / 2] = a0[i] - w * a1[i];
        w *= wn;
    }
}

MiniaudioEngine::MiniaudioEngine() : m_demuxer([this](const uint8_t* payload, size_t size, AudioFormat format) {
    if (format == AudioFormat::MP3) {
        DecodeMp3Payload(payload, size);
    } else {
        DecodeAacPayload(payload, size);
    }
}) {
    m_isDeviceInitialized = false;
    m_isDecoderInitialized = false;
    m_isPlaying = false;
    m_volume = 1.0f;
    // Выделяем память один раз
    m_mainBuffer.resize(16384, 0);
    m_fadeOutBuffer.resize(16384, 0);

    // Запускаем фоновый поток декодирования
    m_isDecoding = true;
    m_decodeThread = std::thread(&MiniaudioEngine::DecodeLoop, this);
}

MiniaudioEngine::~MiniaudioEngine() {
    // Останавливаем фоновый поток
    m_isDecoding = false;
    m_decodeCv.notify_all();

    if (m_decodeThread.joinable()) {
        m_decodeThread.join();
    }
    if (m_isDeviceInitialized) {
        ma_device_uninit(&m_device);
    }
    StopFadeOut();
    m_decoder.reset();
    if (m_aacDecoder) {
        aacDecoder_Close(m_aacDecoder);
    }
}

float MiniaudioEngine::GetVolume() const { return m_volume; }

bool MiniaudioEngine::IsPlaying() const { return m_isPlaying; }

void MiniaudioEngine::SetPositionSeconds(double pos) {
    if (m_currentDurationSec <= 0) return;

    if (pos < 0.0) pos = 0.0;
    if (pos > static_cast<double>(m_currentDurationSec)) pos = static_cast<double>(m_currentDurationSec);

    std::lock_guard<std::mutex> lock(m_audioMutex);

    if (m_decoder) {
        ma_uint64 targetFrame = static_cast<ma_uint64>(pos * static_cast<double>(SAMPLE_RATE));
        if (ma_decoder_seek_to_pcm_frame(m_decoder.get(), targetFrame) == MA_SUCCESS) {
            m_playbackFrameCount = targetFrame;
            m_nearEndTriggered = false;
            m_finishedTriggered = false;
            Logger::Log(LogLevel::INFO, "Miniaudio: Seeked to " + std::to_string(pos) + "s");
        }
    } else {
        m_playbackFrameCount = static_cast<ma_uint64>(pos * static_cast<double>(SAMPLE_RATE));
        m_nearEndTriggered = false;
        m_finishedTriggered = false;

        m_pcmBuffer.Clear();
        {
            std::lock_guard<std::mutex> netLock(m_networkMutex);
            m_aacBuffer.clear();
        }
        m_mp3Buffer.clear();

        m_demuxer.Reset();

        if (m_aacDecoder) {
            aacDecoder_Close(m_aacDecoder);
            m_aacDecoder = aacDecoder_Open(TT_MP4_ADTS, 1);
        }
        mp3dec_init(&m_mp3Decoder);

        if (OnNetworkSeekRequested) {
            OnNetworkSeekRequested(pos);
        }
    }
}

double MiniaudioEngine::GetPositionSeconds() const { return static_cast<double>(m_playbackFrameCount.load()) / static_cast<double>(SAMPLE_RATE); }

double MiniaudioEngine::GetLengthSeconds() const {
    return static_cast<double>(m_currentDurationSec);
}

std::vector<float> MiniaudioEngine::GetSpectrumData() const {
    std::vector<float> result(128, 0.0f);
    if (!m_isPlaying) return result;

    std::vector<Complex> a(256);
    {
        std::lock_guard<std::mutex> lock(m_spectrumMutex);
        for (int i = 0; i < 256; ++i) {
            double multiplier = 0.5 * (1.0 - cos(2 * PI * i / 255.0));
            a[i] = Complex(m_recentSamples[i] * multiplier, 0);
        }
    }

    SimpleFFT(a);

    for (int i = 0; i < 128; ++i) {
        float mag = static_cast<float>(std::abs(a[i]) / 128.0) * 2.5f;
        result[i] = mag;
    }

    return result;
}

bool MiniaudioEngine::Init() {
    QSettings settings("config.ini", QSettings::IniFormat);
    m_crossfadeDurationMs = settings.value("Audio/CrossfadeDurationMs", 3000).toInt();

    m_pcmBuffer.Init(SAMPLE_RATE * 2 * sizeof(int16_t) * 5);
    ma_device_config deviceConfig = ma_device_config_init(ma_device_type_playback);
    deviceConfig.playback.format   = ma_format_s16;
    deviceConfig.playback.channels = 2;
    deviceConfig.sampleRate        = SAMPLE_RATE;
    deviceConfig.dataCallback      = DataCallback;
    deviceConfig.pUserData         = this;

    m_aacDecoder = aacDecoder_Open(TT_MP4_ADTS, 1);
    if (!m_aacDecoder) {
        Logger::Log(LogLevel::ERROR, "Miniaudio: Failed to open FDK-AAC decoder.");
        return false;
    }

    mp3dec_init(&m_mp3Decoder);

    if (ma_device_init(NULL, &deviceConfig, &m_device) != MA_SUCCESS) {
        Logger::Log(LogLevel::ERROR, "Miniaudio: Failed to init playback device.");
        return false;
    }

    m_isDeviceInitialized = true;
    Logger::Log(LogLevel::INFO, "Miniaudio: Device initialized successfully.");
    return true;
}

void MiniaudioEngine::DataCallback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    MiniaudioEngine* engine = static_cast<MiniaudioEngine*>(pDevice->pUserData);
    if (!engine) return;

    {
        std::lock_guard<std::mutex> lock(engine->m_audioMutex);

        if (frameCount * 2 > engine->m_mainBuffer.size()) {
            frameCount = engine->m_mainBuffer.size() / 2;
        }

        engine->m_playbackFrameCount += frameCount;

        std::memset(engine->m_mainBuffer.data(), 0, frameCount * 2 * sizeof(int16_t));
        std::memset(engine->m_fadeOutBuffer.data(), 0, frameCount * 2 * sizeof(int16_t));

        ma_uint32 framesRead = 0;

        if (engine->m_decoder) {
            ma_uint64 read = 0;
            ma_decoder_read_pcm_frames(engine->m_decoder.get(), engine->m_mainBuffer.data(), frameCount, &read);
            framesRead = read;
        } else {
            ma_uint32 bytesToRead = frameCount * 2 * sizeof(int16_t);
            size_t bytesRead = engine->m_pcmBuffer.Read(reinterpret_cast<uint8_t*>(engine->m_mainBuffer.data()), bytesToRead);
            framesRead = bytesRead / (2 * sizeof(int16_t));
        }

        ma_uint32 fadeOutFramesRead = 0;

        if (engine->m_isCrossfading) {
            if (engine->m_fadeOutIsLocal && engine->m_fadeOutDecoder) {
                ma_uint64 read = 0;
                ma_decoder_read_pcm_frames(engine->m_fadeOutDecoder.get(), engine->m_fadeOutBuffer.data(), frameCount, &read);
                fadeOutFramesRead = read;
            } else {
                size_t elementsAvail = engine->m_fadeOutPcm.size() - engine->m_fadeOutPcmReadPos;
                size_t elementsToRead = frameCount * 2;
                if (elementsToRead > elementsAvail) elementsToRead = elementsAvail;

                if (elementsToRead > 0) {
                    std::memcpy(engine->m_fadeOutBuffer.data(), engine->m_fadeOutPcm.data() + engine->m_fadeOutPcmReadPos, elementsToRead * sizeof(int16_t));
                    engine->m_fadeOutPcmReadPos += elementsToRead;
                    fadeOutFramesRead = elementsToRead / 2;
                }
            }
        }

        int16_t* pOut = static_cast<int16_t*>(pOutput);
        for (ma_uint32 i = 0; i < frameCount; ++i) {
            float mainVol = 1.0f;
            float fadeOutVol = 0.0f;

            if (engine->m_isCrossfading) {
                if (engine->m_crossfadeFramesRemaining > 0) {
                    float progress = 1.0f - (static_cast<float>(engine->m_crossfadeFramesRemaining) / engine->m_crossfadeFramesTotal);
                    mainVol = progress;
                    fadeOutVol = 1.0f - progress;
                    engine->m_crossfadeFramesRemaining--;
                } else {
                    engine->StopFadeOut();
                }
            }

            for (int c = 0; c < 2; ++c) {
                int idx = i * 2 + c;
                float s1 = (i < framesRead) ? engine->m_mainBuffer[idx] * mainVol : 0.0f;
                float s2 = (i < fadeOutFramesRead) ? engine->m_fadeOutBuffer[idx] * fadeOutVol : 0.0f;

                float mixed = s1 + s2;
                if (mixed > 32767.0f) mixed = 32767.0f;
                if (mixed < -32768.0f) mixed = -32768.0f;

                pOut[idx] = static_cast<int16_t>(mixed);
            }
        }

        {
            std::lock_guard<std::mutex> specLock(engine->m_spectrumMutex);
            size_t samplesToCopy = std::min(static_cast<size_t>(frameCount), static_cast<size_t>(256));

            if (samplesToCopy < 256) {
                std::memmove(engine->m_recentSamples.data(),
                             engine->m_recentSamples.data() + samplesToCopy,
                             (256 - samplesToCopy) * sizeof(float));
            }

            size_t startIdx = 256 - samplesToCopy;
            size_t pOutStart = frameCount - samplesToCopy;
            for (size_t i = 0; i < samplesToCopy; ++i) {
                float left = pOut[(pOutStart + i) * 2] / 32768.0f;
                float right = pOut[(pOutStart + i) * 2 + 1] / 32768.0f;
                engine->m_recentSamples[startIdx + i] = (left + right) / 2.0f;
            }
        }

        double currentSec = static_cast<double>(engine->m_playbackFrameCount.load()) / static_cast<double>(SAMPLE_RATE);
        double totalSec = static_cast<double>(engine->m_currentDurationSec);
        double crossfadeSec = engine->m_crossfadeDurationMs / 1000.0;

        if (totalSec > 0.0) {
            if (!engine->m_nearEndTriggered && currentSec >= totalSec - 10.0) {
                engine->m_nearEndTriggered = true;
                if (engine->OnTrackNearEnd) {
                    QMetaObject::invokeMethod(QCoreApplication::instance(), [engine]() {
                        engine->OnTrackNearEnd();
                    }, Qt::QueuedConnection);
                }
            }

            double endTriggerSec = totalSec;
            if (engine->m_crossfadeDurationMs > 0) {
                endTriggerSec = totalSec - crossfadeSec;
            }

            if (!engine->m_finishedTriggered && currentSec >= endTriggerSec) {
                engine->m_finishedTriggered = true;
                if (engine->OnTrackFinished) {
                    QMetaObject::invokeMethod(QCoreApplication::instance(), [engine]() {
                        engine->OnTrackFinished();
                    }, Qt::QueuedConnection);
                }
            }
        }
    }
}

bool MiniaudioEngine::PlayStream(const std::string& url, int durationSec, bool crossfade, const std::string& trackId) {
    m_currentTrackId = trackId;

    QString localPath = PathManager::GetDownloadFilePath(trackId, "mp3");

    if (!QFile::exists(localPath)) {
        localPath = PathManager::GetDownloadFilePath(trackId, "aac");
    }

    if (trackId.empty() || !QFile::exists(localPath)) {
        return false;
    }

    std::lock_guard<std::mutex> lock(m_audioMutex);

    m_currentDurationSec = durationSec;
    m_nearEndTriggered = false;
    m_finishedTriggered = false;
    m_playbackFrameCount = 0;

    if (crossfade && m_crossfadeDurationMs > 0) {
        InitiateCrossfade();
    } else {
        StopFadeOut();
        m_decoder.reset();
        m_pcmBuffer.Clear();
    }

    ma_decoder_config decoderConfig = ma_decoder_config_init(ma_format_s16, 2, SAMPLE_RATE);
    m_decoder.reset(new ma_decoder);

#ifdef _WIN32
    // ИСПРАВЛЕНО: Используем правильную функцию для Win32 и добавляем .get()
    if (ma_decoder_init_file_w(reinterpret_cast<const wchar_t*>(localPath.utf16()), &decoderConfig, m_decoder.get()) != MA_SUCCESS) {
#else
    if (ma_decoder_init_file(localPath.toStdString().c_str(), &decoderConfig, m_decoder.get()) != MA_SUCCESS) {
#endif
        m_decoder.reset();
        Logger::Log(LogLevel::ERROR, "Miniaudio: Failed to init decoder for file: " + localPath.toStdString());
        return false;
    }

    m_isPlaying = true;
    ma_device_start(&m_device);
    Logger::Log(LogLevel::INFO, "Miniaudio: Playing local file -> " + localPath.toStdString());
    return true;
}

void MiniaudioEngine::Pause() {
    if (m_isDeviceInitialized && m_isPlaying) {
        m_isPlaying = false;
        ma_device_stop(&m_device);
    }
}

void MiniaudioEngine::Resume() {
    if (m_isDeviceInitialized && !m_isPlaying) {
        m_isPlaying = true;
        ma_device_start(&m_device);
    }
}

void MiniaudioEngine::SetVolume(float volume) {
    m_volume = volume;

    if (m_volume < 0.0f) m_volume = 0.0f;
    if (m_volume > 1.0f) m_volume = 1.0f;

    if (m_isDeviceInitialized) {
        float actualVolume = std::pow(m_volume, 3.0f);
        ma_device_set_master_volume(&m_device, actualVolume);
    }
}

void MiniaudioEngine::PushNetworkData(const uint8_t* data, size_t size) {
    {
        std::lock_guard<std::mutex> lock(m_networkMutex);
        m_aacBuffer.insert(m_aacBuffer.end(), data, data + size);
    }
    m_decodeCv.notify_one();
}

void MiniaudioEngine::DecodeLoop() {
    while (m_isDecoding) {
        {
            std::unique_lock<std::mutex> lock(m_networkMutex);
            m_decodeCv.wait(lock, [this]() {
                return !m_isDecoding || (m_aacBuffer.size() >= 188);
            });
        }

        if (!m_isDecoding) break;

        if (m_pcmBuffer.GetAvailableWrite() < 176400) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        DecodeAACFrames();
    }
}

void MiniaudioEngine::DecodeAACFrames() {
    std::lock_guard<std::mutex> lock(m_networkMutex);

    size_t bytesConsumed = 0;
    while (m_aacBuffer.size() - bytesConsumed >= 188) {
        if (m_pcmBuffer.GetAvailableWrite() < 176400) {
            break;
        }

        m_demuxer.ProcessBytes(m_aacBuffer.data() + bytesConsumed, 188);
        bytesConsumed += 188;
    }

    if (bytesConsumed > 0) {
        m_aacBuffer.erase(m_aacBuffer.begin(), m_aacBuffer.begin() + bytesConsumed);
    }
}

void MiniaudioEngine::DecodeAacPayload(const uint8_t* payload, size_t payloadSize) {
    UCHAR* pBuffer = const_cast<UCHAR*>(payload);
    UINT bufferSize = static_cast<UINT>(payloadSize);
    UINT bytesValid = bufferSize;

    aacDecoder_Fill(m_aacDecoder, &pBuffer, &bufferSize, &bytesValid);

    while (true) {
        std::vector<int16_t> pcmBuf(4096);
        AAC_DECODER_ERROR err = aacDecoder_DecodeFrame(m_aacDecoder, pcmBuf.data(), pcmBuf.size(), 0);

        if (err == AAC_DEC_NOT_ENOUGH_BITS) break;
        if (err != AAC_DEC_OK) {
            Logger::Log(LogLevel::WARNING, "AAC decode error: " + std::to_string(err));
            
            if (OnPlaybackError) {
                QMetaObject::invokeMethod(QCoreApplication::instance(), [this, err]() {
                    OnPlaybackError("AAC Decode Error: " + std::to_string(err));
                }, Qt::QueuedConnection);
            }
            break;
        }

        CStreamInfo* info = aacDecoder_GetStreamInfo(m_aacDecoder);
        if (info && info->numChannels > 0) {
            if (info->numChannels == 1) {
                std::vector<int16_t> stereoBuf(info->frameSize * 2);
                for (int i = 0; i < info->frameSize; ++i) {
                    stereoBuf[i * 2]     = pcmBuf[i];
                    stereoBuf[i * 2 + 1] = pcmBuf[i];
                }
                size_t bytesToOutput = info->frameSize * 2 * sizeof(int16_t);
                m_pcmBuffer.Write(reinterpret_cast<uint8_t*>(stereoBuf.data()), bytesToOutput);
            } else {
                size_t bytesToOutput = info->frameSize * info->numChannels * sizeof(int16_t);
                m_pcmBuffer.Write(reinterpret_cast<uint8_t*>(pcmBuf.data()), bytesToOutput);
            }
        }
    }
}

void MiniaudioEngine::DecodeMp3Payload(const uint8_t* payload, size_t payloadSize) {
    m_mp3Buffer.insert(m_mp3Buffer.end(), payload, payload + payloadSize);

    static constexpr size_t kMinBufferForDecode = 8192;
    static constexpr size_t kMaxFrameSize = 2048;

    if (m_mp3Buffer.size() < kMinBufferForDecode) {
        return;
    }

    mp3dec_frame_info_t info;
    while (m_mp3Buffer.size() > kMaxFrameSize) {
        std::vector<int16_t> pcmBuf(MINIMP3_MAX_SAMPLES_PER_FRAME);
        int samples = mp3dec_decode_frame(&m_mp3Decoder, m_mp3Buffer.data(),
                                           static_cast<int>(m_mp3Buffer.size()),
                                           pcmBuf.data(), &info);

        if (info.frame_bytes == 0) {
            break;
        }

        if (samples > 0 && info.channels > 0) {
            if (info.channels == 1) {
                std::vector<int16_t> stereoBuf(samples * 2);
                for (int i = 0; i < samples; ++i) {
                    stereoBuf[i * 2]     = pcmBuf[i];
                    stereoBuf[i * 2 + 1] = pcmBuf[i];
                }
                size_t bytesToOutput = samples * 2 * sizeof(int16_t);
                m_pcmBuffer.Write(reinterpret_cast<uint8_t*>(stereoBuf.data()), bytesToOutput);
            } else {
                size_t bytesToOutput = static_cast<size_t>(samples) * info.channels * sizeof(int16_t);
                m_pcmBuffer.Write(reinterpret_cast<uint8_t*>(pcmBuf.data()), bytesToOutput);
            }
        }

        m_mp3Buffer.erase(m_mp3Buffer.begin(), m_mp3Buffer.begin() + info.frame_bytes);
    }
}

void MiniaudioEngine::ClearBuffers(bool crossfade, int nextDurationSec) {
    std::lock_guard<std::mutex> lock(m_audioMutex);

    m_currentDurationSec = nextDurationSec;
    m_nearEndTriggered = false;
    m_finishedTriggered = false;
    m_playbackFrameCount = 0;

    if (crossfade && m_crossfadeDurationMs > 0) {
        InitiateCrossfade();
    } else {
        StopFadeOut();
        m_decoder.reset();
        m_pcmBuffer.Clear();
    }

    m_demuxer.Reset();
    m_mp3Buffer.clear();
    mp3dec_init(&m_mp3Decoder);

    std::lock_guard<std::mutex> netLock(m_networkMutex);
    m_aacBuffer.clear();
}

void MiniaudioEngine::InitiateCrossfade() {
    StopFadeOut();

    if (m_decoder) {
        m_fadeOutDecoder = std::move(m_decoder);
        m_fadeOutIsLocal = true;
    } else {
        m_fadeOutIsLocal = false;
        m_fadeOutPcm.clear();
        m_fadeOutPcmReadPos = 0;
        size_t avail = m_pcmBuffer.GetAvailableRead();
        if (avail > 0) {
            m_fadeOutPcm.resize(avail / sizeof(int16_t));
            m_pcmBuffer.Read(reinterpret_cast<uint8_t*>(m_fadeOutPcm.data()), avail);
        }
    }
    m_pcmBuffer.Clear();

    m_crossfadeFramesTotal = (m_crossfadeDurationMs * SAMPLE_RATE) / 1000;
    m_crossfadeFramesRemaining = m_crossfadeFramesTotal;
    m_isCrossfading = true;
}

void MiniaudioEngine::StopFadeOut() {
    m_fadeOutDecoder.reset();
    m_fadeOutPcm.clear();
    m_isCrossfading = false;
}
```

## File: core/audio/miniaudio/MiniaudioEngine.h
```c
#pragma once
#include "core/audio/IAudioEngine.h"
#include "miniaudio.h"
#include "minimp3.h"
#include "utils/buffer/RingBuffer.h"
#include "aacdecoder_lib.h"
#include "utils/parser/MpegTsDemuxer.h"

#include <string>
#include <atomic>
#include <vector>
#include <mutex>
#include <memory>
#include <thread>
#include <condition_variable>


class MiniaudioEngine : public IAudioEngine {
public:
    MiniaudioEngine();
    ~MiniaudioEngine() override;

    bool Init() override;
    bool PlayStream(const std::string& url, int durationSec, bool crossfade, const std::string& trackId) override;
    void Pause() override;
    void Resume() override;
    void SetVolume(float volume) override;
    void SetPositionSeconds(double pos) override;

    float GetVolume() const override;
    bool IsPlaying() const override;
    double GetPositionSeconds() const override;
    double GetLengthSeconds() const override;
    std::vector<float> GetSpectrumData() const override;

    // Метод, куда NetworkStreamer будет пушить скачанные байты AAC
    void PushNetworkData(const uint8_t* data, size_t size);
    void ClearBuffers(bool crossfade = false, int nextDurationSec = 0);

    static constexpr int SAMPLE_RATE = 44100;

private:
    static void DataCallback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount);
    void DecodeAACFrames();   // функция для декодирования ADTS пакетов
    void InitiateCrossfade(); // Вспомогательный метод кроссфейда
    void StopFadeOut();       // Вспомогательный метод очистки затухания
    void DecodeLoop();

    // Кастомный удалитель для декодера
    struct DecoderDeleter {
        void operator()(ma_decoder* dec) const {
            if (dec) {
                ma_decoder_uninit(dec);
                delete dec;
            }
        }
    };

    ma_device m_device;
    std::thread m_decodeThread;
    std::atomic<bool> m_isDecoding{false};
    std::condition_variable m_decodeCv;
    std::vector<int16_t> m_mainBuffer;
    std::vector<int16_t> m_fadeOutBuffer;
    bool m_isDeviceInitialized = false;
    float m_volume = 1.0f;
    std::atomic<bool> m_isPlaying = false;
    std::unique_ptr<ma_decoder, DecoderDeleter> m_decoder;          // Декодер для чтения файлов
    bool m_isDecoderInitialized = false;                            // Флаг состояния декодера
    std::mutex m_audioMutex;                                        // Защита от конфликта потоков
    HANDLE_AACDECODER m_aacDecoder = nullptr;                       // Указатель на FDK-AAC декодер
    RingBuffer m_pcmBuffer;                                         // Потокобезопасный буфер для PCM
    std::vector<uint8_t> m_aacBuffer;                               // Временный буфер для сырых скачанных данных
    std::mutex m_networkMutex;                                      // Защита буфера скачивания
    std::atomic<ma_uint64> m_playbackFrameCount{0};

    // --- ПЕРЕМЕННЫЕ КРОССФЕЙДА И ТАЙМИНГОВ ---
    int m_crossfadeDurationMs = 3000;
    int m_currentDurationSec = 0;
    bool m_nearEndTriggered = false;
    bool m_finishedTriggered = false;

    bool m_isCrossfading = false;
    ma_uint32 m_crossfadeFramesTotal = 0;
    ma_uint32 m_crossfadeFramesRemaining = 0;

    bool m_fadeOutIsLocal = false;
    std::unique_ptr<ma_decoder, DecoderDeleter> m_fadeOutDecoder;
    std::vector<int16_t> m_fadeOutPcm;
    size_t m_fadeOutPcmReadPos = 0;
    // -----------------------------------------

    void DecodeAacPayload(const uint8_t* payload, size_t payloadSize);
    void DecodeMp3Payload(const uint8_t* payload, size_t payloadSize);

    mp3dec_t m_mp3Decoder;
    std::vector<uint8_t> m_mp3Buffer;
    MpegTsDemuxer m_demuxer;

    // --- ПЕРЕМЕННЫЕ ВИЗУАЛИЗАТОРА ---
    mutable std::mutex m_spectrumMutex;
    std::vector<float> m_recentSamples = std::vector<float>(256, 0.0f);

    std::string m_currentTrackId;
};
```

## File: core/audio/playback/PlaybackController.cpp
```cpp
#include "PlaybackController.h"
#include "core/audio/IAudioEngine.h"
#include "core/playlist/PlaylistManager.h"
#include "core/api/IAudioProvider.h"
#include "services/network/NetworkStreamer.h"
#include "utils/logger/Logger.h"
#include "utils/path/PathManager.h"

#include <QFile>
#include <QTimer>
#include <iostream>
#include <QCoreApplication>

PlaybackController::PlaybackController(IAudioEngine& audio, PlaylistManager& playlist, NetworkStreamer& streamer, QObject* parent)
    : QObject(parent), m_audio(audio), m_playlist(playlist), m_streamer(streamer) {}

void PlaybackController::SetCurrentProvider(IAudioProvider* provider) {
    m_currentProvider = provider;
}

void PlaybackController::SetCrossfadeEnabled(bool enabled) {
    m_crossfadeEnabled = enabled;
}

void PlaybackController::SetSavedPosition(double pos) {
    m_savedPosition = pos;
}

void PlaybackController::ClearState() {
    m_streamer.StopDownload();
    m_audio.ClearBuffers(false, 0);
    m_audio.Pause();
    m_preloadedTrack = Track();
    m_cachedNextUrl = "";
}

void PlaybackController::HandleTrackFinished() {
    Logger::Log(LogLevel::INFO, "PlaybackController: Auto-switching to next track...");
    m_playlist.Next();
}

void PlaybackController::HandleTrackNearEnd() {
    Track nextTrack = m_playlist.PeekNextTrack();
    if (nextTrack.id.empty()) return;

    if (!m_currentProvider) return;
    m_currentProvider->FetchTrackUrl(nextTrack.id, [this, nextTrack](const std::string& freshUrl, bool isNetworkError) {
        if (!isNetworkError && !freshUrl.empty()) {
            m_cachedNextUrl = freshUrl;
            m_preloadedTrack = nextTrack;
            Logger::Log(LogLevel::INFO, "PlaybackController: Next track URL pre-fetched successfully.");
        }
    });
}

void PlaybackController::AttemptPlay(const Track& track, int attempt) {
    int currentGen = (attempt == 1) ? ++m_playbackGeneration : m_playbackGeneration.load();

    QString localPath = PathManager::GetDownloadFilePath(track.GetSafeFilename(), "mp3");
    if (!QFile::exists(localPath)) {
        localPath = PathManager::GetDownloadFilePath(track.GetSafeFilename(), "aac");
    }
    bool isDownloaded = QFile::exists(localPath);

    if (attempt == 1 && !m_cachedNextUrl.empty() && m_preloadedTrack.id == track.id && !isDownloaded) {
        if (m_audio.PlayStream(m_cachedNextUrl, track.duration, m_crossfadeEnabled, track.GetSafeFilename())) {
            m_cachedNextUrl = "";
            m_skipCount = 0;
            if (m_savedPosition > 0.0) {
                m_audio.SetPositionSeconds(m_savedPosition);
                m_savedPosition = 0.0;
            }
            std::cout << "\r\033[2K\033[1A\r\033[2K\n> ";
            std::cout.flush();
            return;
        }
    } else if (attempt == 1) {
        std::cout << "\r\033[2K\033[1A\r\033[2K";
        std::cout << "[Загрузка] " << track.artist << " - " << track.title << "...\n\n> ";
        std::cout.flush();
    }

    if (isDownloaded) {
        m_skipCount = 0;
        if (m_audio.PlayStream("", track.duration, m_crossfadeEnabled, track.GetSafeFilename())) {
            if (m_savedPosition > 0.0) {
                m_audio.SetPositionSeconds(m_savedPosition);
                m_savedPosition = 0.0;
            }
            std::cout << "\r\033[2K\033[1A\r\033[2K\033[1A\r\033[2K\n> ";
            std::cout.flush();
        } else {
            m_playlist.Next();
        }
        return;
    }

    auto executePlay = [this, track, attempt, currentGen](const std::string& freshUrl, bool isNetworkError) {
        if (currentGen != m_playbackGeneration.load()) return;

        if (!isNetworkError && freshUrl.empty()) {
            m_skipCount++;
            if (m_skipCount >= 5) {
                Logger::Log(LogLevel::ERROR, "Too many unplayable tracks. Stopping loop.");
                m_skipCount = 0;
                std::cout << "\r\033[2K\033[1A\r\033[2K[Внимание] Ошибка сети/токена. Воспроизведение остановлено.\n\n> ";
                std::cout.flush();
                return;
            }

            Logger::Log(LogLevel::WARNING, "Track is restricted or token invalid. Skipping...");
            m_playlist.Next();
            return;
        }

        m_skipCount = 0;

        if (!freshUrl.empty()) {
            m_streamer.StopDownload();
            m_audio.ClearBuffers(m_crossfadeEnabled, track.duration);
            m_streamer.StartDownload(freshUrl);
            m_audio.Resume();

            if (m_savedPosition > 0.0) {
                m_audio.SetPositionSeconds(m_savedPosition);
                m_savedPosition = 0.0;
            }
            std::cout << "\r\033[2K\033[1A\r\033[2K\033[1A\r\033[2K\n> ";
            std::cout.flush();
            return;
        }

        if (attempt < 3) {
            Logger::Log(LogLevel::INFO, "Retrying stream in 2 seconds...");
            QTimer::singleShot(2000, [this, track, attempt]() { AttemptPlay(track, attempt + 1); });
        } else {
            m_playlist.Next();
        }
    };

    if (m_currentProvider) {
        m_currentProvider->FetchTrackUrl(track.id, executePlay);
    } else if (!isDownloaded) {
        QMetaObject::invokeMethod(QCoreApplication::instance(), [this]() {
            m_playlist.Next();
        }, Qt::QueuedConnection);
    }
}
```

## File: core/audio/playback/PlaybackController.h
```c
#pragma once
#include <QObject>
#include <atomic>
#include <string>
#include "models/Track.h"

class IAudioEngine;
class PlaylistManager;
class IAudioProvider;
class NetworkStreamer;

class PlaybackController : public QObject {
    Q_OBJECT
public:
    PlaybackController(IAudioEngine& audio, PlaylistManager& playlist, NetworkStreamer& streamer, QObject* parent = nullptr);

    void SetCurrentProvider(IAudioProvider* provider);
    void SetCrossfadeEnabled(bool enabled);
    void SetSavedPosition(double pos);

    void AttemptPlay(const Track& track, int attempt = 1);
    void HandleTrackFinished();
    void HandleTrackNearEnd();
    void ClearState();

private:
    IAudioEngine& m_audio;
    PlaylistManager& m_playlist;
    NetworkStreamer& m_streamer;
    IAudioProvider* m_currentProvider = nullptr;

    bool m_crossfadeEnabled = false;
    double m_savedPosition = 0.0;

    int m_skipCount = 0;
    std::atomic<int> m_playbackGeneration{0};

    Track m_preloadedTrack;
    std::string m_cachedNextUrl = "";
};
```

## File: core/audio/IAudioEngine.h
```c
#pragma once
#include <string>
#include <vector>
#include <functional>

class IAudioEngine {
public:
    virtual ~IAudioEngine() = default;

    // Основные методы плеера
    virtual bool Init() = 0;
    virtual bool PlayStream(const std::string& url, int durationSec, bool crossfade, const std::string& trackId) = 0;
    virtual void Pause() = 0;
    virtual void Resume() = 0;
    virtual void SetVolume(float volume) = 0;
    virtual void SetPositionSeconds(double pos) = 0;

    // Геттеры
    virtual float GetVolume() const = 0;
    virtual bool IsPlaying() const = 0;
    virtual double GetPositionSeconds() const = 0;
    virtual double GetLengthSeconds() const = 0;
    virtual std::vector<float> GetSpectrumData() const = 0;
    virtual void ClearBuffers(bool crossfade = false, int nextDurationSec = 0) = 0;

    std::function<void()> OnTrackNearEnd;
    std::function<void()> OnTrackFinished;
    std::function<void(double)> OnNetworkSeekRequested;

    std::function<void(const std::string&)> OnPlaybackError;
};
```

## File: core/auth/oauth/OAuthManager.cpp
```cpp
#include "OAuthManager.h"
#include "utils/logger/Logger.h"
#include "utils/path/PathManager.h"
#include <qtkeychain/keychain.h>

#include <QCoreApplication>
#include <QRegularExpression>

OAuthManager::OAuthManager(QObject* parent) : QObject(parent) {
    if (QCoreApplication::organizationName().isEmpty()) {
        QCoreApplication::setOrganizationName("VKAudioPlayer");
    }
    if (QCoreApplication::applicationName().isEmpty()) {
        QCoreApplication::setApplicationName("VKAudioPlayer");
    }

    Logger::Log(LogLevel::INFO, "auth (OAuth Manager) created. Secure storage initialized.");
}

OAuthManager::~OAuthManager() {}

void OAuthManager::SaveToken(const std::string& token, const QString& service) const {
    auto* job = new QKeychain::WritePasswordJob(service);
    job->setAutoDelete(true);
    job->setKey("oauth_token");
    job->setTextData(QString::fromStdString(token));

    connect(job, &QKeychain::Job::finished, [service](QKeychain::Job* baseJob) {
        if (baseJob->error()) {
            Logger::Log(LogLevel::ERROR, "auth: Failed to securely save token for " + service.toStdString() + ": " + baseJob->errorString().toStdString());
        } else {
            Logger::Log(LogLevel::INFO, "auth: Token securely saved for " + service.toStdString());
        }
    });

    job->start();
}

void OAuthManager::GetSavedToken(const QString& service, std::function<void(const std::string&)> callback) const {
    auto* job = new QKeychain::ReadPasswordJob(service);
    job->setAutoDelete(true);
    job->setKey("oauth_token");

    connect(job, &QKeychain::Job::finished, [service, callback](QKeychain::Job* baseJob) {
        if (baseJob->error()) {
            if (baseJob->error() != QKeychain::Error::EntryNotFound) {
                Logger::Log(LogLevel::ERROR, "auth: Failed to read token for " + service.toStdString() + ": " + baseJob->errorString().toStdString());
            }
            callback("");
        } else {
            auto* readJob = qobject_cast<QKeychain::ReadPasswordJob*>(baseJob);
            callback(readJob->textData().toStdString());
        }
    });

    job->start();
}

void OAuthManager::ClearSavedToken(const QString& service) const {
    auto* job = new QKeychain::DeletePasswordJob(service);
    job->setAutoDelete(true);
    job->setKey("oauth_token");

    connect(job, &QKeychain::Job::finished, [service](QKeychain::Job* baseJob) {
        if (baseJob->error() && baseJob->error() != QKeychain::Error::EntryNotFound) {
            Logger::Log(LogLevel::ERROR, "auth: Failed to delete token for " + service.toStdString() + ": " + baseJob->errorString().toStdString());
        } else {
            Logger::Log(LogLevel::INFO, "auth: Token securely removed for " + service.toStdString());
        }
    });

    job->start();
}

void OAuthManager::onUrlIntercepted(const QString& urlStr) {
    bool isVkCallback = urlStr.startsWith("https://oauth.vk.com/blank.html") ||
                        urlStr.startsWith("https://oauth.vk.ru/blank.html");

    bool isSpotifyCallback = urlStr.startsWith("http://127.0.0.1:8080/callback");

    bool isYandexCallback = urlStr.startsWith("https://music.yandex.ru/") ||
                            urlStr.startsWith("https://oauth.yandex.ru/");

    if (!isVkCallback && !isSpotifyCallback && !isYandexCallback) {
        return;
    }

    if (urlStr.contains("access_token=")) {
        QRegularExpression re("access_token=([^&]+)");
        QRegularExpressionMatch match = re.match(urlStr);

        if (match.hasMatch()) {
            std::string token = match.captured(1).toStdString();
            Logger::Log(LogLevel::INFO, "auth: Token intercepted from URL! Starts with: " + token.substr(0, 6) + "...");
            emit TokenReceived(token);
        }
    }
    else if (urlStr.contains("code=")) {
        QRegularExpression re("code=([^&]+)");
        QRegularExpressionMatch match = re.match(urlStr);

        if (match.hasMatch()) {
            std::string code = match.captured(1).toStdString();
            Logger::Log(LogLevel::INFO, "auth: Spotify Authorization Code intercepted!");
            emit AuthCodeReceived(code);
        }
    }
    else if (urlStr.contains("error=")) {
        Logger::Log(LogLevel::ERROR, "auth: Auth failed or denied.");
        emit AuthFailed("URL auth error.");
    }
}

void OAuthManager::onScTokenIntercepted(const QString& tokenStr) {
    QString cleanToken = tokenStr;
    if (cleanToken.startsWith('"') && cleanToken.endsWith('"')) {
        cleanToken = cleanToken.mid(1, cleanToken.length() - 2);
    }

    Logger::Log(LogLevel::INFO, "auth: SC Token intercepted via JS-Sniper!");
    emit TokenReceived(cleanToken.toStdString());
}
```

## File: core/auth/oauth/OAuthManager.h
```c
#pragma once
#include <QObject>
#include <QString>
#include <string>
#include <functional>


class QSettings;
class OAuthManager : public QObject {
    Q_OBJECT
public:
    explicit OAuthManager(QObject* parent = nullptr);
    ~OAuthManager();

    void GetSavedToken(const QString& service, std::function<void(const std::string&)> callback) const;
    void SaveToken(const std::string& token, const QString& service = "VK") const;
    void ClearSavedToken(const QString& service = "VK") const;

    Q_INVOKABLE void onUrlIntercepted(const QString& urlStr);
    Q_INVOKABLE void onScTokenIntercepted(const QString& tokenStr);

    signals:
        void TokenReceived(const std::string& token);
        void AuthFailed(const std::string& error);
        void AuthCodeReceived(const std::string& code);
};
```

## File: core/auth/router/SourceRouter.cpp
```cpp
#include "SourceRouter.h"
#include "core/api/vk/VkClient.h"
#include "core/api/spotify/SpotifyClient.h"
#include "core/auth/oauth/OAuthManager.h"
#include "utils/logger/Logger.h"

#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QWindow>
#include <iostream>

#include "core/api/soundcloud/SoundCloudClient.h"

SourceRouter::SourceRouter(const QMap<QString, QString>& envVars, QObject* parent)
    : QObject(parent), m_envVars(envVars) {
    m_vkClient = std::make_unique<VkClient>();
    m_spotifyClient = std::make_unique<SpotifyClient>();
    m_soundCloudClient = std::make_unique<SoundCloudClient>();
    m_yandexClient = std::make_unique<YandexClient>();

    m_authManager = std::make_unique<OAuthManager>();

    connect(m_authManager.get(), &OAuthManager::TokenReceived, this, [&](const std::string& token) {

            if (m_currentAuthService == "VK") OnVkTokenReceived(token);

            else if (m_currentAuthService == "Spotify") OnSpotifyTokenReceived(token);

            else if (m_currentAuthService == "SoundCloud") {
                if (m_authEngine) { m_authEngine->deleteLater(); m_authEngine = nullptr; }

                m_authManager->SaveToken(token, "SoundCloud");
                emit AuthUiStateChanged(false);
                std::cout << "\n[УСПЕХ] Авторизация SoundCloud пройдена! Токен перехвачен.\n> ";
                std::cout.flush();

                m_soundCloudClient->SetAccessToken(token);
                m_soundCloudClient->InitializeWithToken();
                emit ProviderReady(true);
            }

            else if (m_currentAuthService == "Yandex") {
                if (m_authEngine) { m_authEngine->deleteLater(); m_authEngine = nullptr; }
                m_authManager->SaveToken(token, "Yandex");
                emit AuthUiStateChanged(false);
                std::cout << "\n[УСПЕХ] Авторизация Yandex пройдена!\n> ";
                std::cout.flush();

                m_yandexClient->SetAccessToken(token);
                emit ProviderReady(true);
                m_yandexClient->FetchAllUserAudio(0, 200);
            }
        });

    connect(m_authManager.get(), &OAuthManager::AuthCodeReceived, this, [&](const std::string& code) {
            static std::string lastCode = "";
            if (code == lastCode) return;
            lastCode = code;

            if (m_currentAuthService == "Spotify") {
                if (m_authEngine) {
                    m_authEngine->deleteLater();
                    m_authEngine = nullptr;
                    emit AuthUiStateChanged(false);
                    std::cout << "\n[Инфо] Код перехвачен. Закрываем окно авторизации...\n> ";
                    std::cout.flush();
                }

                Logger::Log(LogLevel::INFO, "SourceRouter: Exchanging Spotify code for token...");
                m_spotifyClient->ExchangeCodeForToken(code);
            }
        });

    connect(m_spotifyClient.get(), &SpotifyClient::TokenReceived, this, &SourceRouter::OnSpotifyTokenReceived);
    connect(m_spotifyClient.get(), &SpotifyClient::AuthError, this, &SourceRouter::OnSpotifyAuthError);
    connect(m_vkClient.get(), &VkClient::TokenExpired, this, &SourceRouter::OnVkTokenExpired);
}

SourceRouter::~SourceRouter() {
    if (m_authEngine) {
        m_authEngine->deleteLater();
    }
}

void SourceRouter::StartAuthFlow(const QString& service, const QString& authUrl) {
    m_currentAuthService = service;
    Logger::Log(LogLevel::INFO, "SourceRouter: Starting auth flow via QML for " + service.toStdString() + "...");
    emit AuthUiStateChanged(true);

    std::cout << "\n=== Авторизация " << service.toStdString() << " ===\n";
    std::cout << "Откроется окно браузера. Войдите в аккаунт, токен перехватится автоматически.\n> ";
    std::cout.flush();

    if (!m_authEngine) {
        m_authEngine = new QQmlApplicationEngine();
        m_authEngine->rootContext()->setContextProperty("cppAuthManager", m_authManager.get());
        m_authEngine->rootContext()->setContextProperty("cppAuthUrl", authUrl);
        m_authEngine->load(QUrl(QStringLiteral("qrc:/core/auth/auth.qml")));

        if (m_authEngine->rootObjects().isEmpty()) {
            Logger::Log(LogLevel::ERROR, "SourceRouter: Failed to load auth.qml!");
        } else {
            QWindow* rootWindow = qobject_cast<QWindow*>(m_authEngine->rootObjects().first());
            if (rootWindow) {
                connect(rootWindow, &QWindow::visibleChanged, this, [this](bool visible) {
                    if (!visible && m_authEngine) {
                        m_authEngine->deleteLater();
                        m_authEngine = nullptr;
                        emit AuthUiStateChanged(false);
                        std::cout << "\n[Инфо] Окно авторизации закрыто.\n> ";
                        std::cout.flush();
                    }
                });
            }
        }
    }
}

void SourceRouter::OnVkTokenReceived(const std::string& token) {
    if (m_authEngine) { m_authEngine->deleteLater(); m_authEngine = nullptr; }
    m_authManager->SaveToken(token, "VK");
    emit AuthUiStateChanged(false);
    std::cout << "\n[УСПЕХ] Авторизация VK пройдена!\n> ";
    std::cout.flush();

    m_vkClient->SetAccessToken(token);
    emit ProviderReady(true);
    m_vkClient->FetchAllUserAudio(0, 200);
}

void SourceRouter::OnSpotifyTokenReceived(const std::string& token) {
    m_authManager->SaveToken(token, "Spotify");
    emit AuthUiStateChanged(false);
    std::cout << "\n[УСПЕХ] Авторизация Spotify пройдена!\n> ";
    std::cout.flush();

    m_spotifyClient->SetAccessToken(token);
    emit ProviderReady(true);
    m_spotifyClient->FetchAllUserAudio(0, 50);
}

void SourceRouter::OnSpotifyAuthError(const std::string& err) {
    std::cout << "\n[ОШИБКА] Не удалось получить токен Spotify: " << err << "\n> ";
    std::cout.flush();
    emit AuthUiStateChanged(false);
}

void SourceRouter::OnVkTokenExpired() {
    Logger::Log(LogLevel::WARNING, "SourceRouter: Token VK expired.");
    std::cout << "\n[ВНИМАНИЕ] Токен ВК устарел.\n";
    m_authManager->ClearSavedToken("VK");
    m_vkClient->SetAccessToken("");
    StartAuthFlow("VK", "https://oauth.vk.com/authorize?client_id=6287487&display=page&redirect_uri=https://oauth.vk.com/blank.html&scope=408861919&response_type=token&v=5.131");
}

void SourceRouter::StartVkService() {
    m_authManager->GetSavedToken("VK", [this](const std::string& savedToken) {
        if (savedToken.empty()) {
            std::cout << "\n[VK] Токен не найден. Открываем окно авторизации...\n";
            std::cout.flush();
            StartAuthFlow("VK", "https://oauth.vk.com/authorize?client_id=6287487&display=page&redirect_uri=https://oauth.vk.com/blank.html&scope=408861919&response_type=token&v=5.131");
        } else {
            std::cout << "\n[VK] Проверка сохраненного токена...\n";
            std::cout.flush();
            m_vkClient->SetAccessToken(savedToken);

            m_vkClient->ValidateToken([this](bool isValid) {
                if (isValid) {
                    emit AuthUiStateChanged(false);
                    emit ProviderReady(true);
                    m_vkClient->FetchAllUserAudio(0, 200);
                } else {
                    m_authManager->ClearSavedToken("VK");
                    m_vkClient->SetAccessToken("");
                    StartAuthFlow("VK", "https://oauth.vk.com/authorize?client_id=6287487&display=page&redirect_uri=https://oauth.vk.com/blank.html&scope=408861919&response_type=token&v=5.131");
                }
            });
        }
    });
}

void SourceRouter::StartSoundCloudService() {
    m_authManager->GetSavedToken("SoundCloud", [this](const std::string& savedToken) {
        if (savedToken.empty()) {
            std::cout << "\n[SoundCloud] Токен не найден. Открываем окно авторизации...\n";
            std::cout.flush();
            StartAuthFlow("SoundCloud", "https://soundcloud.com/signin");
        } else {
            std::cout << "\n[SoundCloud] Инициализация по сохраненному токену...\n";
            std::cout.flush();
            m_soundCloudClient->SetAccessToken(savedToken);
            m_soundCloudClient->InitializeWithToken();
            emit ProviderReady(true);
        }
    });
}

void SourceRouter::StartSpotifyService() {
    QString spDc = m_envVars.value("SPOTIFY_SP_DC", "");
    QString clientId = m_envVars.value("SPOTIFY_CLIENT_ID", "");

    m_authManager->GetSavedToken("Spotify", [this, spDc, clientId](const std::string& savedToken) {

        // --- РЕЖИМ 1: Обход через sp_dc ---
        if (!spDc.isEmpty()) {
            if (savedToken.empty()) {
                std::cout << "\n[Spotify] Получение Web Access Token через sp_dc...\n"; std::cout.flush();
                m_spotifyClient->AuthWithSpDc(spDc);
            } else {
                m_spotifyClient->SetAccessToken(savedToken);
                std::cout << "Проверка сохраненного токена Spotify (sp_dc)...\n"; std::cout.flush();

                m_spotifyClient->ValidateToken([this, spDc](bool isValid) {
                    if (isValid) {
                        emit AuthUiStateChanged(false);
                        std::cout << "\n[УСПЕХ] Синхронизация треков Spotify...\n> "; std::cout.flush();
                        emit ProviderReady(true);
                        m_spotifyClient->FetchAllUserAudio(0, 50);
                    } else {
                        std::cout << "\n[ВНИМАНИЕ] Токен Spotify устарел. Тихое обновление...\n"; std::cout.flush();
                        m_authManager->ClearSavedToken("Spotify");
                        m_spotifyClient->SetAccessToken("");
                        m_spotifyClient->AuthWithSpDc(spDc);
                    }
                });
            }
        }
        // --- РЕЖИМ 2: Официальный PKCE ---
        else if (!clientId.isEmpty()) {
            if (savedToken.empty()) {
                std::string authUrl = m_spotifyClient->StartAuthPkce(clientId);
                StartAuthFlow("Spotify", QString::fromStdString(authUrl));
            } else {
                m_spotifyClient->SetAccessToken(savedToken);
                std::cout << "Проверка сохраненного токена Spotify (PKCE)...\n"; std::cout.flush();

                m_spotifyClient->ValidateToken([this, clientId](bool isValid) {
                    if (isValid) {
                        emit AuthUiStateChanged(false);
                        std::cout << "\n[УСПЕХ] Синхронизация треков Spotify...\n> "; std::cout.flush();
                        emit ProviderReady(true);
                        m_spotifyClient->FetchAllUserAudio(0, 50);
                    } else {
                        std::cout << "\n[ВНИМАНИЕ] Токен Spotify устарел. Открытие окна авторизации...\n"; std::cout.flush();
                        m_authManager->ClearSavedToken("Spotify");
                        m_spotifyClient->SetAccessToken("");
                        std::string authUrl = m_spotifyClient->StartAuthPkce(clientId);
                        StartAuthFlow("Spotify", QString::fromStdString(authUrl));
                    }
                });
            }
        }
        // --- ОШИБКА КОНФИГУРАЦИИ ---
        else {
            emit AuthUiStateChanged(false);
            std::cout << "\n[ОШИБКА] В .env не задан ни SPOTIFY_SP_DC, ни SPOTIFY_CLIENT_ID!\n> ";
            std::cout.flush();
        }
    });
}

void SourceRouter::StartYandexService() {
    m_authManager->GetSavedToken("Yandex", [this](const std::string& savedToken) {
        if (savedToken.empty()) {
            std::cout << "\n[Yandex] Токен не найден. Открываем окно авторизации...\n";
            std::cout.flush();
            StartAuthFlow("Yandex", "https://oauth.yandex.ru/authorize?response_type=token&client_id=23cabbbdc6cd418abb4b39c32c41195d");
        } else {
            std::cout << "\n[Yandex] Инициализация по сохраненному токену...\n";
            std::cout.flush();
            m_yandexClient->SetAccessToken(savedToken);
            emit ProviderReady(true);
            m_yandexClient->FetchAllUserAudio(0, 200);
        }
    });
}

void SourceRouter::SwitchSource(const std::string& newSource) {
    Logger::Log(LogLevel::INFO, "SourceRouter: Switching audio source to " + newSource);

    if (newSource == "VK") {
        m_currentProvider = m_vkClient.get();
    } else if (newSource == "Spotify") {
        m_currentProvider = m_spotifyClient.get();
    } else if (newSource == "SoundCloud") {
        m_currentProvider = m_soundCloudClient.get();
    } else if (newSource == "Yandex") {
        m_currentProvider = m_yandexClient.get();
    } else if (newSource == "Offline") {
        m_currentProvider = nullptr;
    }

    emit SourceChanged(newSource);

    if (newSource == "VK") {
        StartVkService();
    } else if (newSource == "Spotify") {
        StartSpotifyService();
    } else if (newSource == "SoundCloud") {
        StartSoundCloudService();
    } else if (newSource == "Yandex") {
        StartYandexService();
    } else if (newSource == "Offline") {
        emit AuthUiStateChanged(false);
        emit ProviderReady(false);
    }
}
```

## File: core/auth/router/SourceRouter.h
```c
#pragma once

#include "core/api/yandex/YandexClient.h"

#include <QObject>
#include <QString>
#include <QMap>
#include <memory>
#include <string>

class VkClient;
class SpotifyClient;
class SoundCloudClient;
class OAuthManager;
class IAudioProvider;
class QQmlApplicationEngine;

class SourceRouter : public QObject {
    Q_OBJECT
public:
    SourceRouter(const QMap<QString, QString>& envVars, QObject* parent = nullptr);
    ~SourceRouter() override;

    void SwitchSource(const std::string& newSource);
    IAudioProvider* GetCurrentProvider() const { return m_currentProvider; }

    VkClient* GetVkClient() const { return m_vkClient.get(); }
    SpotifyClient* GetSpotifyClient() const { return m_spotifyClient.get(); }
    SoundCloudClient* GetSoundCloudClient() const { return m_soundCloudClient.get(); }
    YandexClient* GetYandexClient() const { return m_yandexClient.get(); }

    OAuthManager* GetAuthManager() const { return m_authManager.get(); }

    signals:
    void SourceChanged(const std::string& newSource);
    void ProviderReady(bool isOnline);
    void AuthUiStateChanged(bool isWaiting);

private slots:
    void OnVkTokenReceived(const std::string& token);
    void OnSpotifyTokenReceived(const std::string& token);
    void OnSpotifyAuthError(const std::string& err);
    void OnVkTokenExpired();

private:
    void StartVkService();
    void StartSpotifyService();
    void StartSoundCloudService();
    void StartYandexService();

    void StartAuthFlow(const QString& service, const QString& authUrl);

    std::unique_ptr<VkClient> m_vkClient;
    std::unique_ptr<SpotifyClient> m_spotifyClient;
    std::unique_ptr<SoundCloudClient> m_soundCloudClient;
    std::unique_ptr<YandexClient> m_yandexClient;
    
    std::unique_ptr<OAuthManager> m_authManager;

    QQmlApplicationEngine* m_authEngine = nullptr;
    QString m_currentAuthService;
    QMap<QString, QString> m_envVars;
    IAudioProvider* m_currentProvider = nullptr;
};
```

## File: core/auth/auth.qml
```
import QtQuick
import QtQuick.Window
import QtWebView
import QtQml

Window {
    width: 900
    height: 700
    visible: true
    title: "Авторизация"

    WebView {
        id: webView
        anchors.fill: parent
        url: cppAuthUrl

        onUrlChanged: {
            console.log("[QML] Текущий URL: " + url.toString())
            cppAuthManager.onUrlIntercepted(url.toString())
        }
    }

    Timer {
        id: universalSniper
        interval: 300
        running: true
        repeat: true
        onTriggered: {
            // --- SOUNDCLOUD ---
            if (cppAuthUrl.indexOf("soundcloud.com") !== -1) {
                var scCode = `
                    (function() {
                        var token = window.localStorage.getItem('oauth_token');
                        if (token) return token;
                        var cookies = document.cookie.split(';');
                        for (var i = 0; i < cookies.length; i++) {
                            var c = cookies[i].trim();
                            if (c.indexOf('oauth_token=') === 0) {
                                return c.substring('oauth_token='.length, c.length);
                            }
                        }
                        return "";
                    })();
                `;
                webView.runJavaScript(scCode, function(result) {
                    if (result && result !== "null" && result !== "") {
                        console.log("[QML] SoundCloud Token intercepted.");
                        cppAuthManager.onScTokenIntercepted(result);
                        universalSniper.running = false;
                    }
                });
            }
            // --- ВКОНТАКТЕ ---
            else if (cppAuthUrl.indexOf("oauth.vk.com") !== -1) {
                var vkCode = `
                    (function() {
                        var btn = document.querySelector('button[type="submit"]') || document.querySelector('.oauth_button .flat_button');
                        if (btn && !btn.disabled) {
                            btn.click();
                            return true;
                        }
                        return false;
                    })();
                `;
                webView.runJavaScript(vkCode, function(result) {
                    if (result === true) {
                        console.log("[QML] VK: Authorization.");
                    }
                });
            }
        }
    }
}
```

## File: core/lyrics/LyricsFetcher.cpp
```cpp
#include "LyricsFetcher.h"
#include "utils/logger/Logger.h"

#include <QUrl>
#include <QUrlQuery>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

LyricsFetcher::LyricsFetcher(QObject* parent) 
    : QObject(parent), m_manager(new QNetworkAccessManager(this)) {
    Logger::Log(LogLevel::INFO, "LyricsFetcher created.");
}

LyricsFetcher::~LyricsFetcher() {
    Logger::Log(LogLevel::INFO, "LyricsFetcher destroyed.");
}

// ЭТАП 1: Точный поиск (LRCLIB /api/get)
void LyricsFetcher::FetchLyrics(const std::string& artist, const std::string& title, std::function<void(const std::string&)> callback) {
    if (artist.empty() || title.empty()) {
        callback("");
        return;
    }

    QUrl url("https://lrclib.net/api/get");
    QUrlQuery query;
    query.addQueryItem("artist_name", QString::fromStdString(artist));
    query.addQueryItem("track_name", QString::fromStdString(title));
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", "VKAudioPlayer/1.0 (C++ Qt)");

    QNetworkReply* reply = m_manager->get(request);

    QObject::connect(reply, &QNetworkReply::finished, this, [this, reply, artist, title, callback]() {
        if (reply->error() == QNetworkReply::NoError) {
            QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            QJsonObject root = doc.object();

            QString synced = root["syncedLyrics"].toString();
            QString plain = root["plainLyrics"].toString();
            QString result = !synced.isEmpty() ? synced : plain;

            if (!result.isEmpty()) {
                Logger::Log(LogLevel::INFO, "LyricsFetcher: Exact match found in LRCLIB.");
                callback(result.toStdString());
                reply->deleteLater();
                return;
            }
        }

        reply->deleteLater();
        SearchLrcLibFallback(artist, title, callback);
    });
}

// ЭТАП 2: Нечеткий поиск (LRCLIB /api/search)
void LyricsFetcher::SearchLrcLibFallback(const std::string& artist, const std::string& title, std::function<void(const std::string&)> callback) {
    QUrl url("https://lrclib.net/api/search");
    QUrlQuery query;
    query.addQueryItem("q", QString::fromStdString(artist + " " + title));
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", "VKAudioPlayer/1.0 (C++ Qt)");

    QNetworkReply* reply = m_manager->get(request);

    QObject::connect(reply, &QNetworkReply::finished, this, [reply, callback]() {
        if (reply->error() == QNetworkReply::NoError) {
            QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            if (doc.isArray() && !doc.array().isEmpty()) {
                QJsonObject firstHit = doc.array()[0].toObject();
                QString synced = firstHit["syncedLyrics"].toString();
                QString plain = firstHit["plainLyrics"].toString();
                QString result = !synced.isEmpty() ? synced : plain;

                if (!result.isEmpty()) {
                    Logger::Log(LogLevel::INFO, "LyricsFetcher: Search match found in LRCLIB.");
                    callback(result.toStdString());
                    reply->deleteLater();
                    return;
                }
            }
        }

        // Если и тут пусто — отдаем пустую строку и не мучаем систему
        Logger::Log(LogLevel::WARNING, "LyricsFetcher: No lyrics found in LRCLIB databases.");
        callback("");
        reply->deleteLater();
    });
}
```

## File: core/lyrics/LyricsFetcher.h
```c
#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <string>
#include <functional>

class LyricsFetcher : public QObject {
    Q_OBJECT
public:
    explicit LyricsFetcher(QObject* parent = nullptr);
    ~LyricsFetcher();

    void FetchLyrics(const std::string& artist, const std::string& title, std::function<void(const std::string&)> callback);

private:
    void SearchLrcLibFallback(const std::string& artist, const std::string& title, std::function<void(const std::string&)> callback);

    QNetworkAccessManager* m_manager;
};
```

## File: core/playlist/PlaylistManager.cpp
```cpp
#include "PlaylistManager.h"
#include "utils/logger/Logger.h"
#include <random>
#include <algorithm>

void PlaylistManager::AddTrack(const Track& track) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_tracks.push_back(track);
    m_playQueue.push_back(m_tracks.size() - 1);
}

bool PlaylistManager::HasTracks() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return !m_tracks.empty();
}

bool PlaylistManager::IsShuffle() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_isShuffle;
}

Track PlaylistManager::GetCurrentTrack() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_tracks.empty() || m_queueIndex < 0 || m_queueIndex >= m_playQueue.size()) {
        return Track();
    }
    return m_tracks[m_playQueue[m_queueIndex]];
}

int PlaylistManager::GetCurrentAbsoluteIndex() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_tracks.empty() ? -1 : m_playQueue[m_queueIndex];
}

Track PlaylistManager::PeekNextTrack() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_tracks.empty() || m_playQueue.empty()) return Track();

    if (m_repeatMode == RepeatMode::One) return m_tracks[m_playQueue[m_queueIndex]];

    int previewIndex = m_queueIndex + 1;

    if (previewIndex >= m_playQueue.size()) {
        if (m_repeatMode == RepeatMode::All) {
            previewIndex = 0;
        } else {
            return Track();
        }
    }
    return m_tracks[m_playQueue[previewIndex]];
}

void PlaylistManager::RebuildQueue(bool keepCurrentTrack) {
    // Внимание: Этот метод вызывается ИЗ ДРУГИХ методов, которые УЖЕ залочили мьютекс.
    if (m_tracks.empty()) return;

    int currentTrackIndex = m_playQueue.empty() ? 0 : m_playQueue[m_queueIndex];

    m_playQueue.clear();
    for (int i = 0; i < m_tracks.size(); ++i) {
        m_playQueue.push_back(i);
    }

    if (m_isShuffle) {
        std::random_device rd;
        std::mt19937 g(rd());
        std::shuffle(m_playQueue.begin(), m_playQueue.end(), g);

        if (keepCurrentTrack) {
            auto it = std::find(m_playQueue.begin(), m_playQueue.end(), currentTrackIndex);
            if (it != m_playQueue.end()) {
                std::iter_swap(m_playQueue.begin(), it);
            }
        }
        m_queueIndex = 0;
    } else {
        if (keepCurrentTrack) {
            m_queueIndex = currentTrackIndex;
        } else {
            m_queueIndex = 0;
        }
    }
}

void PlaylistManager::Next() {
    Track nextTrack;
    bool shouldPlay = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_tracks.empty()) return;

        if (m_repeatMode == RepeatMode::One) {
            // Остаемся на том же треке
        } else {
            m_queueIndex++;
            if (m_queueIndex >= m_playQueue.size()) {
                if (m_repeatMode == RepeatMode::All) {
                    if (m_isShuffle) RebuildQueue(false);
                    else m_queueIndex = 0;
                } else {
                    m_queueIndex--;
                    Logger::Log(LogLevel::INFO, "Playlist reached the end.");
                    return;
                }
            }
        }
        nextTrack = m_tracks[m_playQueue[m_queueIndex]];
        shouldPlay = true;
    } // Мьютекс разблокирован здесь

    // Дергаем коллбек ВНЕ блокировки мьютекса
    if (shouldPlay && OnTrackRequested) {
        OnTrackRequested(nextTrack);
    }
}

void PlaylistManager::Previous() {
    Track prevTrack;
    bool shouldPlay = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_tracks.empty()) return;

        if (m_repeatMode != RepeatMode::One) {
            m_queueIndex--;
            if (m_queueIndex < 0) {
                m_queueIndex = m_repeatMode == RepeatMode::All ? m_playQueue.size() - 1 : 0;
            }
        }
        prevTrack = m_tracks[m_playQueue[m_queueIndex]];
        shouldPlay = true;
    }

    if (shouldPlay && OnTrackRequested) {
        OnTrackRequested(prevTrack);
    }
}

void PlaylistManager::JumpTo(int index) {
    Track targetTrack;
    bool shouldPlay = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (index < 0 || index >= m_tracks.size()) {
            Logger::Log(LogLevel::WARNING, "Invalid track index!");
            return;
        }

        auto it = std::find(m_playQueue.begin(), m_playQueue.end(), index);
        if (it != m_playQueue.end()) {
            m_queueIndex = std::distance(m_playQueue.begin(), it);
            targetTrack = m_tracks[m_playQueue[m_queueIndex]];
            shouldPlay = true;
        }
    }

    if (shouldPlay && OnTrackRequested) {
        OnTrackRequested(targetTrack);
    }
}

void PlaylistManager::JumpToQueueIndex(int index) {
    Track targetTrack;
    bool shouldPlay = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (index < 0 || index >= m_playQueue.size()) {
            Logger::Log(LogLevel::WARNING, "Invalid queue index!");
            return;
        }

        m_queueIndex = index;
        targetTrack = m_tracks[m_playQueue[m_queueIndex]];
        shouldPlay = true;
    }

    if (shouldPlay && OnTrackRequested) {
        OnTrackRequested(targetTrack);
    }
}

void PlaylistManager::ToggleShuffle() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_isShuffle = !m_isShuffle;
    RebuildQueue(!m_isShuffle);
    Logger::Log(LogLevel::INFO, std::string("Shuffle is now ") + (m_isShuffle ? "ON" : "OFF"));
}

void PlaylistManager::SetShuffle(bool enable) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!enable && !m_isShuffle) return;

    m_isShuffle = enable;
    RebuildQueue(!enable);
    Logger::Log(LogLevel::INFO, std::string("Shuffle is now ") + (m_isShuffle ? "ON (Reshuffled)" : "OFF"));
}

void PlaylistManager::ToggleRepeat() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_repeatMode == RepeatMode::All) {
        m_repeatMode = RepeatMode::One;
        Logger::Log(LogLevel::INFO, "Repeat Mode: ONE TRACK");
    } else if (m_repeatMode == RepeatMode::One) {
        m_repeatMode = RepeatMode::None;
        Logger::Log(LogLevel::INFO, "Repeat Mode: NONE");
    } else {
        m_repeatMode = RepeatMode::All;
        Logger::Log(LogLevel::INFO, "Repeat Mode: ALL TRACKS");
    }
}

std::vector<Track> PlaylistManager::GetQueueTracks() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<Track> queue;
    for (int index : m_playQueue) {
        queue.push_back(m_tracks[index]);
    }
    return queue;
}

std::vector<Track> PlaylistManager::GetAllTracks() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_tracks;
}

void PlaylistManager::InsertTrack(int position, const Track& track) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (position < 0) position = 0;
    if (position > m_tracks.size()) position = m_tracks.size();

    m_tracks.insert(m_tracks.begin() + position, track);

    for (int& idx : m_playQueue) {
        if (idx >= position) {
            idx++;
        }
    }

    if (m_isShuffle) {
        m_playQueue.push_back(position);
    } else {
        m_playQueue.insert(m_playQueue.begin() + position, position);

        if (position <= m_queueIndex) {
            m_queueIndex++;
        }
    }
}

void PlaylistManager::Clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_tracks.clear();
    m_playQueue.clear();
    m_queueIndex = 0;
}

void PlaylistManager::RestoreShuffleQueue(const std::vector<std::string>& shuffledIds) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_isShuffle = true;
    m_playQueue.clear();

    for (const std::string& id : shuffledIds) {
        auto it = std::find_if(m_tracks.begin(), m_tracks.end(), [&](const Track& t){ return t.id == id; });
        if (it != m_tracks.end()) {
            m_playQueue.push_back(std::distance(m_tracks.begin(), it));
        }
    }

    if (m_playQueue.size() != m_tracks.size()) {
        m_playQueue.clear();
        for (int i = 0; i < m_tracks.size(); ++i) m_playQueue.push_back(i);

        std::random_device rd;
        std::mt19937 g(rd());
        std::shuffle(m_playQueue.begin(), m_playQueue.end(), g);
        Logger::Log(LogLevel::WARNING, "PlaylistManager: Shuffle queue mismatched, created a new one.");
    } else {
        Logger::Log(LogLevel::INFO, "PlaylistManager: Shuffle queue successfully restored from DB.");
    }
}

int PlaylistManager::GetRepeatMode() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return static_cast<int>(m_repeatMode);
}

void PlaylistManager::SetRepeatMode(int mode) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (mode >= 0 && mode <= 2) {
        m_repeatMode = static_cast<RepeatMode>(mode);
    }
}
```

## File: core/playlist/PlaylistManager.h
```c
#pragma once
#include <vector>
#include <functional>
#include <mutex>
#include "../../models/Track.h"

enum class RepeatMode { None, All, One };

class PlaylistManager {
public:
    PlaylistManager() = default;

    void AddTrack(const Track& track);
    bool HasTracks() const;
    bool IsShuffle() const;

    Track GetCurrentTrack() const;
    int GetCurrentAbsoluteIndex() const;
    Track PeekNextTrack() const;

    void Next();
    void Previous();
    void JumpTo(int index);
    void JumpToQueueIndex(int queueIndex);
    
    void InsertTrack(int position, const Track& track);

    void ToggleShuffle();
    void SetShuffle(bool enable);
    void ToggleRepeat();
    int GetRepeatMode() const;
    void SetRepeatMode(int mode);
    void RestoreShuffleQueue(const std::vector<std::string>& shuffledIds);

    void Clear();

    std::function<void(const Track&)> OnTrackRequested;
    std::vector<Track> GetQueueTracks() const;
    std::vector<Track> GetAllTracks() const;

private:
    std::vector<Track> m_tracks;
    std::vector<int> m_playQueue;
    int m_queueIndex = 0;

    bool m_isShuffle = false;
    RepeatMode m_repeatMode = RepeatMode::All;

    mutable std::mutex m_mutex; // Защита от одновременного доступа потоков

    void RebuildQueue(bool keepCurrentTrack);
};
```

## File: core/shazam/ShazamSignatureBuilder.cpp
```cpp
#include "ShazamSignatureBuilder.h"
#include "utils/logger/Logger.h"
#include "core/audio/miniaudio/miniaudio.h"
#include "signature.pb.h"

#include <QString>
#include <complex>
#include <cmath>
#include <algorithm>
#include <QByteArray>
#include <QDataStream>
#include <QIODevice>
#include <map>

namespace {
    const double PI = 3.14159265358979323846;
    typedef std::complex<double> Complex;

    void FFT(std::vector<Complex>& a) {
        size_t n = a.size();
        if (n <= 1) return;
        std::vector<Complex> a0(n / 2), a1(n / 2);
        for (size_t i = 0; i < n / 2; i++) {
            a0[i] = a[i * 2];
            a1[i] = a[i * 2 + 1];
        }
        FFT(a0);
        FFT(a1);
        double ang = 2 * PI / n;
        Complex w(1), wn(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n / 2; i++) {
            a[i] = a0[i] + w * a1[i];
            a[i + n / 2] = a0[i] - w * a1[i];
            w *= wn;
        }
    }

    quint32 CalculateCRC32(const std::string& data) {
        quint32 crc = 0xFFFFFFFF;
        for (size_t i = 0; i < data.size(); i++) {
            crc ^= static_cast<quint8>(data[i]);
            for (int j = 0; j < 8; j++) {
                crc = (crc >> 1) ^ (0xEDB88320 & (-(crc & 1)));
            }
        }
        return ~crc;
    }
}

std::vector<int16_t> ShazamSignatureBuilder::ReadAudioFile(const std::string& filePath) {
    ma_decoder_config config = ma_decoder_config_init(ma_format_s16, 1, 16000);
    ma_decoder decoder;

    QString qFilePath = QString::fromStdString(filePath);

#ifdef _WIN32
    if (ma_decoder_init_file_w(reinterpret_cast<const wchar_t*>(qFilePath.utf16()), &config, &decoder) != MA_SUCCESS) {
#else
    if (ma_decoder_init_file(filePath.c_str(), &config, &decoder) != MA_SUCCESS) {
#endif
        Logger::Log(LogLevel::ERROR, "Shazam: Не удалось открыть файл");
        return {};
     }
    /*
    if (ma_decoder_seek_to_pcm_frame(&decoder, 16000 * 30) != MA_SUCCESS) {
        ma_decoder_seek_to_pcm_frame(&decoder, 0);
    }
    */

    std::vector<int16_t> pcmData;
    int16_t tempBuffer[4096];
    ma_uint64 framesRead;

    while (true) {
        ma_decoder_read_pcm_frames(&decoder, tempBuffer, 4096, &framesRead);
        if (framesRead == 0) break;
        pcmData.insert(pcmData.end(), tempBuffer, tempBuffer + framesRead);
    }
    ma_decoder_uninit(&decoder);

    size_t maxSamples = 16000 * 12;
    if (pcmData.size() > maxSamples) pcmData.resize(maxSamples);

    return pcmData;
}

std::vector<std::vector<double>> ShazamSignatureBuilder::GenerateSpectrogram(const std::vector<int16_t>& pcm) {
    const size_t windowSize = 2048;
    const size_t stepSize = 128; // Apple Standard
    std::vector<std::vector<double>> spectrogram;

    if (pcm.size() < windowSize) return spectrogram;

    std::vector<double> hannWindow(windowSize);
    for (size_t i = 0; i < windowSize; ++i) {
        // Классическое окно Ханна
        hannWindow[i] = 0.5 * (1.0 - std::cos(2.0 * PI * i / (windowSize - 1)));
    }

    for (size_t offset = 0; offset + windowSize <= pcm.size(); offset += stepSize) {
        std::vector<Complex> frame(windowSize);
        for (size_t i = 0; i < windowSize; ++i) {
            double sample = pcm[offset + i] / 32768.0;
            frame[i] = Complex(sample * hannWindow[i], 0.0);
        }
        FFT(frame);

        std::vector<double> magnitudes(windowSize / 2);
        for (size_t i = 0; i < windowSize / 2; ++i) {
            // ВАЖНО: Берем квадрат модуля (Power Spectrum), как в оригинальном ядре!
            double mag = std::abs(frame[i]);
            magnitudes[i] = mag * mag;
        }
        spectrogram.push_back(std::move(magnitudes));
    }
    return spectrogram;
}

std::vector<Peak> ShazamSignatureBuilder::FindPeaks(const std::vector<std::vector<double>>& spectrogram) {
    std::vector<Peak> finalPeaks;
    if (spectrogram.empty()) return finalPeaks;

    int numFrames = spectrogram.size();

    // Окно локального максимума: 3х3 (строгий стандарт поиска точек)
    const int tWindow = 1;
    const int fWindow = 1;

    std::vector<Peak> localMaxes;

    for (int t = 0; t < numFrames; ++t) {
        // Границы 4 полос Apple: от 32 до 703 бина
        for (int f = 32; f < 704; ++f) {
            double amp = spectrogram[t][f];

            // Базовый фильтр тишины (очень низкий)
            if (amp < 0.0001) continue;

            bool isMax = true;
            double localSum = 0.0;
            int count = 0;

            int tStart = std::max(0, t - tWindow);
            int tEnd = std::min(numFrames - 1, t + tWindow);
            int fStart = std::max(32, f - fWindow);
            int fEnd = std::min(703, f + fWindow);

            // Ищем локальный максимум И одновременно считаем шум вокруг
            for (int dt = tStart; dt <= tEnd; ++dt) {
                for (int df = fStart; df <= fEnd; ++df) {
                    double neighborAmp = spectrogram[dt][df];
                    localSum += neighborAmp;
                    count++;

                    if (dt == t && df == f) continue;
                    if (neighborAmp >= amp) {
                        isMax = false;
                    }
                }
            }

            // Адаптивный порог: пик должен быть в X раз громче среднего фона
            double localAverage = localSum / count;
            // Условие пробития шума:
            if (isMax && amp > (localAverage * 1.5)) {
                localMaxes.push_back({t, f, amp});
            }
        }
    }

    // Равномерное распределение по секундам (оставляем твою отличную реализацию!)
    const int framesPerSec = 125;
    for (int sec = 0; sec < (numFrames / framesPerSec) + 1; ++sec) {
        int startFrame = sec * framesPerSec;
        int endFrame = startFrame + framesPerSec;

        std::vector<Peak> secPeaks;
        for (const auto& p : localMaxes) {
            if (p.timeFrame >= startFrame && p.timeFrame < endFrame) {
                secPeaks.push_back(p);
            }
        }

        std::sort(secPeaks.begin(), secPeaks.end(), [](const Peak& a, const Peak& b) {
            return a.amplitude > b.amplitude;
        });

        // Берем топ-40 самых выраженных точек в секунду
        if (secPeaks.size() > 40) {
            secPeaks.resize(40);
        }
        finalPeaks.insert(finalPeaks.end(), secPeaks.begin(), secPeaks.end());
    }

    return finalPeaks;
}

QByteArray ShazamSignatureBuilder::AssemblePayload(const std::vector<Peak>& peaks, int totalSamples) {
    shazam::DecodedMessage msg;
    msg.set_sample_rate_hz(16000);
    msg.set_number_samples(totalSamples);

    std::map<int, shazam::FrequencyBand*> bands;
    for (int i = 0; i < 4; i++) {
        auto b = msg.add_frequency_band();
        b->set_band_index(i);
        bands[i] = b;
    }

    auto sortedPeaks = peaks;
    std::sort(sortedPeaks.begin(), sortedPeaks.end(), [](const Peak& a, const Peak& b) {
        return a.timeFrame < b.timeFrame;
    });

    for (const auto& p : sortedPeaks) {
        int bandIdx = -1;
        int freqOffset = 0;

        // Точные границы как в Rust-ядре shazamio-core:
        if (p.freqBin >= 32 && p.freqBin <= 66) {
            bandIdx = 0;
            freqOffset = 32;
        }
        else if (p.freqBin >= 67 && p.freqBin <= 184) {
            bandIdx = 1;
            freqOffset = 67;
        }
        else if (p.freqBin >= 185 && p.freqBin <= 435) {
            bandIdx = 2;
            freqOffset = 185;
        }
        else if (p.freqBin >= 436 && p.freqBin <= 703) {
            bandIdx = 3;
            freqOffset = 436;
        }

        if (bandIdx != -1) {
            bands[bandIdx]->add_hash_array(p.freqBin - freqOffset);
            bands[bandIdx]->add_hash_array(p.timeFrame);
        }
    }

    std::string pbData = msg.SerializeAsString();
    quint32 crc = CalculateCRC32(pbData);

    QByteArray header;
    QDataStream stream(&header, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);

    stream << (quint32)0xCAFE2580;
    stream << (quint32)crc;
    stream << (quint32)pbData.size();

    header.append(48 - header.size(), '\0');

    QByteArray finalPayload = header;
    finalPayload.append(pbData.c_str(), pbData.size());

    return finalPayload;
}

QByteArray ShazamSignatureBuilder::BuildFromFile(const std::string& filePath) {
    std::vector<int16_t> pcm = ReadAudioFile(filePath);
    if (pcm.empty()) return QByteArray();

    std::vector<std::vector<double>> spectrogram = GenerateSpectrogram(pcm);
    std::vector<Peak> peaks = FindPeaks(spectrogram);

    Logger::Log(LogLevel::INFO, "Shazam: Упаковано " + std::to_string(peaks.size()) + " мощных пиков в Protobuf (Apple V5).");

    return AssemblePayload(peaks, pcm.size());
}
```

## File: core/shazam/ShazamSignatureBuilder.h
```c
#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <QByteArray>

struct Peak {
    int timeFrame;
    int freqBin;
    double amplitude;
};

class ShazamSignatureBuilder {
public:
    static QByteArray BuildFromFile(const std::string& filePath);

private:
    static std::vector<int16_t> ReadAudioFile(const std::string& filePath);
    static std::vector<std::vector<double>> GenerateSpectrogram(const std::vector<int16_t>& pcm);
    static std::vector<Peak> FindPeaks(const std::vector<std::vector<double>>& spectrogram);
    static QByteArray AssemblePayload(const std::vector<Peak>& peaks, int totalSamples);
};
```

## File: core/shazam/signature.proto
```protobuf
syntax = "proto3";

package shazam;

message FrequencyBand {
  uint32 band_index = 1;
  repeated uint32 hash_array = 2;
}

message DecodedMessage {
  uint32 sample_rate_hz = 1;
  uint32 number_samples = 2;
  repeated FrequencyBand frequency_band = 3;
}
```

## File: models/Track.h
```c
#pragma once
#include <string>

struct Track {
    std::string id;
    std::string source;
    std::string ownerId;
    std::string artist;
    std::string title;
    std::string url;
    int duration = 0; // в секундах
    std::string coverUrl = "";
    std::string lyrics_id;
    std::string lyrics;

    // Вывод времени (ММ:СС)
    std::string GetFormattedDuration() const {
        int m = duration / 60;
        int s = duration % 60;
        std::string min = std::to_string(m);
        std::string sec = s < 10 ? "0" + std::to_string(s) : std::to_string(s);
        return min + ":" + sec;
    }

    std::string GetSafeFilename() const {
        std::string name = id + " - " + artist + " - " + title;
        const std::string invalidChars = "\\/:*?\"<>|";
        for (char& c : name) {
            if (invalidChars.find(c) != std::string::npos) {
                c = '_';
            }
        }
        return name;
    }
};
```

## File: services/database/DatabaseManager.cpp
```cpp
#include "DatabaseManager.h"
#include "utils/logger/Logger.h"
#include "utils/path/PathManager.h"
#include <QSqlError>
#include <QVariant>
#include <QFile>
#include <QTextStream>
#include <QStringList>

DatabaseManager::DatabaseManager() {
    m_db = QSqlDatabase::addDatabase("QSQLITE");
    m_db.setDatabaseName(PathManager::GetDbPath());
}

DatabaseManager::~DatabaseManager() {
    if (m_db.isOpen()) {
        m_db.close();
    }
}

bool DatabaseManager::Init() {
    if (!m_db.open()) {
        Logger::Log(LogLevel::ERROR, "DB: Failed to open database: " + m_db.lastError().text().toStdString());
        return false;
    }
    CreateTables();
    return true;
}

void DatabaseManager::CreateTables() {
    QSqlQuery query;
    query.exec("CREATE TABLE IF NOT EXISTS Settings (key TEXT PRIMARY KEY, value TEXT)");

    query.exec("CREATE TABLE IF NOT EXISTS Tracks ("
               "id TEXT PRIMARY KEY, "
               "source TEXT, "
               "artist TEXT, "
               "title TEXT, "
               "duration TEXT, "
               "cover_url TEXT, "
               "lyrics_id TEXT, "
               "lyrics TEXT)");

    query.exec("CREATE TABLE IF NOT EXISTS PlayQueue ("
               "position INTEGER, "
               "id TEXT, "
               "source TEXT, "
               "is_shuffle INTEGER, "
               "PRIMARY KEY(position, source, is_shuffle))");

    query.exec("CREATE INDEX IF NOT EXISTS idx_playqueue_lookup ON PlayQueue(source, is_shuffle)");
    query.exec("CREATE INDEX IF NOT EXISTS idx_tracks_source ON Tracks(source)");
}

void DatabaseManager::SetSetting(const QString& key, const QString& value) {
    QSqlQuery query;
    query.prepare("INSERT OR REPLACE INTO Settings (key, value) VALUES (:key, :value)");
    query.bindValue(":key", key);
    query.bindValue(":value", value);
    query.exec();
}

QString DatabaseManager::GetSetting(const QString& key) const {
    QSqlQuery query;
    query.prepare("SELECT value FROM Settings WHERE key = :key");
    query.bindValue(":key", key);
    if (query.exec() && query.next()) {
        return query.value(0).toString();
    }
    return "";
}

void DatabaseManager::ClearSetting(const QString& key) {
    QSqlQuery query;
    query.prepare("DELETE FROM Settings WHERE key = :key");
    query.bindValue(":key", key);
    query.exec();
}

void DatabaseManager::ExportQueueToTxt(const std::vector<Track>& queue, const QString& filename, bool isShuffle) const {
    QString exportPath = PathManager::GetPlaylistExportPath(filename);
    QFile file(exportPath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
        out << "=== ТЕКУЩИЙ ПЛЕЙЛИСТ ===\n";
        out << "Режим: " << (isShuffle ? "SHUFFLE" : "СТАНДАРТНЫЙ") << "\n";
        out << "-------------------------\n\n";

        for (size_t i = 0; i < queue.size(); ++i) {
            out << "[" << (i + 1) << "]. " << QString::fromStdString(queue[i].artist)
                << " - " << QString::fromStdString(queue[i].title)
                << " [" << QString::fromStdString(queue[i].GetFormattedDuration()) << "]\n";
        }
        file.close();
    } else {
        Logger::Log(LogLevel::ERROR, "DB: Failed to generate playlist export.");
    }
}

void DatabaseManager::SaveTracks(const std::vector<Track>& tracks) {
    QSqlQuery query;
    m_db.transaction();

    query.prepare("INSERT OR REPLACE INTO Tracks (id, source, artist, title, duration, cover_url, lyrics_id, lyrics) "
                  "VALUES (:id, :source, :artist, :title, :duration, :cover_url, :lyrics_id, :lyrics)");

    for (const auto& track : tracks) {
        query.bindValue(":id", QString::fromStdString(track.id));
        query.bindValue(":source", QString::fromStdString(track.source));
        query.bindValue(":artist", QString::fromStdString(track.artist));
        query.bindValue(":title", QString::fromStdString(track.title));
        query.bindValue(":duration", QString::fromStdString(track.GetFormattedDuration()));
        query.bindValue(":cover_url", QString::fromStdString(track.coverUrl));
        query.bindValue(":lyrics_id", QString::fromStdString(track.lyrics_id));
        query.bindValue(":lyrics", QString::fromStdString(track.lyrics));
        query.exec();
    }
    m_db.commit();
}

void DatabaseManager::SaveQueue(const std::vector<Track>& currentQueue, const std::string& source, bool isShuffle) {
    QSqlQuery query;
    m_db.transaction();

    query.prepare("DELETE FROM PlayQueue WHERE is_shuffle = :is_shuffle AND source = :source");
    query.bindValue(":is_shuffle", isShuffle ? 1 : 0);
    query.bindValue(":source", QString::fromStdString(source));
    query.exec();

    query.prepare("INSERT INTO PlayQueue (position, id, source, is_shuffle) VALUES (:pos, :id, :source, :shuffle)");
    for (size_t i = 0; i < currentQueue.size(); ++i) {
        query.bindValue(":pos", static_cast<int>(i));
        query.bindValue(":id", QString::fromStdString(currentQueue[i].id));
        query.bindValue(":source", QString::fromStdString(source));
        query.bindValue(":shuffle", isShuffle ? 1 : 0);
        query.exec();
    }
    m_db.commit();
}

std::vector<Track> DatabaseManager::LoadTracks(const std::string& source) {
    std::vector<Track> tracks;
    QSqlQuery query;

    if (source == "Offline") {
        query.prepare("SELECT id, artist, title, duration, cover_url, lyrics_id, lyrics, source "
                      "FROM Tracks");
    } else {
        query.prepare("SELECT t.id, t.artist, t.title, t.duration, t.cover_url, t.lyrics_id, t.lyrics, t.source "
                      "FROM PlayQueue q "
                      "JOIN Tracks t ON q.id = t.id "
                      "WHERE q.is_shuffle = 0 AND q.source = :source "
                      "ORDER BY q.position ASC");
        query.bindValue(":source", QString::fromStdString(source));
    }

    query.exec();

    while (query.next()) {
        Track t;
        t.id = query.value(0).toString().toStdString();
        t.artist = query.value(1).toString().toStdString();
        t.title = query.value(2).toString().toStdString();

        QString durationStr = query.value(3).toString();
        QStringList parts = durationStr.split(':');
        if (parts.size() == 2) {
            t.duration = parts[0].toInt() * 60 + parts[1].toInt();
        } else {
            t.duration = 0;
        }

        t.coverUrl = query.value(4).toString().toStdString();
        t.lyrics_id = query.value(5).toString().toStdString();
        t.lyrics = query.value(6).toString().toStdString();
        t.source = query.value(7).toString().toStdString();

        tracks.push_back(t);
    }

    Logger::Log(LogLevel::INFO, "DB: Loaded " + std::to_string(tracks.size()) + " tracks for source " + source);
    return tracks;
}

void DatabaseManager::UpdateTrackLyrics(const std::string& trackId, const std::string& lyrics) {
    QSqlQuery query;
    query.prepare("UPDATE Tracks SET lyrics = :lyrics WHERE id = :id");
    query.bindValue(":lyrics", QString::fromStdString(lyrics));
    query.bindValue(":id", QString::fromStdString(trackId));
    if (!query.exec()) {
        Logger::Log(LogLevel::ERROR, "DB: Failed to update lyrics for track " + trackId);
    }
}

std::vector<std::string> DatabaseManager::LoadQueueIds(const std::string& source, bool isShuffle) const {
    std::vector<std::string> ids;
    QSqlQuery query;
    query.prepare("SELECT id FROM PlayQueue WHERE source = :source AND is_shuffle = :shuffle ORDER BY position ASC");
    query.bindValue(":source", QString::fromStdString(source));
    query.bindValue(":shuffle", isShuffle ? 1 : 0);
    if (query.exec()) {
        while (query.next()) {
            ids.push_back(query.value(0).toString().toStdString());
        }
    }
    return ids;
}
```

## File: services/database/DatabaseManager.h
```c
#pragma once
#include <QString>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <vector>
#include "models/Track.h"

class DatabaseManager {
public:
    DatabaseManager();
    ~DatabaseManager();

    bool Init();

    // --- Настройки (Токен) ---
    void SetSetting(const QString& key, const QString& value);
    QString GetSetting(const QString& key) const;
    void ClearSetting(const QString& key);

    // --- Треки и очередь ---
    void SaveTracks(const std::vector<Track>& tracks);
    void SaveQueue(const std::vector<Track>& currentQueue, const std::string& source, bool isShuffle);
    std::vector<std::string> LoadQueueIds(const std::string& source, bool isShuffle) const;

    // Вывод в TXT прямо из БД
    void ExportQueueToTxt(const std::vector<Track>& queue, const QString& filename, bool isShuffle) const;
    std::vector<Track> LoadTracks(const std::string& source);

    // Обновление локального кэша текста
    void UpdateTrackLyrics(const std::string& trackId, const std::string& lyrics);

private:
    QSqlDatabase m_db;
    void CreateTables();
};
```

## File: services/downloader/TrackDownloader.cpp
```cpp
#include "TrackDownloader.h"
#include "utils/logger/Logger.h"
#include "utils/path/PathManager.h"
#include "services/network/NetworkStreamer.h"
#include "utils/parser/MpegTsDemuxer.h"

#include <QDir>
#include <QFile>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <memory>
#include <algorithm>
#include <iostream>
#include <QCoreApplication>

namespace {
    QByteArray makeSyncSafe(uint32_t size) {
        QByteArray b(4, 0);
        b[0] = (size >> 21) & 0x7F;
        b[1] = (size >> 14) & 0x7F;
        b[2] = (size >> 7) & 0x7F;
        b[3] = size & 0x7F;
        return b;
    }

    QByteArray makeUInt32BE(uint32_t size) {
        QByteArray b(4, 0);
        b[0] = (size >> 24) & 0xFF;
        b[1] = (size >> 16) & 0xFF;
        b[2] = (size >> 8) & 0xFF;
        b[3] = size & 0xFF;
        return b;
    }

    QByteArray createFrame(const QByteArray& id, const QByteArray& data) {
        QByteArray frame;
        frame.append(id.left(4));
        frame.append(makeUInt32BE(data.size()));
        frame.append('\0');
        frame.append('\0');
        frame.append(data);
        return frame;
    }

    QByteArray createTextFrame(const QByteArray& id, const QString& text) {
        QByteArray data;
        data.append((char)0x01);
        data.append((char)0xFF);
        data.append((char)0xFE);
        const ushort* utf16 = text.utf16();
        data.append(reinterpret_cast<const char*>(utf16), text.length() * 2);
        data.append('\0'); data.append('\0');
        return createFrame(id, data);
    }

    QByteArray createApicFrame(const QByteArray& imageData) {
        QByteArray data;
        data.append('\0');
        data.append("image/jpeg");
        data.append('\0');
        data.append((char)0x03);
        data.append('\0');
        data.append(imageData);
        return createFrame("APIC", data);
    }

    void InjectID3v2(const QString& filePath, const Track& track, const QByteArray& coverData) {
        QByteArray tagData;
        tagData.append(createTextFrame("TIT2", QString::fromStdString(track.title)));
        tagData.append(createTextFrame("TPE1", QString::fromStdString(track.artist)));
        if (!coverData.isEmpty()) {
            tagData.append(createApicFrame(coverData));
        }

        QByteArray header;
        header.append("ID3");
        header.append((char)0x03);
        header.append('\0');
        header.append('\0');
        header.append(makeSyncSafe(tagData.size()));

        QFile file(filePath);
        if (file.open(QIODevice::ReadWrite)) {
            QByteArray rawAudioData = file.readAll();
            file.seek(0);
            file.write(header + tagData);
            file.write(rawAudioData);
            file.close();
        }
    }
}

TrackDownloader::TrackDownloader(QObject* parent) : QObject(parent) {
    QDir().mkpath(PathManager::GetDownloadsDir());
}

void TrackDownloader::Download(const Track& track, const std::string& urlStr) {
    std::string safeName = track.GetSafeFilename();

    QString filePath = PathManager::GetDownloadFilePath(safeName, "mp3");
    QString aacPath = PathManager::GetDownloadFilePath(safeName, "aac");
    auto syncPrint = [](const std::string& text) {
        QMetaObject::invokeMethod(QCoreApplication::instance(), [text]() {
            std::lock_guard<std::mutex> lock(Logger::GetMutex());
            std::cout << "\r\033[2K\033[1A\r\033[2K" << text << "\n\n> ";
            std::cout.flush();
        }, Qt::QueuedConnection);
    };

    if (QFile::exists(filePath) || QFile::exists(aacPath)) {
        Logger::Log(LogLevel::INFO, "[Загрузчик] Трек уже скачан: " + safeName);
        return;
    }

    Logger::Log(LogLevel::INFO, "[Загрузчик] Старт загрузки (Universal Native): " + safeName);

    NetworkStreamer* streamer = new NetworkStreamer(this);
    std::shared_ptr<QFile> file = std::make_shared<QFile>(filePath);
    if (!file->open(QIODevice::WriteOnly)) {
        syncPrint("[Ошибка] Не удалось создать файл для сохранения: " + safeName);
        streamer->deleteLater();
        return;
    }

    auto isAacFormat = std::make_shared<bool>(false);

    // Вся та логика схлопнулась вот в этот элегантный объект!
    auto demuxer = std::make_shared<MpegTsDemuxer>([file, isAacFormat](const uint8_t* payload, size_t size, AudioFormat format) {
        if (format == AudioFormat::AAC_ADTS) {
            *isAacFormat = true;
        }
        file->write(reinterpret_cast<const char*>(payload), size);
    });

    connect(streamer, &NetworkStreamer::DataReceived, [demuxer](const QByteArray& data) {
        demuxer->ProcessBytes(reinterpret_cast<const uint8_t*>(data.constData()), data.size());
    });

    connect(streamer, &NetworkStreamer::DownloadFinished, this, [this, streamer, file, track, filePath, safeName, isAacFormat, syncPrint]() {
        file->close();
        streamer->deleteLater();

        QString finalPath = filePath;
        if (*isAacFormat) {
            finalPath = PathManager::GetDownloadFilePath(safeName, "aac");
            QFile::rename(filePath, finalPath);
        }

        if (track.coverUrl.empty()) {
            InjectID3v2(finalPath, track, QByteArray());
            syncPrint("[Загрузка] " + track.artist + " - " + track.title + " успешно сохранен (без обложки).");
            return;
        }

        QNetworkRequest request((QUrl(QString::fromStdString(track.coverUrl))));
        QNetworkReply* reply = m_manager.get(request);

        connect(reply, &QNetworkReply::finished, this, [reply, finalPath, track, syncPrint]() {
            QByteArray coverData;
            if (reply->error() == QNetworkReply::NoError) {
                coverData = reply->readAll();
            }
            reply->deleteLater();
            InjectID3v2(finalPath, track, coverData);

            syncPrint("[Загрузка] " + track.artist + " - " + track.title + " успешно сохранен.");
        });
    });

    streamer->StartDownload(urlStr);
}
```

## File: services/downloader/TrackDownloader.h
```c
#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QFile>
#include <string>
#include "../../models/Track.h"

class TrackDownloader : public QObject {
    Q_OBJECT
public:
    explicit TrackDownloader(QObject* parent = nullptr);
    void Download(const Track& track, const std::string& url);

private:
    QNetworkAccessManager m_manager;
};
```

## File: services/network/NetworkStreamer.cpp
```cpp
#include "NetworkStreamer.h"
#include "utils/logger/Logger.h"
#include <QNetworkRequest>
#include <QUrl>
#include <QStringList>
#include <QRegularExpression>
#include <QUrlQuery>

NetworkStreamer::NetworkStreamer(QObject* parent)
    : QObject(parent), m_manager(new QNetworkAccessManager(this)), m_reply(nullptr) {
    Logger::Log(LogLevel::INFO, "NetworkStreamer created.");
}

NetworkStreamer::~NetworkStreamer() {
    StopDownload();
    Logger::Log(LogLevel::INFO, "NetworkStreamer destroyed.");
}

void NetworkStreamer::StartDownload(const std::string& urlString) {
    Logger::Log(LogLevel::INFO, "Starting network stream from: " + urlString);
    m_pendingSeekPos = -1.0;

    if (m_reply) {
        StopDownload();
    }
    m_streamType = StreamType::DirectHttp;
    m_baseUrl = QUrl(QString::fromStdString(urlString));

    m_chunkQueue.clear();
    m_isEncrypted = false;
    m_mediaSequence = 0;
    m_aesKey.clear();
    m_aesIV.clear();
    m_currentChunkData.clear();

    QUrl url(QString::fromStdString(urlString));
    QNetworkRequest request(url);
    m_baseUrl = url;

    // Включаем поддержку редиректов
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    m_reply = m_manager->get(request);
    m_reply->setReadBufferSize(512 * 1024);
    m_isPaused = false;

    if (urlString.find(".m3u8") != std::string::npos) {
        connect(m_reply, &QNetworkReply::finished, this, [this, url]() {
            if (m_reply && m_reply->error() == QNetworkReply::NoError) {
                QString manifest = m_reply->readAll();
                ParseM3u8(manifest, url);

                m_reply->deleteLater();
                m_reply = nullptr;

                if (m_isEncrypted) {
                    DownloadKey();
                } else {
                    DownloadNextChunk();
                }
            } else {
                Logger::Log(LogLevel::ERROR, "Failed to download .m3u8 manifest");
                emit DownloadError("Failed to download .m3u8 manifest");
                if (m_reply) {
                    m_reply->deleteLater();
                    m_reply = nullptr;
                }
            }
        });
        connect(m_reply, &QNetworkReply::errorOccurred, this, &NetworkStreamer::OnErrorOccurred);
    }
    else {
        connect(m_reply, &QNetworkReply::readyRead, this, &NetworkStreamer::OnReadyRead);
        connect(m_reply, &QNetworkReply::finished, this, &NetworkStreamer::OnChunkFinished);
        connect(m_reply, &QNetworkReply::errorOccurred, this, &NetworkStreamer::OnErrorOccurred);
    }
}

void NetworkStreamer::StopDownload() {
    m_chunkQueue.clear();
    if (m_reply) {
        Logger::Log(LogLevel::INFO, "Aborting network stream.");
        
        m_reply->disconnect();

        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
}

void NetworkStreamer::ParseM3u8(const QString& manifestData, const QUrl& baseUrl) {
    QStringList lines = manifestData.split('\n');

    QRegularExpression keyRegex("#EXT-X-KEY:METHOD=AES-128,URI=\"([^\"]+)\"(?:,IV=(?:0x)?([0-9a-fA-F]+))?");
    QRegularExpression seqRegex("#EXT-X-MEDIA-SEQUENCE:(\\d+)");

    QRegularExpressionMatch seqMatch = seqRegex.match(manifestData);
    if (seqMatch.hasMatch()) {
        m_baseMediaSequence = seqMatch.captured(1).toULongLong();
        m_mediaSequence = m_baseMediaSequence; // Синхронизируем
    }

    QUrlQuery baseQuery(baseUrl);
    m_hlsChunks.clear();
    double currentDuration = 0.0;

    for (const QString& line : lines) {
        QString trimmed = line.trimmed();

        QRegularExpressionMatch match = keyRegex.match(trimmed);
        if (match.hasMatch()) {
            m_isEncrypted = true;
            QUrl keyUrlResolved = baseUrl.resolved(QUrl(match.captured(1)));
            // ... [слияние параметров ключа оставляем как есть] ...
            m_keyUrl = keyUrlResolved;
            if (!match.captured(2).isEmpty()) {
                m_aesIV = QByteArray::fromHex(match.captured(2).toUtf8());
            }
            continue;
        }

        // ДОБАВЛЕНО: Парсим длительность чанка
        if (trimmed.startsWith("#EXTINF:")) {
            QString valStr = trimmed.mid(8);
            int commaPos = valStr.indexOf(',');
            if (commaPos != -1) valStr = valStr.left(commaPos);
            currentDuration = valStr.toDouble();
            continue;
        }

        if (trimmed.isEmpty() || trimmed.startsWith("#")) continue;

        QUrl chunkUrl = baseUrl.resolved(QUrl(trimmed));

        // слияние параметров запроса для чанка
        QUrlQuery chunkQuery(chunkUrl);
        for (const auto& item : baseQuery.queryItems()) {
            if (!chunkQuery.hasQueryItem(item.first)) {
                chunkQuery.addQueryItem(item.first, item.second);
            }
        }

        chunkUrl.setQuery(chunkQuery);

        m_hlsChunks.push_back({chunkUrl, currentDuration});
        m_chunkQueue.enqueue(chunkUrl);
    }

    // ДОБАВЛЕНО: Умное определение типа потока
    if (m_hlsChunks.size() == 1 && m_isEncrypted) {
        m_streamType = StreamType::EncryptedMonolith;
    } else if (m_isEncrypted) {
        m_streamType = StreamType::HlsEncrypted;
    } else {
        m_streamType = StreamType::HlsUnencrypted;
    }

    Logger::Log(LogLevel::INFO, "Parsed " + std::to_string(m_hlsChunks.size()) + " audio chunks. Encrypted: " + (m_isEncrypted ? "Yes" : "No"));
    if (m_pendingSeekPos >= 0.0) {
        double pos = m_pendingSeekPos;
        m_pendingSeekPos = -1.0;
        SeekTo(pos);
    } else {
        DownloadNextChunk();
    }
}

void NetworkStreamer::DownloadKey() {
    Logger::Log(LogLevel::INFO, "Downloading AES-128 key...");
    QNetworkRequest request(m_keyUrl);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    m_reply = m_manager->get(request);

    connect(m_reply, &QNetworkReply::finished, this, [this]() {
        if (m_reply && m_reply->error() == QNetworkReply::NoError) {
            m_aesKey = m_reply->readAll();
            Logger::Log(LogLevel::INFO, "AES key downloaded successfully.");

            m_reply->deleteLater();
            m_reply = nullptr;

            DownloadNextChunk();
        } else {
             Logger::Log(LogLevel::ERROR, "Failed to download AES key.");
        }
    });
}

void NetworkStreamer::DownloadNextChunk() {
    if (m_chunkQueue.isEmpty()) {
        Logger::Log(LogLevel::INFO, "All chunks downloaded for current track.");
        emit DownloadFinished();
        return;
    }

    m_currentChunkData.clear();

    QUrl nextUrl = m_chunkQueue.dequeue();
    QNetworkRequest request(nextUrl);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    m_reply = m_manager->get(request);
    m_reply->setReadBufferSize(512 * 1024);

    connect(m_reply, &QNetworkReply::readyRead, this, &NetworkStreamer::OnReadyRead);
    connect(m_reply, &QNetworkReply::finished, this, &NetworkStreamer::OnChunkFinished);
    connect(m_reply, &QNetworkReply::errorOccurred, this, &NetworkStreamer::OnErrorOccurred);
}

void NetworkStreamer::OnReadyRead() {
    if (!m_reply || m_isPaused) return;
    m_currentChunkData.append(m_reply->readAll());
}

void NetworkStreamer::OnChunkFinished() {
    if (m_reply && m_reply->error() == QNetworkReply::NoError) {
        m_currentChunkData.append(m_reply->readAll());

        if (m_currentChunkData.size() > 0) {
            if (m_currentChunkData.startsWith("<!DOCTYPE") || m_currentChunkData.startsWith("<html")) {
                Logger::Log(LogLevel::ERROR, "VK returned an HTML error page instead of an audio chunk!");
            }
            else if (m_isEncrypted) {
                DecryptAndPushChunk();
            } else {
                emit DataReceived(m_currentChunkData);
            }
        }
    }

    if (m_reply) {
        m_reply->deleteLater();
        m_reply = nullptr;
    }

    DownloadNextChunk();
}

void NetworkStreamer::DecryptAndPushChunk() {
    if (m_currentChunkData.isEmpty()) return;

    //Если файл уже является чистым MPEG-TS
    uint8_t firstByte = static_cast<uint8_t>(m_currentChunkData[0]);
    if (firstByte == 0x47 && m_currentChunkData.size() % 188 == 0) {
        Logger::Log(LogLevel::INFO, "VK sent raw MPEG-TS despite manifest! Bypassing AES.");
        emit DataReceived(m_currentChunkData);
        return;
    }

    int id3Size = 0;
    if (m_currentChunkData.size() >= 10 && m_currentChunkData.startsWith("ID3")) {
        const uint8_t* d = reinterpret_cast<const uint8_t*>(m_currentChunkData.constData());
        id3Size = 10 + ((d[6] << 21) | (d[7] << 14) | (d[8] << 7) | d[9]);
        if (id3Size > m_currentChunkData.size()) id3Size = 0;
        Logger::Log(LogLevel::INFO, "Found unencrypted ID3 tag. Size: " + std::to_string(id3Size) + " bytes.");
    }

    int cipherSize = m_currentChunkData.size() - id3Size;
    if (cipherSize <= 0) {
        emit DataReceived(m_currentChunkData);
        return;
    }

    if (cipherSize % 16 != 0) {
        Logger::Log(LogLevel::WARNING, "Cipher size (" + std::to_string(cipherSize) + ") is not a multiple of 16. Padding...");
        int padding = 16 - (cipherSize % 16);
        m_currentChunkData.append(QByteArray(padding, 0));
        cipherSize += padding;
    }

    if (m_aesKey.size() != 16) {
        std::string err = "Invalid AES key size.";
        Logger::Log(LogLevel::ERROR, err);
        emit DownloadError(err);
        return;
    }

    QByteArray currentIV = m_aesIV;
    if (currentIV.isEmpty()) {
        currentIV = QByteArray(16, 0);
        uint64_t seq = m_mediaSequence;
        for (int i = 15; i >= 8; --i) {
            currentIV[i] = seq & 0xFF;
            seq >>= 8;
        }
    }
    m_mediaSequence++;

    struct AES_ctx ctx;
    AES_init_ctx_iv(&ctx, reinterpret_cast<const uint8_t*>(m_aesKey.constData()),
                          reinterpret_cast<const uint8_t*>(currentIV.constData()));

    uint8_t* cipherDataPtr = reinterpret_cast<uint8_t*>(m_currentChunkData.data()) + id3Size;
    AES_CBC_decrypt_buffer(&ctx, cipherDataPtr, cipherSize);

    emit DataReceived(m_currentChunkData);
}

void NetworkStreamer::PauseDownload() {
    if (!m_isPaused) {
        m_isPaused = true;
        Logger::Log(LogLevel::INFO, "High Watermark reached. Pausing network read.");
    }
}

void NetworkStreamer::ResumeDownload() {
    if (m_isPaused) {
        m_isPaused = false;
        Logger::Log(LogLevel::INFO, "Low Watermark reached. Resuming network read.");
        if (m_reply && m_reply->bytesAvailable() > 0) {
            OnReadyRead();
        }
    }
}

void NetworkStreamer::OnErrorOccurred(QNetworkReply::NetworkError code) {
    if (m_reply) {
        std::string err = m_reply->errorString().toStdString();
        Logger::Log(LogLevel::ERROR, "Network error (" + std::to_string(code) + "): " + err);
        emit DownloadError(err);
    }
}

void NetworkStreamer::SeekTo(double targetSeconds) {
    if (m_hlsChunks.isEmpty() && m_totalFileSize == 0) {
        m_pendingSeekPos = targetSeconds;
        Logger::Log(LogLevel::INFO, "NetworkStreamer: Manifest not loaded yet. Seek deferred to " + std::to_string(targetSeconds) + "s.");
        return;
    }
    StopDownload();
    m_currentChunkData.clear();

    if (m_streamType == StreamType::DirectHttp || m_streamType == StreamType::HlsUnencrypted) {
        // --- СТРАТЕГИЯ 1: HTTP Range Request ---
        std::string typeStr = (m_streamType == StreamType::DirectHttp) ? "Direct HTTP" : "HLS Unencrypted";

        if (m_totalFileSize > 0 && m_trackDurationSec > 0) {
            qint64 targetByte = static_cast<qint64>((targetSeconds / m_trackDurationSec) * m_totalFileSize);

            Logger::Log(LogLevel::INFO, "NetworkStreamer: [Strategy -> HTTP Range] Type: " + typeStr + ". Requesting bytes=" + std::to_string(targetByte) + "-");

            QNetworkRequest request(m_baseUrl);
            request.setRawHeader("Range", QString("bytes=%1-").arg(targetByte).toUtf8());
            request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

            m_reply = m_manager->get(request);
            connect(m_reply, &QNetworkReply::readyRead, this, &NetworkStreamer::OnReadyRead);
            connect(m_reply, &QNetworkReply::finished, this, &NetworkStreamer::OnChunkFinished);
            connect(m_reply, &QNetworkReply::errorOccurred, this, &NetworkStreamer::OnErrorOccurred);
        } else {
            Logger::Log(LogLevel::WARNING, "NetworkStreamer: [Strategy -> HTTP Range] Failed! Missing total file size or duration.");
        }
    }
    else if (m_streamType == StreamType::HlsEncrypted) {
        // --- СТРАТЕГИЯ 2: Прыжок по зашифрованным чанкам HLS ---
        double accumulatedTime = 0.0;
        int targetIndex = 0;

        for (int i = 0; i < m_hlsChunks.size(); ++i) {
            if (accumulatedTime + m_hlsChunks[i].durationSec > targetSeconds) {
                targetIndex = i;
                break;
            }
            accumulatedTime += m_hlsChunks[i].durationSec;
        }

        // Жесткая синхронизация AES IV
        m_mediaSequence = m_baseMediaSequence + targetIndex;

        // Пересобираем очередь чанков, отбрасывая прослушанные
        m_chunkQueue.clear();
        for (int i = targetIndex; i < m_hlsChunks.size(); ++i) {
            m_chunkQueue.enqueue(m_hlsChunks[i].url);
        }

        Logger::Log(LogLevel::INFO, "NetworkStreamer: [Strategy -> HLS Chunk] Jumping to chunk " + std::to_string(targetIndex) + ". Sequence reset to " + std::to_string(m_mediaSequence));
        DownloadNextChunk();
    }
    else if (m_streamType == StreamType::EncryptedMonolith) {
        // --- СТРАТЕГИЯ 3: Mmap для зашифрованных монолитов (В планах) ---
        Logger::Log(LogLevel::WARNING, "NetworkStreamer: [Strategy -> RAM File Mapping] Native mmap seek for monoliths is pending implementation.");
    }
    else {
        Logger::Log(LogLevel::ERROR, "NetworkStreamer: Unknown stream type! Cannot seek.");
    }
}
```

## File: services/network/NetworkStreamer.h
```c
#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QByteArray>
#include <QQueue>
#include <QUrl>
#include <QStringList>
#include <string>

extern "C" {
#include "aes.h"
}

enum class StreamType {
    Unknown,
    DirectHttp,         // Открытые MP3
    HlsUnencrypted,     // Обычный m3u8
    HlsEncrypted,       // Зашифрованный m3u8
    EncryptedMonolith   // Зашифрованный m3u8, 1 чанк
};

struct HlsChunk {
    QUrl url;
    double durationSec;
};

class NetworkStreamer : public QObject {
    Q_OBJECT
public:
    explicit NetworkStreamer(QObject* parent = nullptr);
    ~NetworkStreamer();

    void StartDownload(const std::string& url);
    void StopDownload();
    void SeekTo(double targetSeconds);

signals:
    void DataReceived(const QByteArray& data);
    void DownloadFinished();
    void DownloadError(const std::string& errorString);

public slots:
    void PauseDownload();
    void ResumeDownload();

private slots:
    void OnReadyRead();
    void OnChunkFinished();
    void OnErrorOccurred(QNetworkReply::NetworkError code);

private:
    void ParseM3u8(const QString& manifestData, const QUrl& baseUrl);
    void DownloadNextChunk();
    void DownloadKey();          // Скачивание AES-ключа
    void DecryptAndPushChunk();  // Расшифровка и передача данных

    bool m_isPaused = false;
    QNetworkAccessManager* m_manager;
    QNetworkReply* m_reply;

    // --- ПЕРЕМЕННЫЕ РОУТИНГА ---
    StreamType m_streamType = StreamType::Unknown;
    QUrl m_baseUrl;
    qint64 m_totalFileSize = 0;
    int m_trackDurationSec = 0;

    // --- ПЕРЕМЕННЫЕ ДЛЯ HLS AES-128 ---
    QQueue<QUrl> m_chunkQueue;
    QVector<HlsChunk> m_hlsChunks;
    uint64_t m_baseMediaSequence = 0;
    bool m_isEncrypted = false;
    uint64_t m_mediaSequence = 0;
    QUrl m_keyUrl;
    QByteArray m_aesKey;
    QByteArray m_aesIV;
    QByteArray m_currentChunkData; // Буфер для накопления целого чанка
    double m_pendingSeekPos = -1.0;
};
```

## File: ui/console/commands/CommandDispatcher.cpp
```cpp
#include "CommandDispatcher.h"
#include "core/audio/IAudioEngine.h"
#include "core/playlist/PlaylistManager.h"
#include "services/database/DatabaseManager.h"
#include "services/downloader/TrackDownloader.h"
#include "core/lyrics/LyricsFetcher.h"
#include "core/api/IAudioProvider.h"
#include "utils/logger/Logger.h"
#include "utils/path/PathManager.h"
#include "core/shazam/ShazamSignatureBuilder.h"

#include <QMetaObject>
#include <QFile>
#include <QDesktopServices>
#include <QUrl>
#include <cmath>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUuid>
#include <QtConcurrent>


CommandDispatcher::CommandDispatcher(IAudioEngine& audio, PlaylistManager& playlist,
                                     DatabaseManager& db, TrackDownloader& downloader, LyricsFetcher& lyrics)
    : m_audio(audio), m_playlist(playlist), m_dbManager(db), m_downloader(downloader), m_lyricsFetcher(lyrics) {
    RegisterCommands();
}

void CommandDispatcher::SetCurrentProvider(IAudioProvider* provider) {
    m_currentProvider = provider;
}

void CommandDispatcher::SetPrintCallback(std::function<void(const std::string&)> printCb) {
    m_printCb = printCb;
}

void CommandDispatcher::Print(const std::string& msg) {
    if (m_printCb) m_printCb(msg);
}

void CommandDispatcher::Dispatch(const std::string& input) {
    if (input.empty()) return;

    std::string cmd;
    std::string arg;
    size_t spacePos = input.find(' ');

    if (spacePos != std::string::npos) {
        cmd = input.substr(0, spacePos);
        arg = input.substr(spacePos + 1);
    } else {
        cmd = input;
    }

    for (char& c : cmd) {
        c = std::tolower(static_cast<unsigned char>(c));
    }

    if (arg.length() >= 2 && arg.front() == '"' && arg.back() == '"') {
        arg = arg.substr(1, arg.length() - 2);
    }

    auto it = m_commands.find(cmd);
    if (it != m_commands.end()) {
        it->second(arg);
    } else {
        Print("[Ошибка] Неизвестная команда. Введи 'h' для справки.\n\n> ");
    }
}

void CommandDispatcher::RegisterCommands() {
    // --- Управление воспроизведением ---
    m_commands["p"] = [this](const std::string&) {
        QMetaObject::invokeMethod(QCoreApplication::instance(), [this]() {
            if (m_audio.IsPlaying()) m_audio.Pause(); else m_audio.Resume();
        }, Qt::QueuedConnection);
    };

    m_commands["n"] = [this](const std::string&) {
        QMetaObject::invokeMethod(QCoreApplication::instance(), [this]() { m_playlist.Next(); }, Qt::QueuedConnection);
    };

    m_commands["b"] = [this](const std::string&) {
        QMetaObject::invokeMethod(QCoreApplication::instance(), [this]() { m_playlist.Previous(); }, Qt::QueuedConnection);
    };

    m_commands["+"] = [this](const std::string&) {
        QMetaObject::invokeMethod(QCoreApplication::instance(), [this]() { m_audio.SetVolume(m_audio.GetVolume() + 0.1f); }, Qt::QueuedConnection);
    };

    m_commands["-"] = [this](const std::string&) {
        QMetaObject::invokeMethod(QCoreApplication::instance(), [this]() { m_audio.SetVolume(m_audio.GetVolume() - 0.1f); }, Qt::QueuedConnection);
    };

    m_commands["v"] = [this](const std::string& arg) {
        try {
            int vol = std::stoi(arg);
            if (vol < 0) vol = 0;
            if (vol > 100) vol = 100;
            QMetaObject::invokeMethod(QCoreApplication::instance(), [this, vol]() {
                m_audio.SetVolume(vol / 100.0f);
            }, Qt::QueuedConnection);
            Print("[Громкость] Установлена громкость: " + std::to_string(vol) + "%\n\n> ");
        } catch (...) {
            Print("[Ошибка] Неверный формат. Используй: v <число от 0 до 100>\n\n> ");
        }
    };

    m_commands["cv"] = [this](const std::string&) {
        int vol = static_cast<int>(std::round(m_audio.GetVolume() * 100));
        Print("[Громкость] Текущая громкость: " + std::to_string(vol) + "%\n\n> ");
    };

    m_commands["seek"] = [this](const std::string& arg) {
        try {
            double pos = std::stod(arg);
            QMetaObject::invokeMethod(QCoreApplication::instance(), [this, pos]() {
                m_audio.SetPositionSeconds(pos);
            }, Qt::QueuedConnection);
            Print("[Перемотка] Переход на " + std::to_string(static_cast<int>(pos)) + " сек.\n\n> ");
        } catch (...) {
            Print("[Ошибка] Неверный формат. Используй: seek <секунды>\n\n> ");
        }
    };

    m_commands["r"] = [this](const std::string&) {
        QMetaObject::invokeMethod(QCoreApplication::instance(), [this]() { m_playlist.ToggleRepeat(); }, Qt::QueuedConnection);
    };

    // --- Навигация и Плейлист ---
    m_commands["j"] = [this](const std::string& arg) {
        try {
            int idx = std::stoi(arg);
            QMetaObject::invokeMethod(QCoreApplication::instance(), [this, idx]() {
                m_playlist.JumpToQueueIndex(idx - 1);
            }, Qt::QueuedConnection);
            Print("[Плейлист] Переход к треку " + std::to_string(idx) + "\n\n> ");
        } catch (...) {
            Print("[Ошибка] Неверный номер трека.\n\n> ");
        }
    };

    m_commands["tl"] = [this](const std::string&) {
        QMetaObject::invokeMethod(QCoreApplication::instance(), [this]() {
            m_dbManager.ExportQueueToTxt(m_playlist.GetQueueTracks(), "playlist.txt", m_playlist.IsShuffle());
        }, Qt::QueuedConnection);
        Print("[Инфо] Текущий плейлист успешно экспортирован в playlist.txt\n\n> ");
    };

    m_commands["sh"] = [this](const std::string&) {
        QMetaObject::invokeMethod(QCoreApplication::instance(), [this]() {
            m_playlist.SetShuffle(true);
            m_playlist.JumpToQueueIndex(0);
            std::string src = QSettings(PathManager::GetConfigPath(), QSettings::IniFormat).value("General/source", "VK").toString().toStdString();
            m_dbManager.SaveQueue(m_playlist.GetQueueTracks(), src, m_playlist.IsShuffle());
            m_dbManager.ExportQueueToTxt(m_playlist.GetQueueTracks(), "playlist.txt", m_playlist.IsShuffle());
        }, Qt::QueuedConnection);
        Print("[Плейлист] Режим: Перемешивание (Shuffle). Стартуем случайный трек!\n\n> ");
    };

    m_commands["st"] = [this](const std::string&) {
        QMetaObject::invokeMethod(QCoreApplication::instance(), [this]() {
            m_playlist.SetShuffle(false);
            std::string src = QSettings(PathManager::GetConfigPath(), QSettings::IniFormat).value("General/source", "VK").toString().toStdString();
            m_dbManager.SaveQueue(m_playlist.GetQueueTracks(), src, m_playlist.IsShuffle());
            m_dbManager.ExportQueueToTxt(m_playlist.GetQueueTracks(), "playlist.txt", m_playlist.IsShuffle());
        }, Qt::QueuedConnection);
        Print("[Плейлист] Режим: Стандартный порядок\n\n> ");
    };

    auto resetHandler = [this](const std::string&) {
        QMetaObject::invokeMethod(QCoreApplication::instance(), [this]() {
            m_playlist.SetShuffle(false);
            m_playlist.JumpTo(0);
            std::string src = QSettings(PathManager::GetConfigPath(), QSettings::IniFormat).value("General/source", "VK").toString().toStdString();
            m_dbManager.SaveQueue(m_playlist.GetQueueTracks(), src, m_playlist.IsShuffle());
            m_dbManager.ExportQueueToTxt(m_playlist.GetQueueTracks(), "playlist.txt", m_playlist.IsShuffle());
        }, Qt::QueuedConnection);
        Print("[Сессия] Плейлист сброшен: стандартный порядок, 1-й трек.\n\n> ");
    };
    m_commands["rs"] = resetHandler;
    m_commands["reset"] = resetHandler;

    m_commands["search"] = [this](const std::string& arg) {
        if (arg.empty()) {
            Print("[Ошибка] Пустой запрос. Используй: search <название или автор>\n\n> ");
            return;
        }

        std::string searchArtist = arg;
        std::string searchTitle = arg;
        bool isSplit = false;

        size_t dashPos = arg.find("-");
        if (dashPos != std::string::npos) {
            searchArtist = arg.substr(0, dashPos);
            searchTitle = arg.substr(dashPos + 1);

            auto trim = [](std::string& s) {
                s.erase(0, s.find_first_not_of(" \t"));
                s.erase(s.find_last_not_of(" \t") + 1);
            };
            trim(searchArtist);
            trim(searchTitle);
            isSplit = true;
        }

        QString qArtist = QString::fromStdString(searchArtist).trimmed();
        QString qTitle = QString::fromStdString(searchTitle).trimmed();
        QString qFull = QString::fromStdString(arg).trimmed();

        std::vector<Track> queue = m_playlist.GetQueueTracks();
        std::string s(50, '-');
        std::string res = "[Поиск] Результаты по запросу \"" + qFull.toStdString() + "\":\n" + s + "\n";

        int matchCount = 0;
        for (size_t i = 0; i < queue.size(); ++i) {
            QString trackArtist = QString::fromStdString(queue[i].artist);
            QString trackTitle = QString::fromStdString(queue[i].title);

            bool match = false;
            if (isSplit) {
                match = trackArtist.contains(qArtist, Qt::CaseInsensitive) && trackTitle.contains(qTitle, Qt::CaseInsensitive);
            } else {
                match = trackArtist.contains(qFull, Qt::CaseInsensitive) || trackTitle.contains(qFull, Qt::CaseInsensitive);
            }

            if (match) {
                res += "[" + std::to_string(i + 1) + "]. " + queue[i].artist + " - " + queue[i].title + " [" + queue[i].GetFormattedDuration() + "]\n";
                matchCount++;
                if (matchCount >= 20) {
                    res += "... Показаны первые 20 совпадений.\n";
                    break;
                }
            }
        }

        if (matchCount == 0) res += "Ничего не найдено.\n";
        res += s + "\n\n> ";
        Print(res);
    };

    m_commands["shazam"] = [this](const std::string& arg) {
        if (arg.empty()) {
            Print("[Shazam] Использование: shazam <путь_к_файлу> или shazam mic\n\n> ");
            return;
        }

        std::string argStr = arg;
        QMetaObject::invokeMethod(QCoreApplication::instance(), [this, argStr]() {
            if (argStr == "mic") {
                Print("[Shazam] Инициализация микрофона (в разработке)...\n\n> ");
            } else {
                Print("[Shazam] Анализ файла в фоне. Можете продолжать работу...\n> ");

                QtConcurrent::run([this, argStr]() {
                    QByteArray rawSignature = ShazamSignatureBuilder::BuildFromFile(argStr);
                    if (rawSignature.isEmpty()) {
                        Print("\n[Shazam] Ошибка генерации подписи.\n> ");
                        return;
                    }

                    QMetaObject::invokeMethod(QCoreApplication::instance(), [this, rawSignature]() {
                        QString base64Sig = QString(rawSignature.toBase64());

                        Logger::Log(LogLevel::INFO, "\n=== ДАМП BASE64 ОТ C++ ===");
                        Logger::Log(LogLevel::INFO, base64Sig.toStdString());
                        Logger::Log(LogLevel::INFO, "===============================\n");

                        QJsonObject sigObj;
                        sigObj["uri"] = "data:audio/vnd.shazam.sig;base64," + base64Sig;
                        sigObj["samplems"] = 12000;

                        QJsonObject rootObj;
                        rootObj["signature"] = sigObj;
                        QByteArray jsonPayload = QJsonDocument(rootObj).toJson(QJsonDocument::Compact);

                        QNetworkAccessManager* manager = new QNetworkAccessManager();
                        QString uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
                        QUrl url("https://amp.shazam.com/discovery/v5/ru/RU/android/-/tag/" + uuid + "/" + uuid);

                        QNetworkRequest request(url);
                        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
                        request.setRawHeader("User-Agent", "Shazam Android/13.7.0");

                        QNetworkReply* reply = manager->post(request, jsonPayload);

                        QObject::connect(reply, &QNetworkReply::sslErrors, [this](const QList<QSslError>& errors) {
                            for (const auto& err : errors) {
                                Logger::Log(LogLevel::ERROR, "Shazam SSL Error: " + err.errorString().toStdString());
                            }
                        });

                        QObject::connect(reply, &QNetworkReply::finished, [this, reply, manager]() {
                            int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                            QUrl redirectUrl = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();

                            Logger::Log(LogLevel::INFO, "Shazam: HTTP Status " + std::to_string(statusCode));

                            if (!redirectUrl.isEmpty()) {
                                Logger::Log(LogLevel::WARNING, "Shazam: Требуется редирект на " + redirectUrl.toString().toStdString());
                            }

                            if (reply->error() == QNetworkReply::NoError) {
                                QByteArray rawBody = reply->readAll();
                                Logger::Log(LogLevel::INFO, "Shazam: Сырой ответ: " + rawBody.left(300).toStdString());

                                QJsonDocument json = QJsonDocument::fromJson(rawBody);
                                QJsonObject trackObj = json.object()["track"].toObject();

                                if (trackObj.isEmpty()) {
                                    Print("\n[Shazam] Трек не распознан :( Возможно, его нет в базе.\n> ");
                                } else {
                                    QString title = trackObj["title"].toString();
                                    QString artist = trackObj["subtitle"].toString();
                                    Print("\n[Shazam] 🎵 УСПЕХ! Найдено: " + artist.toStdString() + " - " + title.toStdString() + "\n> ");
                                }
                            } else {
                                Logger::Log(LogLevel::ERROR, "Shazam Network Error: " + reply->errorString().toStdString());
                                Logger::Log(LogLevel::ERROR, "Shazam Error Body: " + reply->readAll().toStdString());
                            }
                            reply->deleteLater();
                            manager->deleteLater();
                        });
                    }, Qt::QueuedConnection);
                });
            }
        }, Qt::QueuedConnection);
    };

    // --- Сеть, Загрузки и Тексты ---
    auto dlRmHandler = [this](const std::string& cmd, const std::string& arg) {
        Track targetTrack;
        bool isValid = false;

        if (arg.empty()) {
            targetTrack = m_playlist.GetCurrentTrack();
            isValid = true;
        } else {
            try {
                int idx = std::stoi(arg) - 1;
                std::vector<Track> queue = m_playlist.GetQueueTracks();
                if (idx >= 0 && idx < queue.size()) {
                    targetTrack = queue[idx];
                    isValid = true;
                }
            } catch(...) {}
        }

        if (isValid && !targetTrack.id.empty()) {
            QString pathMp3 = PathManager::GetDownloadFilePath(targetTrack.GetSafeFilename(), "mp3");
            QString pathAac = PathManager::GetDownloadFilePath(targetTrack.GetSafeFilename(), "aac");

            if (cmd == "dl") {
                if (QFile::exists(pathMp3) || QFile::exists(pathAac)) {
                    Print("[Загрузка] Трек уже скачан.\n\n> ");
                } else {
                    if (!m_currentProvider) {
                        Print("[Ошибка] Нет активного онлайн-источника для скачивания.\n\n> ");
                        return;
                    }
                    Print("[Загрузка] Получение ссылки для " + targetTrack.title + "...\n\n> ");
                    QMetaObject::invokeMethod(QCoreApplication::instance(), [this, targetTrack]() {
                        m_currentProvider->FetchTrackUrl(targetTrack.id, [this, targetTrack](const std::string& url, bool err) {
                            if (!err && !url.empty()) {
                                m_downloader.Download(targetTrack, url);
                            } else {
                                Logger::Log(LogLevel::WARNING, "Failed to get URL for download.");
                            }
                        });
                    }, Qt::QueuedConnection);
                }
            } else if (cmd == "rm") {
                if (QFile::exists(pathMp3) || QFile::exists(pathAac)) {
                    QFile::remove(pathMp3);
                    QFile::remove(pathAac);
                    Print("[Кэш] Удален: " + targetTrack.artist + " - " + targetTrack.title + "\n\n> ");
                } else {
                    Print("[Кэш] Трек не был скачан.\n\n> ");
                }
            }
        } else {
            Print("[Ошибка] Не удалось найти трек.\n\n> ");
        }
    };
    m_commands["dl"] = [=](const std::string& arg) { dlRmHandler("dl", arg); };
    m_commands["rm"] = [=](const std::string& arg) { dlRmHandler("rm", arg); };

    auto lyricsHandler = [this](const std::string& arg) {
        bool isNewFile = (arg.find("new") != std::string::npos);
        Track currentTrack = m_playlist.GetCurrentTrack();

        auto showLyricsFile = [this, currentTrack, isNewFile](const std::string& text) {
            if (text.empty()) {
                Print("[Ошибка] Не удалось загрузить текст (См. logs/app.log).\n\n> ");
                return;
            }

            QString filePath = PathManager::GetLyricsFilePath(currentTrack.artist, currentTrack.title, isNewFile);
            QFile file(filePath);
            if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                file.write(QByteArray::fromStdString(text));
                file.close();
            }

            QFileInfo fileInfo(filePath);
            QDesktopServices::openUrl(QUrl::fromLocalFile(fileInfo.absoluteFilePath()));
            Print("[Текст] Открыт файл: " + filePath.toStdString() + "\n\n> ");
        };

        std::string text = currentTrack.lyrics;
        if (text.empty()) {
            Print("[Текст] Поиск текста...\n\n> ");
            QMetaObject::invokeMethod(QCoreApplication::instance(), [this, currentTrack, showLyricsFile]() {
                m_lyricsFetcher.FetchLyrics(currentTrack.artist, currentTrack.title, [this, currentTrack, showLyricsFile](const std::string& fetchedText) {
                    if (!fetchedText.empty()) {
                        m_dbManager.UpdateTrackLyrics(currentTrack.id, fetchedText);
                    }
                    showLyricsFile(fetchedText);
                });
            }, Qt::QueuedConnection);
        } else {
            showLyricsFile(text);
        }
    };
    m_commands["ly"] = lyricsHandler;
    m_commands["lyrics"] = lyricsHandler;

    // --- Настройки системы ---
    m_commands["source"] = [this](const std::string&) {
        Print("\n=== Выбор источника ===\n1 - ВКонтакте\n2 - Spotify\n3 - SoundCloud\n4 - Yandex\n5 - Оффлайн режим\n\nВведите номер: ");
        if (OnSourceChangeRequested) OnSourceChangeRequested("SELECT");
    };

    m_commands["vis"] = [this](const std::string&) {
        if (OnVisualizerToggled) OnVisualizerToggled();
    };

    m_commands["mode"] = [this](const std::string& arg) {
        try {
            int mode = std::stoi(arg);
            if (mode == 0 || mode == 1) {
                bool isGapless = (mode == 1);
                if (OnGaplessModeChanged) {
                    QMetaObject::invokeMethod(QCoreApplication::instance(), [this, isGapless]() {
                        OnGaplessModeChanged(isGapless);
                    }, Qt::QueuedConnection);
                }
                std::string modeStr = isGapless ? "плавный (gapless)" : "стандартный";
                Print("[Режим] Установлен " + modeStr + " переход.\n\n> ");
            } else {
                Print("[Ошибка] Используй: mode 0 (стандарт) или mode 1 (плавный)\n\n> ");
            }
        } catch (...) {
            Print("[Ошибка] Неверный формат. Используй: mode 0 или mode 1\n\n> ");
        }
    };

    m_commands["logout"] = [this](const std::string& arg) {
        if (arg == "vk" || arg == "spotify" || arg == "sc" || arg == "yandex" || arg == "all") {
            if (OnLogoutRequested) {
                QMetaObject::invokeMethod(QCoreApplication::instance(), [this, arg]() {
                    OnLogoutRequested(arg);
                }, Qt::QueuedConnection);
            }
        } else {
            Print("[Ошибка] Укажите сервис: logout vk | logout spotify | logout sc | logout yandex | logout all\n\n> ");
        }
    };

    m_commands["i"] = [this](const std::string&) {
        QMetaObject::invokeMethod(QCoreApplication::instance(), [this]() {
            Track current = m_playlist.GetCurrentTrack();
            std::string info = "[Инфо] Артист: " + current.artist + "\n"
                             + "[Инфо] Название: " + current.title + "\n"
                             + "[Инфо] ID: " + current.id + "\n"
                             + "[Инфо] Обложка: " + (current.coverUrl.empty() ? "НЕТ ОБЛОЖКИ" : current.coverUrl) + "\n\n> ";
            Print(info);
        }, Qt::QueuedConnection);
    };

    m_commands["q"] = [this](const std::string&) {
        if (OnQuitRequested) OnQuitRequested();
    };

    m_commands["h"] = [this](const std::string&) {
        std::string s(50, '*');
        std::string helpText = "\n" + s + "\n"
                  + " [P] Play/Pause\n [N] Next\n [B] Prev\n"
                  + " [+] Vol Up\n [-] Vol Down\n [v <num>] Set Volume\n"
                  + " [st] Standard Order\n [sh] Shuffle\n [R] Repeat Mode\n"
                  + " [J <num>] Jump to track\n [cv] Current volume\n"
                  + " [rs] Reset Session\n"
                  + " [mode <0/1>] 0 - Standard, 1 - Gapless transition\n"
                  + " [search <text>] Search tracks in playlist\n"
                  + " [shazam <file/mic>] Recognize track via Shazam\n"
                  + " [ly] Show lyrics for current track\n"
                  + " [logout <service>] Logout from choosen service\n"
                  + " [source] Select audio source\n"
                  + " [tl] Export tracklist to TXT\n"
                  + " [dl] / [dl <num>] Download track\n"
                  + " [rm] / [rm <num>] Delete downloaded track\n"
                  + " [vis] Toggle visualizer\n"
                  + " [Q] Quit\n"
                  + s + "\n\n> ";
        Print(helpText);
    };
}
```

## File: ui/console/commands/CommandDispatcher.h
```c
#pragma once
#include <string>
#include <map>
#include <functional>

class IAudioEngine;
class PlaylistManager;
class DatabaseManager;
class TrackDownloader;
class LyricsFetcher;
class IAudioProvider;

class CommandDispatcher {
public:
    CommandDispatcher(IAudioEngine& audio, PlaylistManager& playlist,
                      DatabaseManager& dbManager, TrackDownloader& downloader,
                      LyricsFetcher& lyricsFetcher);

    void SetCurrentProvider(IAudioProvider* provider);
    void SetPrintCallback(std::function<void(const std::string&)> printCb);

    std::function<void(const std::string&)> OnSourceChangeRequested;
    std::function<void(bool)> OnGaplessModeChanged;
    std::function<void()> OnVisualizerToggled;
    std::function<void()> OnQuitRequested;
    std::function<void(const std::string&)> OnLogoutRequested;

    void Dispatch(const std::string& input);

private:
    void RegisterCommands();
    void Print(const std::string& msg);

    IAudioEngine& m_audio;
    PlaylistManager& m_playlist;
    DatabaseManager& m_dbManager;
    TrackDownloader& m_downloader;
    LyricsFetcher& m_lyricsFetcher;
    IAudioProvider* m_currentProvider = nullptr;

    std::function<void(const std::string&)> m_printCb;

    std::map<std::string, std::function<void(const std::string&)>> m_commands;
};
```

## File: ui/console/core/ConsoleController.cpp
```cpp
#ifdef _WIN32
#include <windows.h>
#endif

#include <QDir>
#include <QFile>
#include <QDesktopServices>
#include <QUrl>
#include <QFileInfo>
#include <QRegularExpression>
#include <iostream>
#include <string>
#include <cctype>
#include <QCoreApplication>
#include <QMetaObject>
#include <cmath>
#include <QSettings>

#include "ConsoleController.h"
#include "core/audio/IAudioEngine.h"
#include "core/playlist/PlaylistManager.h"
#include "core/auth/oauth/OAuthManager.h"
#include "services/database/DatabaseManager.h"
#include "utils/logger/Logger.h"
#include "utils/path/PathManager.h"
#include "core/api/IAudioProvider.h"
#include "services/downloader/TrackDownloader.h"
#include "core/lyrics/LyricsFetcher.h"
#include "ui/console/commands/CommandDispatcher.h"
#include "ui/console/view/ConsoleRenderer.h"


ConsoleController::ConsoleController(
    IAudioEngine& audio, PlaylistManager& playlist, OAuthManager& authManager,
    DatabaseManager& dbManager, TrackDownloader& downloader, LyricsFetcher& lyricsFetcher
) : m_audio(audio), m_playlist(playlist), m_authManager(authManager),
    m_dbManager(dbManager), m_downloader(downloader), m_lyricsFetcher(lyricsFetcher),
    m_currentState(ConsoleState::COMMAND_MODE), m_isRunning(false) {

    m_dispatcher = std::make_unique<CommandDispatcher>(audio, playlist, dbManager, downloader, lyricsFetcher);
    m_renderer = std::make_unique<ConsoleRenderer>(audio, playlist);

    m_dispatcher->SetPrintCallback([this](const std::string& text) {
        if (!m_isRunning || !QCoreApplication::instance()) return;
        QMetaObject::invokeMethod(QCoreApplication::instance(), [text]() {
            std::lock_guard<std::mutex> lock(Logger::GetMutex());
            std::cout << "\r\033[2K\033[1A\r\033[2K" << text;
            std::cout.flush();
        }, Qt::QueuedConnection);
    });

    QSettings settings(PathManager::GetConfigPath(), QSettings::IniFormat);
    m_renderer->SetVisualizerEnabled(settings.value("Ui/ShowVisualizer", true).toBool());

    m_dispatcher->OnVisualizerToggled = [this]() {
        bool newState = !m_renderer->IsVisualizerEnabled();
        m_renderer->SetVisualizerEnabled(newState);
        QSettings s(PathManager::GetConfigPath(), QSettings::IniFormat);
        s.setValue("Ui/ShowVisualizer", newState);
        s.sync();
    };

    m_dispatcher->OnSourceChangeRequested = [this](const std::string&) {
        m_currentState = ConsoleState::SELECT_SOURCE;
    };

    m_dispatcher->OnLogoutRequested = [this](const std::string& service) {
        auto processLogout = [this](const QString& svcName, const std::string& internalName) {
            m_authManager.ClearSavedToken(svcName);

            std::string msg = "[Выход] Токен для " + internalName + " успешно удален.\n\n> ";
            QMetaObject::invokeMethod(QCoreApplication::instance(), [msg]() {
                std::lock_guard<std::mutex> lock(Logger::GetMutex());
                std::cout << "\r\033[2K\033[1A\r\033[2K" << msg;
                std::cout.flush();
            }, Qt::QueuedConnection);
        };

        bool logoutVk = (service == "vk" || service == "all");
        bool logoutSpotify = (service == "spotify" || service == "all");
        bool logoutSc = (service == "sc" || service == "all");
        bool logoutYandex = (service == "yandex" || service == "all");

        if (logoutVk) processLogout("VK", "ВКонтакте");
        if (logoutSpotify) processLogout("Spotify", "Spotify");
        if (logoutSc) processLogout("SoundCloud", "SoundCloud");
        if (logoutYandex) processLogout("Yandex", "Yandex");

        QSettings settings(PathManager::GetConfigPath(), QSettings::IniFormat);
        QString currentSource = settings.value("General/source", "").toString();

        bool activeLoggedOut = false;
        if (currentSource == "VK" && logoutVk) activeLoggedOut = true;
        if (currentSource == "Spotify" && logoutSpotify) activeLoggedOut = true;
        if (currentSource == "SoundCloud" && logoutSc) activeLoggedOut = true;
        if (currentSource == "Yandex" && logoutYandex) activeLoggedOut = true;

        if (activeLoggedOut && m_currentProvider) {
            m_audio.Pause();
            m_playlist.Clear();
            emit SourceChanged("Offline");
        }
    };

    m_dispatcher->OnGaplessModeChanged = [this](bool isGapless) {
        if (OnGaplessModeChanged) OnGaplessModeChanged(isGapless);
    };

    m_dispatcher->OnQuitRequested = [this]() {
        emit QuitRequested();
        m_isRunning = false;
    };
}


ConsoleController::~ConsoleController() {
    Stop();
}

void ConsoleController::SetState(ConsoleState state) {
    m_currentState = state;
}

void ConsoleController::Start() {
    if (m_isRunning) return;

    // Прячем курсор (ANSI escape code)
    std::cout << "\033[?25l";
    std::cout.flush();

    m_isRunning = true;
    m_inputThread = std::thread(&ConsoleController::InputLoop, this);
    m_uiThread = std::thread(&ConsoleController::UiLoop, this);
}

void ConsoleController::Stop() {
    m_isRunning = false;

    std::cout << "\033[?25h";
    std::cout.flush();

#ifdef _WIN32
    HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
    if (hStdin != INVALID_HANDLE_VALUE) {
        CancelIoEx(hStdin, NULL);
    }
#endif

    if (m_inputThread.joinable()) m_inputThread.detach();
    if (m_uiThread.joinable()) m_uiThread.join();
}

void ConsoleController::InputLoop() {
    std::string rawInput;

    auto syncPrint = [this](const std::string& text) {
        if (!m_isRunning || !QCoreApplication::instance()) return;

        QMetaObject::invokeMethod(QCoreApplication::instance(), [text]() {
            std::lock_guard<std::mutex> lock(Logger::GetMutex());
            std::cout << "\r\033[2K\033[1A\r\033[2K" << text;
            std::cout.flush();
        }, Qt::QueuedConnection);
    };

    while (m_isRunning) {
        if (!std::getline(std::cin, rawInput)) {
            std::cin.clear();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        // === РЕЖИМ ОЖИДАНИЯ ТОКЕНА ===
        if (m_currentState == ConsoleState::WAITING_TOKEN_URL) {
            size_t start = rawInput.find_first_not_of(" \t\r\n");
            if (start == std::string::npos) {
                std::cout << "> ";
                std::cout.flush();
                continue;
            }
            std::string input = rawInput.substr(start, rawInput.find_last_not_of(" \t\r\n") - start + 1);

            if (input == "offline") {
                m_currentState = ConsoleState::COMMAND_MODE;
                emit OfflineModeRequested();
                continue;
            }

            QString urlStr = QString::fromStdString(input);
            std::cout << "Обработка ссылки...\n\n> ";
            std::cout.flush();

            QMetaObject::invokeMethod(&m_authManager, [&, urlStr]() {
                m_authManager.onUrlIntercepted(urlStr);
            }, Qt::QueuedConnection);

            m_currentState = ConsoleState::COMMAND_MODE;
            continue;
        }

        // === РЕЖИМ ВЫБОРА ИСТОЧНИКА ===
        if (m_currentState == ConsoleState::SELECT_SOURCE) {
            size_t start = rawInput.find_first_not_of(" \t\r\n");
            if (start == std::string::npos) {
                std::cout << "> "; std::cout.flush(); continue;
            }
            std::string input = rawInput.substr(start, rawInput.find_last_not_of(" \t\r\n") - start + 1);

            if (input == "1") {
                emit SourceChanged("VK");
            } else if (input == "2") {
                emit SourceChanged("Spotify");
            } else if (input == "3") {
                emit SourceChanged("SoundCloud");
            } else if (input == "4") {
                emit SourceChanged("Yandex");
            } else if (input == "5") {
                emit SourceChanged("Offline");
            } else {
                syncPrint("[Ошибка] Неверный выбор. Введите 1-5:\n> ");
                continue;
            }
            m_currentState = ConsoleState::COMMAND_MODE;
            continue;
        }

        // === РЕЖИМ ПЛЕЕРА (COMMAND_MODE) ===
        std::cout << "\033[1A\r\033[2K";
        std::cout.flush();

        size_t start = rawInput.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) {
            std::cout << "> ";
            std::cout.flush();
            continue;
        }
        std::string input = rawInput.substr(start, rawInput.find_last_not_of(" \t\r\n") - start + 1);

        if (m_currentState == ConsoleState::COMMAND_MODE) {
            m_dispatcher->Dispatch(input);
            continue;
        }
    }
}

void ConsoleController::UiLoop() {
    while (m_isRunning) {
        if (m_currentState == ConsoleState::WAITING_TOKEN_URL || m_currentState == ConsoleState::SELECT_SOURCE) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            continue;
        }

        if (m_currentState == ConsoleState::COMMAND_MODE) {
            m_renderer->Render();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(m_renderer->IsVisualizerEnabled() ? 50 : 500));
    }
}

void ConsoleController::SetCurrentProvider(IAudioProvider* provider) {
    m_currentProvider = provider;
    if (m_dispatcher) m_dispatcher->SetCurrentProvider(provider);
}
```

## File: ui/console/core/ConsoleController.h
```c
#pragma once

#include <memory>

#include <QObject>
#include <QString>
#include <atomic>
#include <thread>
#include <functional>

class IAudioEngine;
class PlaylistManager;
class OAuthManager;
class DatabaseManager;
class IAudioProvider;
class TrackDownloader;
class LyricsFetcher;
class CommandDispatcher;
class ConsoleRenderer;

enum class ConsoleState {
    COMMAND_MODE,
    WAITING_TOKEN_URL,
    SELECT_SOURCE
};

class ConsoleController : public QObject {
    Q_OBJECT
public:
    ConsoleController(
        IAudioEngine& audio,
        PlaylistManager& playlist,
        OAuthManager& authManager,
        DatabaseManager& dbManager,
        TrackDownloader& downloader,
        LyricsFetcher& lyricsFetcher);
    ~ConsoleController();

    void Start();
    void Stop();
    void SetState(ConsoleState state);
    ConsoleState GetState() const { return m_currentState; }

    void SetCurrentProvider(IAudioProvider* provider); // Сеттер для переключения источника

    std::function<void(bool)> OnGaplessModeChanged;

    signals:
        void QuitRequested();
        void OfflineModeRequested();
        void SourceChanged(const std::string& sourceName);

private:
    void InputLoop();
    void UiLoop();

    IAudioEngine& m_audio;
    PlaylistManager& m_playlist;
    OAuthManager& m_authManager;
    DatabaseManager& m_dbManager;
    TrackDownloader& m_downloader;
    LyricsFetcher& m_lyricsFetcher;
    IAudioProvider* m_currentProvider = nullptr;
    std::unique_ptr<CommandDispatcher> m_dispatcher;
    std::unique_ptr<ConsoleRenderer> m_renderer;
    std::atomic<ConsoleState> m_currentState;
    std::atomic<bool> m_isRunning;
    std::thread m_inputThread;
    std::thread m_uiThread;
};
```

## File: ui/console/view/ConsoleRenderer.cpp
```cpp
#include "ConsoleRenderer.h"
#include "core/audio/IAudioEngine.h"
#include "core/playlist/PlaylistManager.h"
#include "utils/logger/Logger.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <iostream>
#include <cmath>
#include <vector>

ConsoleRenderer::ConsoleRenderer(IAudioEngine& audio, PlaylistManager& playlist)
    : m_audio(audio), m_playlist(playlist) {}

void ConsoleRenderer::SetVisualizerEnabled(bool enabled) {
    m_showVisualizer = enabled;
}

void ConsoleRenderer::Render() {
    if (!m_audio.IsPlaying()) return;

    double current = m_audio.GetPositionSeconds();
    if (current < 0.0) current = 0.0;
    int currentSecInt = static_cast<int>(current);

    double total = m_audio.GetLengthSeconds();
    if (total <= 0.0) {
        total = static_cast<double>(m_playlist.GetCurrentTrack().duration);
    }

    if (total > 0.0) {
        int percent = static_cast<int>((current / total) * 100.0);
        if (percent > 100) percent = 100;
        if (percent < 0) percent = 0;

        int curMin = currentSecInt / 60;
        int curSec = currentSecInt % 60;
        int totMin = static_cast<int>(total) / 60;
        int totSec = static_cast<int>(total) % 60;

        int barLength = 40;
        int filled = static_cast<int>((current / total) * barLength);
        if (filled > barLength) filled = barLength;
        if (filled < 0) filled = 0;

        std::string bar = "[";
        for (int i = 0; i < barLength; ++i) {
            if (i < filled) bar += "\xE2\x96\x88";
            else bar += "-";
        }
        bar += "]";

        std::string spectrum = " [";
        const int numBands = 16;
        if (m_showVisualizer) {
            const char* blocks[] = {" ", "▂", "▃", "▄", "▅", "▆", "▇", "█"};
            std::vector<float> fft = m_audio.GetSpectrumData();
            if (!fft.empty()) {
                for (int i = 0; i < numBands; ++i) {
                    float peak = 0.0f;
                    int startBin = static_cast<int>(std::pow(2.0, i * 7.0 / numBands));
                    int endBin = static_cast<int>(std::pow(2.0, (i + 1) * 7.0 / numBands));
                    
                    if (endBin <= startBin) endBin = startBin + 1;

                    if (endBin > static_cast<int>(fft.size())) {
                        endBin = static_cast<int>(fft.size());
                    }

                    for (int b = startBin; b < endBin; ++b) {
                        if (fft[b] > peak) peak = fft[b];
                    }

                    int level = static_cast<int>(std::sqrt(peak) * 18.0f);
                    if (level < 0) level = 0;
                    if (level > 7) level = 7;

                    spectrum += blocks[level];
                }
            } else {
                spectrum += std::string(numBands, ' ');
            }
        } else {
            spectrum += std::string(numBands, ' ');
        }
        spectrum += "]";

        Track currentTrack = m_playlist.GetCurrentTrack();
        std::vector<Track> queue = m_playlist.GetQueueTracks();
        int trackIndex = 0;

        for (size_t i = 0; i < queue.size(); ++i) {
            if (queue[i].id == currentTrack.id) {
                trackIndex = i + 1;
                break;
            }
        }

        std::string trackName = std::to_string(trackIndex) + ". " + currentTrack.artist + " - " + currentTrack.title;

        char buffer[512];
        snprintf(buffer, sizeof(buffer), "%s | %02d:%02d / %02d:%02d %s %d%%%s",
                 trackName.c_str(), curMin, curSec, totMin, totSec, bar.c_str(), percent, spectrum.c_str());
        std::string currentStr = buffer;

        if (currentStr != m_lastPrintedStr) {
            m_lastPrintedStr = currentStr;
            QMetaObject::invokeMethod(QCoreApplication::instance(), [currentStr]() {
                std::lock_guard<std::mutex> lock(Logger::GetMutex());
                printf("\033[s\033[1A\r\033[2K%s\033[u", currentStr.c_str());
                fflush(stdout);
            }, Qt::QueuedConnection);
        }
    }
}
```

## File: ui/console/view/ConsoleRenderer.h
```c
#pragma once
#include <string>

class IAudioEngine;
class PlaylistManager;

class ConsoleRenderer {
public:
    ConsoleRenderer(IAudioEngine& audio, PlaylistManager& playlist);

    void Render();
    
    void SetVisualizerEnabled(bool enabled);
    bool IsVisualizerEnabled() const { return m_showVisualizer; }

private:
    IAudioEngine& m_audio;
    PlaylistManager& m_playlist;
    
    bool m_showVisualizer = true;
    std::string m_lastPrintedStr = "";
};
```

## File: utils/buffer/RingBuffer.cpp
```cpp
#include "RingBuffer.h"
#include <algorithm>
#include <cstring>

RingBuffer::RingBuffer(size_t size) 
    : m_capacity(size), m_readPos(0), m_writePos(0), m_availableBytes(0) {
    m_buffer.resize(size);
}

size_t RingBuffer::GetAvailableRead() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_availableBytes;
}

size_t RingBuffer::GetAvailableWrite() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_capacity - m_availableBytes;
}

size_t RingBuffer::Write(const uint8_t* data, size_t sizeToWrite) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    size_t availableWrite = m_capacity - m_availableBytes;
    size_t actualWrite = std::min(sizeToWrite, availableWrite);
    
    if (actualWrite == 0) return 0; // Буфер переполнен

    // Пишем до конца буфера
    size_t firstPart = std::min(actualWrite, m_capacity - m_writePos);
    std::memcpy(&m_buffer[m_writePos], data, firstPart);
    
    // Если нужно, загибаемся в начало буфера
    if (firstPart < actualWrite) {
        std::memcpy(&m_buffer[0], data + firstPart, actualWrite - firstPart);
    }

    m_writePos = (m_writePos + actualWrite) % m_capacity;
    m_availableBytes += actualWrite;

    return actualWrite;
}

size_t RingBuffer::Read(uint8_t* data, size_t sizeToRead) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    size_t actualRead = std::min(sizeToRead, m_availableBytes);
    
    if (actualRead == 0) return 0; // Буфер пуст (сеть не успевает)

    size_t firstPart = std::min(actualRead, m_capacity - m_readPos);
    std::memcpy(data, &m_buffer[m_readPos], firstPart);
    
    if (firstPart < actualRead) {
        std::memcpy(data + firstPart, &m_buffer[0], actualRead - firstPart);
    }

    m_readPos = (m_readPos + actualRead) % m_capacity;
    m_availableBytes -= actualRead;

    return actualRead;
}

// Пустой конструктор для инициализации
RingBuffer::RingBuffer()
    : m_capacity(0), m_readPos(0), m_writePos(0), m_availableBytes(0) {
}

// Отложенная инициализация
void RingBuffer::Init(size_t size) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_capacity = size;
    m_buffer.resize(size);
    m_readPos = 0;
    m_writePos = 0;
    m_availableBytes = 0;
}

// Очистка буфера
void RingBuffer::Clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_readPos = 0;
    m_writePos = 0;
    m_availableBytes = 0;
}
```

## File: utils/buffer/RingBuffer.h
```c
#pragma once
#include <vector>
#include <mutex>
#include <cstdint>

class RingBuffer {
public:
    explicit RingBuffer(size_t size);
    // пустой конструктор
    RingBuffer();
    
    // передача данных
    size_t Write(const uint8_t* data, size_t sizeToWrite);
    
    // сбор данных
    size_t Read(uint8_t* data, size_t sizeToRead);
    
    // Узнать, сколько байт доступно для чтения
    size_t GetAvailableRead() const;
    
    // Узнать, сколько свободного места осталось
    size_t GetAvailableWrite() const;

    // методы инициализации
    void Init(size_t size);

    // методы очистки
    void Clear();

private:
    std::vector<uint8_t> m_buffer;
    size_t m_capacity;
    size_t m_readPos;
    size_t m_writePos;
    size_t m_availableBytes;
    
    mutable std::mutex m_mutex;
};
```

## File: utils/env/EnvParser.cpp
```cpp
#include "EnvParser.h"
#include "utils/logger/Logger.h"
#include <QFile>
#include <QTextStream>

QMap<QString, QString> EnvParser::Parse(const QString& filePath) {
    QMap<QString, QString> env;
    QFile file(filePath);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        while (!in.atEnd()) {
            QString line = in.readLine().trimmed();
            if (line.isEmpty() || line.startsWith("#")) continue;
            int idx = line.indexOf('=');
            if (idx != -1) {
                QString key = line.left(idx).trimmed();
                QString value = line.mid(idx + 1).trimmed();
                if (value.startsWith('"') && value.endsWith('"')) {
                    value = value.mid(1, value.length() - 2);
                }
                env[key] = value;
            }
        }
    } else {
        Logger::Log(LogLevel::WARNING, "EnvParser: .env file not found at " + filePath.toStdString());
    }
    return env;
}
```

## File: utils/env/EnvParser.h
```c
#pragma once
#include <QString>
#include <QMap>

class EnvParser {
public:
    static QMap<QString, QString> Parse(const QString& filePath);
};
```

## File: utils/logger/Logger.cpp
```cpp
#define _CRT_SECURE_NO_WARNINGS
#include "Logger.h"
#include "utils/path/PathManager.h"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace fs = std::filesystem;

static std::ofstream logFile;
std::mutex Logger::s_mutex;

void Logger::Init() {
    PathManager::Init();
    std::string logPath = PathManager::GetLogFilePath().toStdString();

    // Открываем файл
    logFile.open(logPath, std::ios::out | std::ios::trunc);

    if (!logFile.is_open()) {
        std::cerr << "[ERROR] Failed to open log file at " << logPath << std::endl;
    }
}

void Logger::Log(LogLevel level, const std::string& message) {
#ifdef NDEBUG
    if (level == LogLevel::DEBUG) return;
#endif
    // Получаем текущее время
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);

    // Форматируем время в строку [HH:MM:SS]
    std::stringstream ssTime;
    ssTime << std::put_time(std::localtime(&in_time_t), "%H:%M:%S");
    std::string timeStr = "[" + ssTime.str() + "] ";

    std::string levelStr;
    switch (level) {
        case LogLevel::INFO:    levelStr = "[INFO] "; break;
        case LogLevel::WARNING: levelStr = "[WARN] "; break;
        case LogLevel::ERROR:   levelStr = "[ERROR] "; break;
    }

    std::string fullMessage = timeStr + levelStr + message;
    std::string clearUi = "\r\033[2K\033[1A\r\033[2K";

    std::lock_guard<std::mutex> lock(s_mutex);

    if (level == LogLevel::ERROR) {
        std::cerr << clearUi << fullMessage << "\n\n> ";
        std::cerr.flush();
    } else {
        std::cout << clearUi << fullMessage << "\n\n> ";
        std::cout.flush();
    }

    if (logFile.is_open()) {
        logFile << fullMessage << std::endl;
        logFile.flush();
    }
}

void Logger::Close() {
    if (logFile.is_open()) {
        logFile.close();
    }
}
```

## File: utils/logger/Logger.h
```c
#pragma once
#include <string>
#include <mutex>

#ifdef _WIN32
#undef ERROR
#endif

enum class LogLevel { DEBUG, INFO, WARNING, ERROR };

class Logger {
public:
    static void Init();
    static void Log(LogLevel level, const std::string& message);
    static void Close();
    static std::mutex& GetMutex() { return s_mutex; }
private:
    static std::mutex s_mutex;
};
```

## File: utils/parser/MpegTsDemuxer.cpp
```cpp
#include "MpegTsDemuxer.h"
#include "utils/logger/Logger.h"
#include <algorithm>

MpegTsDemuxer::MpegTsDemuxer(PayloadCallback callback) : m_callback(callback) {}

void MpegTsDemuxer::Reset() {
    m_buffer.clear();
    m_audioPid = 0x1FFF;
    m_id3BytesToSkip = 0;
    m_format = AudioFormat::Unknown;
    m_isTsStreamDetermined = false;
    m_isTsStream = false;
}

AudioFormat MpegTsDemuxer::DetectAudioFormat(const uint8_t* data, size_t size) {
    if (size < 2 || data[0] != 0xFF) return AudioFormat::Unknown;
    uint8_t b1 = data[1];
    // ADTS (AAC)
    if ((b1 & 0xF0) == 0xF0) {
        if (((b1 >> 1) & 0x03) == 0x00) return AudioFormat::AAC_ADTS;
    }
    // MP3
    if ((b1 & 0xE0) == 0xE0) {
        if (((b1 >> 3) & 0x03) != 0x01 && ((b1 >> 1) & 0x03) != 0x00) return AudioFormat::MP3;
    }
    return AudioFormat::Unknown;
}

void MpegTsDemuxer::ProcessBytes(const uint8_t* data, size_t size) {
    if (!data || size == 0) return;

    if (!m_isTsStreamDetermined) {
        m_isTsStream = (data[0] == 0x47);
        m_isTsStreamDetermined = true;
    }

    if (!m_isTsStream) {
        if (m_format == AudioFormat::Unknown) m_format = DetectAudioFormat(data, size);
        m_callback(data, size, m_format);
        return;
    }

    m_buffer.insert(m_buffer.end(), data, data + size);
    size_t bytesConsumed = 0;

    while (m_buffer.size() - bytesConsumed >= 188) {
        const uint8_t* tsPacket = m_buffer.data() + bytesConsumed;

        if (tsPacket[0] != 0x47) {
            auto startIt = m_buffer.begin() + bytesConsumed;
            auto it = std::find(startIt, m_buffer.end(), 0x47);
            bytesConsumed = std::distance(m_buffer.begin(), it);
            continue;
        }

        uint16_t pid = ((tsPacket[1] & 0x1F) << 8) | tsPacket[2];
        uint8_t pusi = (tsPacket[1] & 0x40) >> 6;
        uint8_t afc  = (tsPacket[3] & 0x30) >> 4;

        size_t payloadOffset = 4;
        if (afc == 2 || afc == 3) payloadOffset += 1 + tsPacket[4];

        if ((afc == 1 || afc == 3) && payloadOffset < 188) {
            size_t payloadSize = 188 - payloadOffset;
            const uint8_t* payload = tsPacket + payloadOffset;

            if (pusi == 1 && payloadSize >= 9 && payload[0] == 0x00 && payload[1] == 0x00 && payload[2] == 0x01) {
                uint8_t streamId = payload[3];

                if (m_audioPid == 0x1FFF && streamId >= 0xC0 && streamId <= 0xDF) {
                    m_audioPid = pid;
                    Logger::Log(LogLevel::INFO, "Demuxer: Locked to AUDIO PID -> " + std::to_string(pid));
                }

                if (pid == m_audioPid) {
                    uint8_t pesHeaderLen = payload[8];
                    size_t pesTotalOffset = 9 + pesHeaderLen;
                    if (pesTotalOffset < payloadSize) {
                        payload += pesTotalOffset;
                        payloadSize -= pesTotalOffset;
                    } else {
                        payloadSize = 0;
                    }

                    if (payloadSize >= 10 && payload[0] == 'I' && payload[1] == 'D' && payload[2] == '3') {
                        uint32_t tagSize = ((payload[6] & 0x7F) << 21) | ((payload[7] & 0x7F) << 14) |
                                           ((payload[8] & 0x7F) << 7)  | (payload[9] & 0x7F);
                        m_id3BytesToSkip = 10 + tagSize;
                    }
                }
            }

            if (pid == m_audioPid && m_id3BytesToSkip > 0 && payloadSize > 0) {
                size_t toSkip = std::min(m_id3BytesToSkip, payloadSize);
                payload += toSkip;
                payloadSize -= toSkip;
                m_id3BytesToSkip -= toSkip;
            }

            if (payloadSize > 0 && pid == m_audioPid) {
                if (m_format == AudioFormat::Unknown) m_format = DetectAudioFormat(payload, payloadSize);
                m_callback(payload, payloadSize, m_format);
            }
        }
        bytesConsumed += 188;
    }

    if (bytesConsumed > 0) {
        m_buffer.erase(m_buffer.begin(), m_buffer.begin() + bytesConsumed);
    }
}
```

## File: utils/parser/MpegTsDemuxer.h
```c
#pragma once
#include <vector>
#include <cstdint>
#include <functional>

enum class AudioFormat { Unknown, AAC_ADTS, MP3 };

class MpegTsDemuxer {
public:
    using PayloadCallback = std::function<void(const uint8_t* payload, size_t payloadSize, AudioFormat format)>;

    explicit MpegTsDemuxer(PayloadCallback callback);

    void ProcessBytes(const uint8_t* data, size_t size);
    void Reset();

private:
    AudioFormat DetectAudioFormat(const uint8_t* data, size_t size);

    PayloadCallback m_callback;
    std::vector<uint8_t> m_buffer;
    uint16_t m_audioPid = 0x1FFF;
    size_t m_id3BytesToSkip = 0;
    AudioFormat m_format = AudioFormat::Unknown;
    
    bool m_isTsStreamDetermined = false;
    bool m_isTsStream = false;
};
```

## File: utils/path/PathManager.cpp
```cpp
#include "PathManager.h"
#include <QStandardPaths>
#include <QDir>
#include <QRegularExpression>
#include <QCoreApplication>

QString PathManager::s_appDataDir;
QString PathManager::s_downloadsDir;
QString PathManager::s_lyricsDir;
QString PathManager::s_logsDir;
bool PathManager::s_initialized = false;

void PathManager::Init() {
    if (s_initialized) return;

#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    s_appDataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (s_appDataDir.isEmpty()) {
        s_appDataDir = QDir::currentPath() + "/VKAudioPlayer";
    }
#else
    // На десктопе сохраняем портативность: работаем в текущей директории приложения
    s_appDataDir = QDir::currentPath();
#endif

    QDir appDir(s_appDataDir);
    if (!appDir.exists()) {
        appDir.mkpath(".");
    }

    s_downloadsDir = s_appDataDir + "/downloads";
    s_lyricsDir = s_appDataDir + "/lyrics";
    s_logsDir = s_appDataDir + "/logs";

    QDir().mkpath(s_downloadsDir);
    QDir().mkpath(s_lyricsDir);
    QDir().mkpath(s_logsDir);

    s_initialized = true;
}

QString PathManager::GetAppDataDir() {
    if (!s_initialized) Init();
    return s_appDataDir;
}

QString PathManager::GetDownloadsDir() {
    if (!s_initialized) Init();
    return s_downloadsDir;
}

QString PathManager::GetLyricsDir() {
    if (!s_initialized) Init();
    return s_lyricsDir;
}

QString PathManager::GetLogsDir() {
    if (!s_initialized) Init();
    return s_logsDir;
}

QString PathManager::GetDbPath() {
    return GetAppDataDir() + "/player_data.db";
}

QString PathManager::GetConfigPath() {
    return GetAppDataDir() + "/config.ini";
}

QString PathManager::GetPlaylistExportPath(const QString& filename) {
    QString fname = filename.isEmpty() ? "playlist.txt" : filename;
    return GetAppDataDir() + "/" + fname;
}

QString PathManager::GetLogFilePath() {
    return GetLogsDir() + "/app.log";
}

QString PathManager::GetDownloadFilePath(const std::string& safeFilename, const QString& ext) {
    QString extension = ext.startsWith('.') ? ext : ("." + ext);
    return GetDownloadsDir() + "/" + QString::fromStdString(safeFilename) + extension;
}

QString PathManager::GetLyricsFilePath(const std::string& artist, const std::string& title, bool isNewFile) {
    if (isNewFile) {
        QString safeArtist = QString::fromStdString(artist).replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");
        QString safeTitle = QString::fromStdString(title).replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");
        return GetLyricsDir() + "/" + safeArtist + " - " + safeTitle + ".txt";
    }
    return GetLyricsDir() + "/lyric.txt";
}
```

## File: utils/path/PathManager.h
```c
#pragma once
#include <QString>
#include <string>

class PathManager {
public:
    static void Init();

    static QString GetAppDataDir();
    static QString GetDownloadsDir();
    static QString GetLyricsDir();
    static QString GetLogsDir();

    static QString GetDbPath();
    static QString GetConfigPath();
    static QString GetPlaylistExportPath(const QString& filename = QString());
    static QString GetLogFilePath();

    static QString GetDownloadFilePath(const std::string& safeFilename, const QString& ext);
    static QString GetLyricsFilePath(const std::string& artist, const std::string& title, bool isNewFile = false);

private:
    static QString s_appDataDir;
    static QString s_downloadsDir;
    static QString s_lyricsDir;
    static QString s_logsDir;
    static bool s_initialized;
};
```

## File: main.cpp
```cpp
#include <iostream>
#include <QGuiApplication>
#include <QSettings>
#include <string>
#include <QtWebView>

#ifdef _WIN32
#include <windows.h>
#endif

#include "core/audio/miniaudio/MiniaudioEngine.h"
#include "core/lyrics/LyricsFetcher.h"
#include "core/playlist/PlaylistManager.h"
#include "core/auth/router/SourceRouter.h"
#include "core/api/vk/VkClient.h"
#include "core/api/spotify/SpotifyClient.h"
#include "core/api/soundcloud/SoundCloudClient.h"
#include "services/database/DatabaseManager.h"
#include "services/downloader/TrackDownloader.h"
#include "services/network/NetworkStreamer.h"
#include "ui/console/core/ConsoleController.h"
#include "utils/logger/Logger.h"
#include "utils/path/PathManager.h"
#include "utils/env/EnvParser.h"
#include "core/audio/playback/PlaybackController.h"

int main(int argc, char *argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    GetConsoleMode(hOut, &dwMode);
    dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hOut, dwMode);
#endif

    QGuiApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    QtWebView::initialize();

    QCoreApplication::setOrganizationName("VKAudioTeam");
    QCoreApplication::setApplicationName("VKAudioPlayer");

    PathManager::Init();
    Logger::Init();

    QMap<QString, QString> envVars = EnvParser::Parse(".env");

    Logger::Log(LogLevel::INFO, "--- VK Audio Player Started ---");
    Logger::Log(LogLevel::INFO, "DB Path: " + PathManager::GetDbPath().toStdString());

    QSettings settings(PathManager::GetConfigPath(), QSettings::IniFormat);
    if (!settings.contains("Audio/CrossfadeDurationMs")) {
        settings.setValue("Audio/CrossfadeDurationMs", 3000);
        settings.sync();
    }

    std::string activeSource = settings.value("General/source", "VK").toString().toStdString();
    bool crossfadeEnabled = settings.value("Audio/CrossfadePlayback", false).toBool();
    bool isShuffle = settings.value("Session/Shuffle", false).toBool();
    bool autoPlay = settings.value("Session/AutoPlay", false).toBool();
    float savedVolume = settings.value("Session/Volume", 1.0f).toFloat();
    int savedTrackIndex = settings.value("Session/CurrentTrackIndex", -1).toInt();
    double savedPosition = settings.value("Session/Position", 0.0).toDouble();
    int savedRepeatMode = settings.value("Session/Repeat", 1).toInt();

    DatabaseManager dbManager;
    if (!dbManager.Init()) return -1;

    MiniaudioEngine audio;
    if (!audio.Init()) return -1;

    PlaylistManager playlist;
    playlist.SetRepeatMode(savedRepeatMode);
    TrackDownloader downloader;
    LyricsFetcher lyricsFetcher;
    NetworkStreamer streamer;

    PlaybackController playbackCtrl(audio, playlist, streamer);
    SourceRouter router(envVars);

    // Заглушка для конструктора консоли (позже нужно убрать authManager оттуда)
    ConsoleController console(audio, playlist, *router.GetAuthManager(), dbManager, downloader, lyricsFetcher);

    audio.SetVolume(savedVolume);
    bool isPlaybackStarted = false;
    int vkSyncIndex = 0;

    playbackCtrl.SetCrossfadeEnabled(crossfadeEnabled);
    playbackCtrl.SetSavedPosition(savedPosition);

    // --- Связи компонентов (Внутренняя логика плеера) ---
    QObject::connect(&streamer, &NetworkStreamer::DataReceived, [&](const QByteArray& data) {
        audio.PushNetworkData(reinterpret_cast<const uint8_t*>(data.constData()), data.size());
    });

    audio.OnNetworkSeekRequested = [&](double targetSeconds) {
        QMetaObject::invokeMethod(QCoreApplication::instance(), [&streamer, targetSeconds]() {
            streamer.SeekTo(targetSeconds);
        }, Qt::QueuedConnection);
    };

    QObject::connect(&console, &ConsoleController::QuitRequested, &app, &QCoreApplication::quit);

    audio.OnTrackFinished = [&]() { playbackCtrl.HandleTrackFinished(); };
    audio.OnTrackNearEnd = [&]() { playbackCtrl.HandleTrackNearEnd(); };
    audio.OnPlaybackError = [&](const std::string& err) {
        Logger::Log(LogLevel::ERROR, "Playback failed: " + err + ". Skipping to next track...");
        playlist.Next();
    };
    playlist.OnTrackRequested = [&](Track track) { playbackCtrl.AttemptPlay(track); };

    console.OnGaplessModeChanged = [&](bool isCrossfade) {
        settings.setValue("Audio/CrossfadePlayback", isCrossfade);
        settings.sync();
        playbackCtrl.SetCrossfadeEnabled(isCrossfade);
        Logger::Log(LogLevel::INFO, std::string("Main: Crossfade transition set to ") + (isCrossfade ? "ON" : "OFF"));
    };

    // --- Функции инициализации ---
    auto initPlaylistAndStart = [&](bool isOnline) {
        if (isPlaybackStarted) return;

        if (!playlist.HasTracks()) {
            std::vector<Track> cachedTracks = dbManager.LoadTracks(activeSource);
            if (isOnline) {
                for (const auto& t : cachedTracks) playlist.AddTrack(t);
            } else {
                for (const auto& t : cachedTracks) {
                    QString mp3Path = PathManager::GetDownloadFilePath(t.GetSafeFilename(), "mp3");
                    QString aacPath = PathManager::GetDownloadFilePath(t.GetSafeFilename(), "aac");
                    if (QFile::exists(mp3Path) || QFile::exists(aacPath)) {
                        playlist.AddTrack(t);
                    }
                }
            }

            if (isShuffle) {
                std::vector<std::string> savedQueue = dbManager.LoadQueueIds(activeSource, true);
                if (!savedQueue.empty()) {
                    playlist.RestoreShuffleQueue(savedQueue);
                } else {
                    playlist.SetShuffle(true);
                }
            }
        }

        if (playlist.HasTracks()) {
            std::cout << "\r\033[2K=== ПЛЕЕР ГОТОВ К РАБОТЕ ===\nРежим очереди: " << (isShuffle ? "Шафл" : "Стандартный")
                      << "\nАвтостарт: " << (autoPlay ? "ВКЛ" : "ВЫКЛ") << "\n"
                      << (isOnline ? "" : "[ОФФЛАЙН] Загружены только скачанные треки.\n")
                      << "Введите 'h' для вывода списка команд\n\n> ";
            std::cout.flush();

            isPlaybackStarted = true;

            if (savedTrackIndex >= 0 && savedTrackIndex < playlist.GetAllTracks().size()) {
                playlist.JumpTo(savedTrackIndex);
                savedTrackIndex = -1;
            } else {
                playlist.OnTrackRequested(playlist.GetCurrentTrack());
            }

            if (!autoPlay) audio.Pause();
        } else {
            std::cout << "\n[Оффлайн] Нет скачанных треков. Плеер пуст.\n> ";
            std::cout.flush();
        }
    };

    auto onAudioFetched = [&](const std::vector<Track>& tracks) {
        bool hasNewTracks = false;
        auto allTracks = playlist.GetAllTracks();
        for (const auto& track : tracks) {
            bool exists = false;
            for (const auto& cached : allTracks) {
                if (cached.id == track.id) { exists = true; break; }
            }
            if (!exists) {
                playlist.InsertTrack(vkSyncIndex, track);
                hasNewTracks = true;
            }
            vkSyncIndex++;
        }
        dbManager.SaveTracks(tracks);
        if (!isPlaybackStarted) {
            initPlaylistAndStart(true);
        }
        if (!isPlaybackStarted || hasNewTracks) {
            dbManager.SaveQueue(playlist.GetAllTracks(), activeSource, false);
            dbManager.SaveQueue(playlist.GetQueueTracks(), activeSource, playlist.IsShuffle());
            dbManager.ExportQueueToTxt(playlist.GetQueueTracks(), "playlist.txt", playlist.IsShuffle());
        }
    };

    auto onFinishedFetching = [&]() {
        Logger::Log(LogLevel::INFO, "=== ФОНОВАЯ СИНХРОНИЗАЦИЯ ЗАВЕРШЕНА ===");
        dbManager.SaveQueue(playlist.GetAllTracks(), activeSource, false);
        dbManager.SaveQueue(playlist.GetQueueTracks(), activeSource, playlist.IsShuffle());
        dbManager.ExportQueueToTxt(playlist.GetQueueTracks(), "playlist.txt", playlist.IsShuffle());
    };

    // --- Связи с роутером ---
    QObject::connect(&console, &ConsoleController::OfflineModeRequested, &app, [&]() {
        initPlaylistAndStart(false);
    }, Qt::QueuedConnection);

    QObject::connect(&console, &ConsoleController::SourceChanged, &app, [&](const std::string& source) {
        router.SwitchSource(source);
    }, Qt::QueuedConnection);

    QObject::connect(&router, &SourceRouter::SourceChanged, [&](const std::string& newSource) {
        activeSource = newSource;
        playbackCtrl.ClearState();
        isPlaybackStarted = false;
        playlist.Clear();

        settings.setValue("General/source", QString::fromStdString(newSource));
        settings.sync();

        console.SetCurrentProvider(router.GetCurrentProvider());
        playbackCtrl.SetCurrentProvider(router.GetCurrentProvider());
    });

    QObject::connect(&router, &SourceRouter::AuthUiStateChanged, [&](bool isWaiting) {
        console.SetState(isWaiting ? ConsoleState::WAITING_TOKEN_URL : ConsoleState::COMMAND_MODE);
    });

    QObject::connect(&router, &SourceRouter::ProviderReady, [&](bool isOnline) {
        vkSyncIndex = 0;
        initPlaylistAndStart(isOnline);
    });

    QObject::connect(router.GetVkClient(), &IAudioProvider::AudioFetched, [&](const std::vector<Track>& tracks) {
        if (router.GetCurrentProvider() == router.GetVkClient()) onAudioFetched(tracks);
    });
    QObject::connect(router.GetVkClient(), &IAudioProvider::FinishedFetching, [&]() {
        if (router.GetCurrentProvider() == router.GetVkClient()) onFinishedFetching();
    });

    QObject::connect(router.GetSpotifyClient(), &IAudioProvider::AudioFetched, [&](const std::vector<Track>& tracks) {
        if (router.GetCurrentProvider() == router.GetSpotifyClient()) onAudioFetched(tracks);
    });
    QObject::connect(router.GetSpotifyClient(), &IAudioProvider::FinishedFetching, [&]() {
        if (router.GetCurrentProvider() == router.GetSpotifyClient()) onFinishedFetching();
    });

    QObject::connect(router.GetSoundCloudClient(), &IAudioProvider::AudioFetched, [&](const std::vector<Track>& tracks) {
        if (router.GetCurrentProvider() == router.GetSoundCloudClient()) onAudioFetched(tracks);
    });
    QObject::connect(router.GetSoundCloudClient(), &IAudioProvider::FinishedFetching, [&]() {
            if (router.GetCurrentProvider() == router.GetSoundCloudClient()) onFinishedFetching();
        });

    QObject::connect(router.GetYandexClient(), &IAudioProvider::AudioFetched, [&](const std::vector<Track>& tracks) {
        if (router.GetCurrentProvider() == router.GetYandexClient()) onAudioFetched(tracks);
    });
    QObject::connect(router.GetYandexClient(), &IAudioProvider::FinishedFetching, [&]() {
        if (router.GetCurrentProvider() == router.GetYandexClient()) onFinishedFetching();
    });


    QObject::connect(&app, &QCoreApplication::aboutToQuit, [&]() {
        Logger::Log(LogLevel::INFO, "Main: Saving session state...");
        settings.setValue("Session/Volume", audio.GetVolume());
        settings.setValue("Session/CurrentTrackIndex", playlist.GetCurrentAbsoluteIndex());
        settings.setValue("Session/Position", audio.GetPositionSeconds());
        settings.setValue("Session/Shuffle", playlist.IsShuffle());
        settings.setValue("Session/Repeat", playlist.GetRepeatMode());
        settings.sync();
    });

    router.SwitchSource(activeSource);
    console.Start();

    int exitCode = app.exec();
    Logger::Close();
    return exitCode;
}
```

## File: resources.qrc
```
<!DOCTYPE RCC><RCC version="1.0">
    <qresource prefix="/">
        <file>core/auth/auth.qml</file>
    </qresource>
</RCC>
```
