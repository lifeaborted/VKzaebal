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