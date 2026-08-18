#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <string>

class SpotifyClient : public QObject {
    Q_OBJECT
public:
    explicit SpotifyClient(QObject* parent = nullptr);
    ~SpotifyClient();

    void ExchangeCodeForToken(const std::string& code, const QString& clientId, const QString& clientSecret);

    signals:
        void TokenReceived(const std::string& token);
    void AuthError(const std::string& errorString);

private:
    QNetworkAccessManager* m_manager;
};