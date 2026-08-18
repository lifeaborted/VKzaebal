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

QString SpotifyClient::GenerateAuthUrl(const QString& clientId) {
    const QString possibleCharacters("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~");
    m_codeVerifier.clear();
    for(int i = 0; i < 64; ++i) {
        int index = QRandomGenerator::global()->bounded(possibleCharacters.length());
        m_codeVerifier.append(possibleCharacters.at(index));
    }

    QByteArray hash = QCryptographicHash::hash(m_codeVerifier.toUtf8(), QCryptographicHash::Sha256);
    QString codeChallenge = hash.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);

    QString authUrl = "https://accounts.spotify.com/authorize?";
    authUrl += "client_id=" + QUrl::toPercentEncoding(clientId);
    authUrl += "&response_type=code";
    authUrl += "&redirect_uri=" + QUrl::toPercentEncoding("http://127.0.0.1:8080/callback");
    authUrl += "&scope=" + QUrl::toPercentEncoding("user-library-read user-read-playback-state playlist-read-private");
    authUrl += "&show_dialog=true";
    authUrl += "&code_challenge_method=S256";
    authUrl += "&code_challenge=" + QUrl::toPercentEncoding(codeChallenge);

    return authUrl;
}

void SpotifyClient::ExchangeCodeForToken(const std::string& code, const QString& clientId) {
    QUrl url("https://accounts.spotify.com/api/token");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    QString decodedCode = QUrl::fromPercentEncoding(QString::fromStdString(code).toUtf8());

    QByteArray body;
    body.append("grant_type=authorization_code");
    body.append("&client_id=" + QUrl::toPercentEncoding(clientId));
    body.append("&code=" + QUrl::toPercentEncoding(decodedCode));
    body.append("&redirect_uri=" + QUrl::toPercentEncoding("http://127.0.0.1:8080/callback"));
    body.append("&code_verifier=" + QUrl::toPercentEncoding(m_codeVerifier));

    QNetworkReply* reply = m_manager->post(request, body);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() == QNetworkReply::NoError) {
            QJsonDocument json = QJsonDocument::fromJson(reply->readAll());
            std::string token = json.object()["access_token"].toString().toStdString();
            emit TokenReceived(token);
        } else {
            std::string err = reply->errorString().toStdString();
            Logger::Log(LogLevel::ERROR, "Spotify token exchange failed: " + err);
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
        QJsonObject root = json.object();
        QJsonArray items = root["items"].toArray();

        std::vector<Track> chunkTracks;
        chunkTracks.reserve(items.size());

        for (const QJsonValue& val : items) {
            QJsonObject trackObj = val.toObject()["track"].toObject();
            Track track;

            track.id = trackObj["id"].toString().toStdString();
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