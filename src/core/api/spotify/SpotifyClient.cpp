#include "SpotifyClient.h"
#include "utils/logger/Logger.h"
#include <QUrl>
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

void SpotifyClient::AuthWithSpDc(const QString& spDcCookie) {
    // Стучимся на скрытый эндпоинт выдачи веб-токенов
    QUrl url("https://open.spotify.com/get_access_token?reason=transport&productType=web_player");
    QNetworkRequest request(url);

    // Имитируем реальный браузер (Добавлены Origin и Referer)
    request.setRawHeader("Cookie", QByteArray("sp_dc=") + spDcCookie.toUtf8());
    request.setHeader(QNetworkRequest::UserAgentHeader, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("App-Platform", "WebPlayer");
    request.setRawHeader("Origin", "https://open.spotify.com");    // <--- ДОБАВЛЕНО
    request.setRawHeader("Referer", "https://open.spotify.com/");  // <--- ДОБАВЛЕНО
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
            Logger::Log(LogLevel::ERROR, "Spotify Web Auth failed: " + err);
            emit AuthError(err);
        }
        reply->deleteLater();
    });
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
    // Ускоренная сборка URL без QUrlQuery
    QByteArray urlBytes = "https://api.spotify.com/v1/me/tracks?limit=50&offset=" + QByteArray::number(offset);
    QUrl url = QUrl(QString::fromUtf8(urlBytes));

    QNetworkRequest request(url);
    request.setRawHeader("Authorization", QByteArray("Bearer ") + QByteArray::fromStdString(m_accessToken));

    QNetworkReply* reply = m_manager->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply, offset]() {
        if (reply->error() != QNetworkReply::NoError) {
            Logger::Log(LogLevel::ERROR, "Spotify API Error: " + reply->errorString().toStdString());

            if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 401) {
                emit TokenExpired();
            }
            reply->deleteLater();
            return;
        }

        QByteArray response_data = reply->readAll();
        QJsonDocument json = QJsonDocument::fromJson(response_data);

        if (json.isNull() || !json.isObject()) {
            Logger::Log(LogLevel::ERROR, "Spotify API: Received invalid JSON");
            emit FinishedFetching();
            reply->deleteLater();
            return;
        }

        QJsonObject root = json.object();
        QJsonArray items = root["items"].toArray();

        std::vector<Track> chunkTracks;
        chunkTracks.reserve(items.size());

        for (const QJsonValue& val : items) {
            QJsonObject trackObj = val.toObject()["track"].toObject();
            Track track;

            track.id = trackObj["id"].toString().toStdString();
            track.source = "Spotify";
            track.title = trackObj["name"].toString().toStdString();
            track.duration = trackObj["duration_ms"].toInt() / 1000;
            track.ownerId = "spotify";
            track.url = "";

            QJsonArray artistsArray = trackObj["artists"].toArray();
            QString artistName;
            for (int i = 0; i < artistsArray.size(); ++i) {
                if (i > 0) artistName += ", ";
                artistName += artistsArray[i].toObject()["name"].toString();
            }
            track.artist = artistName.toStdString();

            QJsonArray imagesArray = trackObj["album"].toObject()["images"].toArray();
            if (!imagesArray.isEmpty()) {
                track.coverUrl = imagesArray[0].toObject()["url"].toString().toStdString();
            }

            chunkTracks.push_back(std::move(track));
        }

        if (!chunkTracks.empty()) {
            emit AudioFetched(chunkTracks);
        }

        int total = root["total"].toInt();
        Logger::Log(LogLevel::INFO, "Spotify: Fetched chunk offset " + std::to_string(offset) + ", items: " + std::to_string(items.size()));

        if (offset + items.size() < total && !items.isEmpty()) {
            FetchAllUserAudio(offset + items.size(), 50);
        } else {
            emit FinishedFetching();
        }

        reply->deleteLater();
    });
}