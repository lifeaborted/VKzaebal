#include "SpotifyClient.h"
#include "utils/logger/Logger.h"
#include <QUrlQuery>
#include <QJsonObject>
#include <QJsonArray>
#include <QCryptographicHash>
#include <QRandomGenerator>

SpotifyClient::SpotifyClient(QObject* parent) : BaseApiProvider(parent) {
    Logger::Log(LogLevel::INFO, "SpotifyClient created.");
}
SpotifyClient::~SpotifyClient() {
    Logger::Log(LogLevel::INFO, "SpotifyClient destroyed.");
}

bool SpotifyClient::HandleApiError(const QJsonDocument& json, int httpStatusCode) {
    if (httpStatusCode == 401) {
        emit AuthError("Token expired (401)");
        return true;
    }
    if (json.isObject() && json.object().contains("error")) {
        QJsonValue errVal = json.object()["error"];
        std::string errMsg = errVal.isString() ? errVal.toString().toStdString() : "Spotify API Error";
        if (errVal.isObject() && errVal.toObject().contains("message")) {
            errMsg = errVal.toObject()["message"].toString().toStdString();
        }
        Logger::Log(LogLevel::ERROR, "Spotify API Error [" + std::to_string(httpStatusCode) + "]: " + errMsg);
        emit AuthError(errMsg);
        return true;
    }
    return false;
}

void SpotifyClient::ValidateToken(std::function<void(bool)> callback) {
    if (m_accessToken.empty()) { callback(false); return; }
    QUrl url("https://api.spotify.com/v1/me");
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", QByteArray("Bearer ") + QString::fromStdString(m_accessToken).toUtf8());

    SendJsonRequest(request, [callback](const QJsonDocument&) {
        Logger::Log(LogLevel::INFO, "Spotify: Token is valid.");
        callback(true);
    }, [callback](const std::string&) {
        Logger::Log(LogLevel::WARNING, "Spotify: Token validation failed.");
        callback(false);
    });
}

void SpotifyClient::FetchTrackUrl(const std::string& trackId, std::function<void(const std::string&, bool)> callback) {
    callback("", false); // Заглушка
}

void SpotifyClient::FetchAllUserAudio(int offset, int count) {
    QUrl url(QString("https://api.spotify.com/v1/me/tracks?limit=50&offset=%1").arg(offset));
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", QByteArray("Bearer ") + QByteArray::fromStdString(m_accessToken));
    request.setHeader(QNetworkRequest::UserAgentHeader, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");

    SendJsonRequest(request, [this, offset](const QJsonDocument& json) {
        QJsonObject root = json.object();
        if (!root.contains("items") || !root["items"].isArray()) { emit FinishedFetching(); return; }

        QJsonArray items = root["items"].toArray();
        std::vector<Track> chunkTracks;
        chunkTracks.reserve(items.size());

        for (const QJsonValue& val : items) {
            if (!val.isObject()) continue;
            QJsonObject trackObj = val.toObject()["track"].toObject();
            Track track;
            track.id = trackObj["id"].toString().toStdString();
            track.source = "Spotify";
            track.title = trackObj.value("name").toString().toStdString();
            track.duration = trackObj.value("duration_ms").toInt(0) / 1000;
            track.ownerId = "spotify";

            if (trackObj.contains("artists") && trackObj["artists"].isArray()) {
                QJsonArray artistsArray = trackObj["artists"].toArray();
                QString artistName;
                for (int i = 0; i < artistsArray.size(); ++i) {
                    if (i > 0) artistName += ", ";
                    artistName += artistsArray[i].toObject().value("name").toString();
                }
                track.artist = artistName.toStdString();
            }
            if (trackObj.contains("album") && trackObj["album"].isObject()) {
                QJsonArray imagesArray = trackObj["album"].toObject()["images"].toArray();
                if (!imagesArray.isEmpty()) track.coverUrl = imagesArray[0].toObject().value("url").toString().toStdString();
            }
            if (!track.id.empty() && !track.title.empty()) chunkTracks.push_back(std::move(track));
        }
        if (!chunkTracks.empty()) emit AudioFetched(chunkTracks);

        int total = root.value("total").toInt(0);
        if (offset + items.size() < total && !items.isEmpty()) FetchAllUserAudio(offset + items.size(), 50);
        else emit FinishedFetching();
    }, [this](const std::string&) { emit FinishedFetching(); });
}

std::string SpotifyClient::StartAuthPkce(const QString& clientId) {
    m_clientId = clientId;
    const QString possibleChars("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~");
    m_codeVerifier.clear();
    for (int i = 0; i < 64; ++i) m_codeVerifier.append(possibleChars.at(QRandomGenerator::global()->generate() % possibleChars.length()));
    QString codeChallenge = QCryptographicHash::hash(m_codeVerifier.toUtf8(), QCryptographicHash::Sha256).toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);

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

    SendJsonRequest(request, [this](const QJsonDocument& json) {
        std::string token = json.object()["access_token"].toString().toStdString();
        if (!token.empty()) {
            Logger::Log(LogLevel::INFO, "Spotify: Access Token successfully obtained via PKCE!");
            emit TokenReceived(token);
        } else emit AuthError("Access token not found in response.");
    }, nullptr, postData);
}

void SpotifyClient::AuthWithSpDc(const QString& spDcCookie) {
    QUrl url("https://open.spotify.com/get_access_token?reason=transport&productType=web_player");
    QNetworkRequest request(url);
    request.setRawHeader("Cookie", QByteArray("sp_dc=") + spDcCookie.toUtf8());
    request.setHeader(QNetworkRequest::UserAgentHeader, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");

    SendJsonRequest(request, [this](const QJsonDocument& json) {
        std::string token = json.object()["accessToken"].toString().toStdString();
        if (!token.empty()) {
            Logger::Log(LogLevel::INFO, "Spotify: Web Access Token successfully obtained via sp_dc!");
            emit TokenReceived(token);
        } else emit AuthError("Token not found in response.");
    });
}