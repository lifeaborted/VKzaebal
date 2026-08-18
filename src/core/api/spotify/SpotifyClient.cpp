#include "SpotifyClient.h"
#include "utils/logger/Logger.h"
#include <QUrl>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCryptographicHash>
#include <QRandomGenerator>

SpotifyClient::SpotifyClient(QObject* parent) : IAudioProvider(parent), m_manager(new QNetworkAccessManager(this)) {
    Logger::Log(LogLevel::INFO, "SpotifyClient created.");
}

SpotifyClient::~SpotifyClient() {
    Logger::Log(LogLevel::INFO, "SpotifyClient destroyed.");
}

QString SpotifyClient::GenerateAuthUrl(const QString& clientId) {
    // 1. Генерируем code_verifier (Случайная строка от 43 до 128 символов)
    const QString possibleCharacters("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~");
    m_codeVerifier.clear();
    for(int i = 0; i < 64; ++i) {
        int index = QRandomGenerator::global()->bounded(possibleCharacters.length());
        m_codeVerifier.append(possibleCharacters.at(index));
    }

    // 2. Генерируем code_challenge (Хеш SHA-256 от verifier, закодированный в Base64URL)
    QByteArray hash = QCryptographicHash::hash(m_codeVerifier.toUtf8(), QCryptographicHash::Sha256);
    QString codeChallenge = hash.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);

    // 3. Собираем ссылку для авторизации вручную, строго контролируя кодирование
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

    // Собираем POST тело по стандарту PKCE
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

void SpotifyClient::SetAccessToken(const std::string& token) {
    m_accessToken = token;
}

void SpotifyClient::FetchTrackUrl(const std::string& trackId, std::function<void(const std::string&, bool)> callback) {
    // ЗАГЛУШКА
    callback("", false);
}

void SpotifyClient::FetchAllUserAudio(int offset, int count) {
    // ЗАГЛУШКА
    emit FinishedFetching();
}