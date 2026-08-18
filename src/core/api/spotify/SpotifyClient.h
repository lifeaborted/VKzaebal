#pragma once
#include "core/api/IAudioProvider.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>

class SpotifyClient : public IAudioProvider {
    Q_OBJECT
public:
    explicit SpotifyClient(QObject* parent = nullptr);
    ~SpotifyClient() override;

    void ExchangeCodeForToken(const std::string& code, const QString& clientId, const QString& clientSecret);

    void SetAccessToken(const std::string& token) override;
    void FetchTrackUrl(const std::string& trackId, std::function<void(const std::string&, bool)> callback) override;
    void FetchAllUserAudio(int offset = 0, int count = 200) override;

    signals:
        void TokenReceived(const std::string& token);
    void AuthError(const std::string& errorString);

private:
    QNetworkAccessManager* m_manager;
    std::string m_accessToken;
};