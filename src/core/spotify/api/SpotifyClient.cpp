#include "SpotifyClient.h"
#include "utils/logger/Logger.h"
#include <QUrl>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>

SpotifyClient::SpotifyClient(QObject* parent) : QObject(parent), m_manager(new QNetworkAccessManager(this)) {
    Logger::Log(LogLevel::INFO, "SpotifyClient created.");
}

SpotifyClient::~SpotifyClient() {
    Logger::Log(LogLevel::INFO, "SpotifyClient destroyed.");
}

void SpotifyClient::ExchangeCodeForToken(const std::string& code, const QString& clientId, const QString& clientSecret) {
    QUrl url("https://accounts.spotify.com/api/token");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    QByteArray auth = (clientId + ":" + clientSecret).toUtf8().toBase64();
    request.setRawHeader("Authorization", "Basic " + auth);

    QString decodedCode = QUrl::fromPercentEncoding(QString::fromStdString(code).toUtf8());

    QByteArray body;
    body.append("grant_type=authorization_code");
    body.append("&code=" + QUrl::toPercentEncoding(decodedCode));
    body.append("&redirect_uri=" + QUrl::toPercentEncoding("http://127.0.0.1:8080/callback"));

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